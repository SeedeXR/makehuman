// SPDX-License-Identifier: AGPL-3.0-or-later
//
// The application. This is the AGPL side: it loads the mesh through mh::core
// and hands the UI a plain view, which is what lets mh_ui and mh_render stay
// Apache-2.0.
#include "makehuman/core/AssetIndex.h"
#include "makehuman/core/Blendshape.h"
#include "makehuman/core/CorrectiveBlob.h"
#include "makehuman/core/CorrectiveCache.h"
#include "makehuman/core/Decimator.h"
#include "makehuman/core/Material.h"
#include "makehuman/core/Mesh.h"
#include "makehuman/core/Mhm.h"
#include "makehuman/core/ObjReader.h"
#include "makehuman/core/Proxy.h"
#include "makehuman/core/Random.h"
#include "makehuman/core/RenderMesh.h"
#include "makehuman/core/SkinTone.h"
#include "makehuman/core/SliderLayout.h"
#include "makehuman/core/Subdivider.h"
#include "makehuman/core/SurfaceWalk.h"
#include "makehuman/core/Symmetry.h"
#include "makehuman/core/Target.h"
#include "makehuman/core/TargetIndex.h"
#include "makehuman/core/TopologyHash.h"
#include "makehuman/foundation/DataDir.h"
#include "makehuman/foundation/Naming.h"
#include "makehuman/foundation/NormalBlend.h"
#include "makehuman/foundation/Version.h"
#include "makehuman/io/BvhWriter.h"
#include "makehuman/io/Compact.h"
#include "makehuman/io/FbxWriter.h"
#include "makehuman/io/GltfWriter.h"
#include "makehuman/io/ObjWriter.h"
#include "makehuman/io/SceneIO.h"
#include "makehuman/io/UsdWriter.h"
#include "makehuman/render/OffscreenRenderer.h"
#include "makehuman/rig/BvhPose.h"
#include "makehuman/rig/CorrectiveRuntime.h"
#include "makehuman/rig/EyeAim.h"
#include "makehuman/rig/Facs.h"
#include "makehuman/rig/PoseUnits.h"
#include "makehuman/rig/PosedMesh.h"
#include "makehuman/rig/Skeleton.h"
#include "makehuman/rig/Skinning.h"
#include "makehuman/rig/VertexWeights.h"
#include "makehuman/rig/Wrinkles.h"
#include "makehuman/ui/AssetPanel.h"
#include "makehuman/ui/Background.h"
#include "makehuman/ui/FrameStats.h"
#include "makehuman/ui/ImageViewer.h"
#include "makehuman/ui/Language.h"
#include "makehuman/ui/MacroStatus.h"
#include "makehuman/ui/MainWindow.h"
#include "makehuman/ui/ModifierPanel.h"
#include "makehuman/ui/RenderDialog.h"
#include "makehuman/ui/TaskRegistry.h"
#include "makehuman/ui/Theme.h"
#include "makehuman/ui/UndoCommands.h"

#include <QApplication>
#include <QCommandLineParser>
#include <QFileDialog>
#include <QFont>
#include <QHash>
#include <QIcon>
#include <QMessageBox>
#include <QPixmap>
#include <QStatusBar>
#include <QTimer>
#include <QUndoStack>
#include "makehuman/ui/ViewportWidget.h"

#include <QImage>

#include <algorithm>
#include <array>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <map>
#include <memory>
#include <optional>
#include <ranges>
#include <span>
#include <sstream>
#include <string>
#include <string_view>

namespace {

/// The asset root, resolved once at startup.
///
/// A process-wide constant, so a set-once accessor rather than a parameter
/// threaded through every loader: the alternative is five signatures carrying a
/// value that never differs between calls.
std::filesystem::path& dataRoot() {
    static std::filesystem::path root{MH_DATA_DIR};
    return root;
}

void setDataRoot(std::filesystem::path root) {
    dataRoot() = std::move(root);
}

const std::filesystem::path& dataDir() {
    return dataRoot();
}

/// The rig the application skins with, as a stem under `data/rigs` (so
/// "default" means default.mhskel + default_weights.mhw) or an explicit path to
/// a .mhskel. Set once from --rig, before anything loads a skeleton.
///
/// This exists because main.cpp hard-coded "default" and the 179-bone Mixamo
/// superset was therefore unreachable from the application -- which is exactly
/// why nothing ever noticed it could not build its rest matrices.
std::string& rigNameRef() {
    static std::string name{"default"};
    return name;
}

void setRigName(std::string name) {
    rigNameRef() = std::move(name);
}

/// The skeleton file for the selected rig, and the weights beside it.
std::filesystem::path rigFile(std::string_view suffix) {
    const std::string& name = rigNameRef();
    // An explicit path wins; a bare stem is looked up under data/rigs. Reduce
    // both to a base with no extension, so `--rig /x/foo.mhskel`, `--rig /x/foo`
    // and `--rig default` all behave the same.
    std::filesystem::path base = (name.find('/') != std::string::npos || name.ends_with(".mhskel"))
                                     ? std::filesystem::path{name}
                                     : dataDir() / "rigs" / name;
    if (base.extension() == ".mhskel") base.replace_extension();
    base += std::string(suffix);
    return base;
}

/// Every rig that is actually installed, so an unknown --rig can say what IS
/// available instead of making the user guess.
/// The rigs on disk, by stem, sorted. One source of truth for the `--rig` error
/// message and for the Skeleton picker -- they listed different things when
/// this was a string-joining function and the picker did its own scan.
std::vector<std::string> rigStems() {
    std::vector<std::string> stems;
    std::error_code ec;
    for (const auto& e : std::filesystem::directory_iterator(dataDir() / "rigs", ec)) {
        if (e.path().extension() == ".mhskel") stems.push_back(e.path().stem().string());
    }
    std::ranges::sort(stems);
    return stems;
}

std::string availableRigs() {
    std::string out;
    for (const auto& st : rigStems()) {
        if (!out.empty()) out += ", ";
        out += st;
    }
    return out;
}

/// The rig, its pose and the live-rig capture. Moved into `mh::rig` so the
/// posing it drives can be tested: `main.cpp` is not linkable, and the one
/// mutation that survived session fifteen was inside the helper that used to
/// live here. Aliased rather than renamed at every use.
using PoseRig = mh::rig::PoseRig;

/// Layers an expression onto @p pose, in model space.
///
/// The recipe is the reference's and every step of it is parity-tested
/// elsewhere: build the 60 face units from `face-poseunits.{bvh,json}`, blend
/// the ones the expression names at their weights, then `mixPoses` the result
/// over the body pose for exactly the bones the blend moves.
///
/// **The bone list is derived, not configured.** It is every bone the BLEND
/// leaves non-identity, so an expression touches the jaw and lips it actually
/// uses and nothing else -- a hardcoded "face bones" list would go stale the
/// first time a rig gained a bone.
///
/// Takes an `Expression` rather than a path, so that `--expression`, which
/// reads one from a `.mhpose`, and `--facs`, which derives one from Action
/// Units, cannot end up applying it differently.
///
/// @param pose empty for an unposed body, in which case the expression alone
///        becomes the pose.
bool applyExpressionUnits(const mh::rig::Expression& expr, const mh::rig::Skeleton& skel,
                          std::vector<mh::foundation::Mat4>& pose) {
    const auto bvh = mh::io::readBvh(dataDir() / "poseunits" / "face-poseunits.bvh");
    if (!bvh) {
        std::fprintf(stderr, "cannot read the face pose units\n");
        return false;
    }
    auto names = mh::rig::loadPoseUnitNames(dataDir() / "poseunits" / "face-poseunits.json");
    if (!names) {
        std::fprintf(stderr, "cannot read the pose unit names: %s\n",
                     names.error().message().c_str());
        return false;
    }
    const auto units = mh::rig::makePoseUnits(*bvh, skel, std::move(*names));
    if (!units) {
        std::fprintf(stderr, "cannot build the pose units: %s\n", units.error().message().c_str());
        return false;
    }

    std::vector<size_t> indices;
    std::vector<float> weights;
    for (const mh::rig::WeightedUnit& u : expr.units) {
        const auto at = units->indexOf(u.name);
        if (!at) {
            // Named, not skipped: a typo in an expression file otherwise
            // produces a subtly wrong face and no way to find out why.
            std::fprintf(stderr, "no such pose unit \"%s\" in the face library\n", u.name.c_str());
            return false;
        }
        indices.push_back(*at);
        weights.push_back(u.weight);
    }

    const auto blended = units->blend(indices, weights);
    if (blended.size() != skel.boneCount()) {
        std::fprintf(stderr, "the expression blend does not match the rig\n");
        return false;
    }

    std::vector<size_t> faceBones;
    for (size_t b = 0; b < blended.size(); ++b) {
        const mh::foundation::Mat4 id = mh::foundation::Mat4::identity();
        bool moved                    = false;
        for (size_t r = 0; r < 4 && !moved; ++r) {
            for (size_t c = 0; c < 4; ++c) {
                if (std::abs(blended[b].m[r][c] - id.m[r][c]) > 1e-6F) {
                    moved = true;
                    break;
                }
            }
        }
        if (moved) faceBones.push_back(b);
    }

    if (pose.empty()) {
        pose = blended;
    } else {
        const auto mixed = mh::rig::mixPoses(pose, blended, faceBones);
        if (!mixed) {
            std::fprintf(stderr, "cannot layer the expression onto the pose: %s\n",
                         mixed.error().message().c_str());
            return false;
        }
        pose = *mixed;
    }
    std::printf("expression %s (%zu units, %zu bones)\n", expr.name.c_str(), expr.units.size(),
                faceBones.size());
    return true;
}

bool applyExpression(const std::filesystem::path& file, const mh::rig::Skeleton& skel,
                     std::vector<mh::foundation::Mat4>& pose) {
    const auto expr = mh::rig::loadExpression(file);
    if (!expr) {
        std::fprintf(stderr, "cannot load expression: %s\n", expr.error().message().c_str());
        return false;
    }
    return applyExpressionUnits(*expr, skel, pose);
}

bool applyFacs(std::span<const mh::rig::ActionUnit> aus, const mh::rig::Skeleton& skel,
               std::vector<mh::foundation::Mat4>& pose) {
    const auto expr = mh::rig::facsExpression(aus);
    if (!expr) {
        std::fprintf(stderr, "cannot build the FACS expression: %s\n",
                     expr.error().message().c_str());
        return false;
    }
    return applyExpressionUnits(*expr, skel, pose);
}

/// The `--pose-frame` index, or none. Set once at start-up.
///
/// A file-scope value for the same reason as the expression file: every
/// `loadPoseRig` call site wants the same frame, and none of them has an
/// opinion about it.
std::optional<size_t>& poseFrameRef() {
    static std::optional<size_t> frame;
    return frame;
}

/// The `--expression` file, or empty. Set once at start-up.
///
/// A file-scope value for the same reason as the skinning method: every
/// `loadPoseRig` call site -- start-up, the Pose picker, the Skeleton picker --
/// wants the same expression, and none of them has an opinion about it.
std::filesystem::path& expressionFileRef() {
    static std::filesystem::path path;
    return path;
}

/// A user-facing workspace name to the preset the window knows, through the
/// shipped name table.
///
/// Owner directive 12.1: one resolver, and the table is DATA. The window still
/// keys presets by their LEGACY name -- that is what `workspacePresets()` and
/// every saved workspace file already say -- so this maps whatever the user
/// typed onto the canonical id and back to that legacy spelling. The profile
/// decides which column is tried first; the other is the fallback, and the
/// fallback warns ONCE per name so a user learns the new word without being
/// stopped by it.
///
/// A name the table does not know is returned unchanged, so the window reports
/// "no such workspace preset" with what the user actually typed.
/// The shipped workspace name table, read once.
///
/// One read for two consumers -- this resolver and the window's menu labels --
/// so the file cannot be present for one and missing for the other.
const std::expected<mh::foundation::NameTable, mh::foundation::NameError>& workspaceNames() {
    static const auto table =
        mh::foundation::loadNameTable(dataDir() / "naming" / "workspace.names");
    return table;
}

QString resolveWorkspaceName(mh::foundation::NamingProfile profile, const QString& name) {
    const auto& table = workspaceNames();
    if (!table) {
        // The table is data and can be missing; the presets still work under
        // their legacy names, which is the default profile anyway.
        return name;
    }
    const auto hit = mh::foundation::resolve(*table, profile, name.toStdString());
    if (!hit) return name;
    if (hit->viaFallback) {
        // Once per name is the directive; once per RUN is what happens, because
        // `--workspace` takes a single value and this is its only caller. A
        // `static std::set` of already-warned names was written here and a
        // mutation showed it could never fire twice -- so it went, and the
        // de-duplication belongs with the second caller when there is one.
        const auto display = mh::foundation::displayName(*table, profile, hit->canonical);
        if (display) {
            std::fprintf(stderr,
                         "\"%s\" is the other profile's name for this; %s is the one to use\n",
                         name.toStdString().c_str(), std::string(*display).c_str());
        }
    }
    const auto legacy =
        mh::foundation::displayName(*table, mh::foundation::NamingProfile::Legacy, hit->canonical);
    return legacy ? QString::fromStdString(std::string(*legacy)) : name;
}

/// The `--facs` request, or empty. Set once at start-up, for the same reason as
/// `expressionFileRef`.
/// The `--pose-unit` request, in the order given, or empty.
///
/// The order is kept because `PoseUnits::blend` multiplies quaternions and does
/// not commute: the sequence a user typed IS the expression, exactly as the
/// order inside a `.mhpose` is.
std::vector<mh::rig::WeightedUnit>& poseUnitsRef() {
    static std::vector<mh::rig::WeightedUnit> units;
    return units;
}

std::vector<mh::rig::ActionUnit>& facsRef() {
    static std::vector<mh::rig::ActionUnit> aus;
    return aus;
}

/// The expression the command line asked for, from whichever flag said so.
///
/// `--facs` and `--pose-unit` are two vocabularies for one thing -- an Action
/// Unit names the pose units it moves -- so they converge here, and
/// `--save-expression` writes whichever was used without knowing which.
std::expected<mh::rig::Expression, std::string> requestedExpression() {
    if (!poseUnitsRef().empty()) {
        mh::rig::Expression expr;
        expr.units = poseUnitsRef();
        return expr;
    }
    const auto expr = mh::rig::facsExpression(facsRef());
    if (!expr) return std::unexpected(expr.error().message());
    return *expr;
}

/// Where the eyes are looking, or nothing. Set once from `--look-at`.
///
/// A file-scope value for the same reason as `gUseDualQuaternion`: every
/// `loadPoseRig` call site -- start-up, the Pose picker, the Skeleton picker --
/// has to apply it, and none of them has an opinion about it.
std::optional<mh::foundation::Vec3> gLookAt;

/// Loads the rig, and the pose named by @p pose if there is one.
///
/// "A-pose" is not a file: the MakeHuman base mesh is authored in one, so the
/// rest mesh IS the A-pose and posing it would be posing it twice. Only a pose
/// that differs from the authored rest needs a BVH.
///
/// **The rig loads either way.** It used to return here for "rest", so
/// `--rig mixamo_superset` with no `--pose` loaded no skeleton at all and the
/// export could not have carried one -- the bind pose being precisely the most
/// useful thing to export.
bool loadPoseRig(const mh::core::Mesh& mesh, const std::string& pose, PoseRig& out) {
    const std::filesystem::path& expressionFile = expressionFileRef();
    const bool wantPose = !(pose == "rest" || pose == "apose" || pose == "a-pose");

    std::filesystem::path file = pose;
    if (pose == "tpose" || pose == "t-pose") file = dataDir() / "poses" / "tpose.bvh";

    const auto skelPath = rigFile(".mhskel");
    if (!std::filesystem::exists(skelPath)) {
        std::fprintf(stderr, "unknown --rig %s; available: %s\n", rigNameRef().c_str(),
                     availableRigs().c_str());
        return false;
    }
    auto skel = mh::rig::loadSkeleton(skelPath);
    if (!skel) {
        std::fprintf(stderr, "cannot load the rig: %s\n", skel.error().message().c_str());
        return false;
    }
    if (!skel->updateJoints(mesh.coord()) || !skel->buildRestMatrices()) {
        std::fprintf(stderr, "cannot build the rest pose\n");
        return false;
    }

    auto weights = mh::rig::loadWeights(rigFile("_weights.mhw"), mesh.vertexCount());
    if (!weights) {
        std::fprintf(stderr, "cannot load weights: %s\n", weights.error().message().c_str());
        return false;
    }

    // 4 is what glTF's JOINTS_0/WEIGHTS_0 allow, and it clamps ~19% of the
    // shipped rig's vertices while the reference truncates nothing. Measured
    // cost under a hard 60-degree pose: ONE vertex of 19,158 moves more than
    // 10 microns (that one by 2.0 mm), so this is reported, not fixed --
    // see memory/todo.md, M5.
    out.weights = weights->compile(*skel, mh::io::kGltfInfluences);
    std::printf("rig %s (%zu bones)\n", rigNameRef().c_str(), skel->boneCount());
    if (out.weights.clampedVertices > 0) {
        std::printf("  clamped %zu of %zu vertices to 4 influences (rig uses up to %u)\n",
                    out.weights.clampedVertices, mesh.vertexCount(), out.weights.maxInfluences);
    }

    // The body pose and the expression are independent: either, both or
    // neither. Both end up in ONE model-space pose that is converted to
    // bone-local once, because converting twice would conjugate the second
    // through the first's frame.
    std::vector<mh::foundation::Mat4> modelPose;
    if (wantPose) {
        // Naming a frame is how a caller says "I know this is an animation";
        // without one, loadBodyPose keeps refusing multi-frame files.
        const auto bodyPose = poseFrameRef()
                                  ? mh::rig::loadBodyPoseFrame(file, *skel, *poseFrameRef())
                                  : mh::rig::loadBodyPose(file, *skel);
        if (!bodyPose) {
            std::fprintf(stderr, "cannot load pose: %s\n", bodyPose.error().message().c_str());
            return false;
        }
        modelPose = *bodyPose;
    }

    if (!expressionFile.empty()) {
        if (!applyExpression(expressionFile, *skel, modelPose)) return false;
    }
    if (!poseUnitsRef().empty()) {
        // The same path an expression FILE takes -- a raw unit request and a
        // .mhpose are the same thing, one typed and one on disk.
        mh::rig::Expression expr;
        expr.name  = "command line";
        expr.units = poseUnitsRef();
        if (!applyExpressionUnits(expr, *skel, modelPose)) return false;
    }

    if (!facsRef().empty()) {
        if (!applyFacs(facsRef(), *skel, modelPose)) return false;
    }

    if (!modelPose.empty()) {
        // The file's rotations are in model space; skinning wants them in each
        // bone's rest frame. Skipping this yields a plausible but wrong pose.
        out.localPose = mh::rig::poseToBoneLocal(*skel, modelPose);
    }

    // The eye aim goes on AFTER the conversion, because `aimEyes` already works
    // in each bone's own rest frame -- putting it in `modelPose` would send it
    // through `poseToBoneLocal` and conjugate it a second time.
    //
    // It writes only the two eye entries, so it composes onto whatever pose was
    // loaded rather than replacing it. With no pose at all the array is filled
    // with identities first: looking at something IS a pose, and `PoseRig` is
    // only "posed" when there is one.
    if (gLookAt.has_value()) {
        if (out.localPose.empty()) {
            out.localPose.assign(skel->boneCount(), mh::foundation::Mat4::identity());
        }
        const auto aim = mh::rig::aimEyes(*skel, *gLookAt, out.localPose);
        if (!aim) {
            std::fprintf(stderr, "cannot aim the eyes: %s\n", aim.error().message().c_str());
            return false;
        }
        std::printf("look-at: eyes turned %.1f and %.1f degrees%s\n", aim->leftDegrees,
                    aim->rightDegrees, aim->clamped ? " (clamped to the eye's range)" : "");
        // The eyes already had a driver: FACS AU61-64 rotate the same two
        // bones through pose units, and this REPLACES those entries while
        // every other bone the unit touches survives. Measured:
        // `--facs AU61=1.0` alone moves 1,721 vertices, `--look-at` alone
        // 1,144, together 1,722 -- neither. The eyelids follow the unit and the
        // eyeballs ignore it.
        //
        // Overriding is right, since a look-at is a constraint, but a silent
        // partial application is how someone spends an afternoon wondering why
        // half an expression took.
        if (aim->replacedExistingPose) {
            std::fprintf(stderr,
                         "warning: look-at replaced an eye rotation already in the pose "
                         "(a FACS gaze unit, or a pose file) -- the constraint wins for the "
                         "eyeballs, and everything else that pose set is unaffected\n");
        }
    }

    out.skeleton = std::move(*skel);
    return true;
}

/// Which skinning method `poseInPlace` uses. Set once from `--skinning`.
///
/// A file-scope flag rather than a parameter threaded through: the pose helper
/// is called from four places and none of them has an opinion about the method
/// -- the user does, once, at start-up.
///
/// Initialised to match `--skinning`'s own default so the two cannot disagree,
/// but the option is the authority: main() assigns this from the parsed value
/// unconditionally, and nothing reads it before then.
bool gUseDualQuaternion = true;

/// Whether a loaded pose is APPLIED. The reference's `_posed`
/// (`shared/animation.py:986-994`), driven by the toolbar's Pose toggle: being
/// posed is this flag AND a pose being loaded, which is what `PoseRig::posed()`
/// answers.
///
/// A file-scope flag for the same reason as the one above -- the pose helper is
/// called from several places and none of them has an opinion.
bool gApplyPose = true;

/// The corrective set `--correctives` bound, or nothing.
///
/// A file-scope owner for the same reason as the two flags above: `poseInPlace`
/// is called from several places and none of them has an opinion about it.
///
/// The three parts must live together and in this order. `runtime` holds spans
/// into `bytes` -- that is the point of the mappable blob layout -- so the bytes
/// have to outlive it, and `blob` is the view that produced those spans.
struct BoundCorrectives {
    std::vector<std::byte> bytes;
    mh::core::CompiledCorrectives blob;
    mh::rig::CorrectiveRuntime runtime;
    /// Kept because the wrinkle paths live here and not in the blob -- see
    /// `core::CorrectiveCache::manifest`.
    mh::core::CorrectiveManifest manifest;
};

std::unique_ptr<BoundCorrectives> gCorrectives;

/// The wrinkle map the bound correctives are asking for this frame, if any.
///
/// Reports the maps it could not show, ONCE. `render::MeshInstance` carries one
/// map per mesh, so a set whose fired poses name several sheets loses all but
/// the strongest -- and an author whose elbow crease never appears has no other
/// way to find out. Printed per render rather than per frame because the CLI
/// renders once; the interactive path would want it rate-limited.
mh::rig::WrinkleChoice wrinkleForFrame() {
    if (gCorrectives == nullptr) return {};
    const mh::rig::WrinkleChoice choice = mh::rig::chooseWrinkle(
        gCorrectives->manifest, gCorrectives->blob.poseNames, gCorrectives->runtime.weights());
    // Reported when it CHANGES, not on every call. `buildScene` runs twice for
    // one `--render` -- measured, the line printed twice -- and in the window it
    // runs on every slider drag, so printing unconditionally is noise and
    // printing once ever goes stale the moment the pose moves.
    //
    // Safe as a static because nothing in this file is threaded -- checked, no
    // QtConcurrent, no std::thread, no QThread.
    static std::string reported;
    std::array<char, 512> buf{};
    std::string report;
    if (!choice.map.empty()) {
        std::snprintf(buf.data(), buf.size(), "wrinkle: %s at %.2f",
                      choice.map.filename().string().c_str(), static_cast<double>(choice.weight));
        report = buf.data();
    }
    if (choice.dropped > 0) {
        std::snprintf(buf.data(), buf.size(),
                      "\nwarning: %zu wrinkle maps not shown (%s) -- one mesh carries one map, "
                      "and %s is the strongest",
                      choice.dropped, choice.droppedNames.c_str(),
                      choice.map.filename().string().c_str());
        report += buf.data();
    }
    if (!report.empty() && report != reported) {
        std::printf("%s\n", report.c_str());
        reported = report;
    }
    return choice;
}

/// Bakes the fired wrinkle sheet into a normal map beside @p exportPath, and
/// returns where it put it.
///
/// No interchange format has a pose-driven normal map -- glTF, FBX and UsdSkel
/// each give a material ONE normal texture -- so the blend is baked at the pose
/// being written. Without this the app reports the wrinkle and then writes a
/// file with no crease in it, which is exactly the silence the live-rig
/// corrective export was caught doing.
///
/// **The bake is correct at the exported pose and nowhere else.** A consumer
/// that re-poses the rig keeps these creases. That is a property of the
/// formats, not of this function, and it is the same trade the correctives make
/// when they refuse to bake into rest geometry -- except that here there is no
/// alternative to refuse in favour of.
///
/// Written beside the export and named after it. A GLB EMBEDS its images, so
/// the sidecar is redundant there and required by every other format; one rule
/// beats a per-format one.
std::optional<std::filesystem::path> bakeWrinkleBeside(
    const std::filesystem::path& exportPath, const mh::foundation::MaterialDesc& material) {
    if (gCorrectives == nullptr) return std::nullopt;
    const mh::rig::WrinkleChoice choice = mh::rig::chooseWrinkle(
        gCorrectives->manifest, gCorrectives->blob.poseNames, gCorrectives->runtime.weights());
    if (choice.map.empty() || choice.weight <= 0.0F) return std::nullopt;

    QImage sheet(QString::fromStdString(choice.map.string()));
    if (sheet.isNull()) {
        std::fprintf(stderr, "cannot bake the wrinkle: %s will not load\n",
                     choice.map.string().c_str());
        return std::nullopt;
    }
    sheet = sheet.convertToFormat(QImage::Format_RGBA8888);

    // A material with no normal map still exports its creases: the base becomes
    // a flat sheet the size of the wrinkle, so the result is the crease alone,
    // normalized. `default.mhmat` is exactly this case -- eight of the nine
    // shipped skins name a normal map and it does not.
    QImage base;
    if (!material.normalTexture.empty()) {
        base = QImage(QString::fromStdString(material.normalTexture.string()));
        if (base.isNull()) {
            std::fprintf(stderr, "cannot bake the wrinkle: %s will not load\n",
                         material.normalTexture.string().c_str());
            return std::nullopt;
        }
        base = base.convertToFormat(QImage::Format_RGBA8888);
    } else {
        base = QImage(sheet.width(), sheet.height(), QImage::Format_RGBA8888);
        base.fill(QColor(128, 128, 255));
    }

    const auto baked = mh::foundation::bakeWrinkleIntoNormalMap(
        mh::foundation::NormalMapImage{
            std::span<const uint8_t>(base.constBits(), static_cast<size_t>(base.sizeInBytes())),
            base.width(), base.height()},
        1.0F,
        mh::foundation::NormalMapImage{
            std::span<const uint8_t>(sheet.constBits(), static_cast<size_t>(sheet.sizeInBytes())),
            sheet.width(), sheet.height()},
        choice.weight);
    if (!baked) {
        std::fprintf(stderr, "cannot bake the wrinkle: %s\n", baked.error().message().c_str());
        return std::nullopt;
    }

    // How much of the map the crease actually moved, because "a file was
    // written" is not evidence that anything was baked into it.
    //
    // A file comparison cannot stand in for this: the sidecar is a RE-ENCODED
    // PNG, so it differs from the source byte-for-byte whatever its pixels say.
    // Measured -- a bake mutated to copy the base through unchanged still
    // passed a `files_differ` against the skin's own normal map.
    //
    // X AND Y ONLY, and z deliberately excluded. The crease is a tangent-space
    // SLOPE, so it lives in x and y; z moves for any base that is not exactly
    // unit length, purely from renormalizing. Counting z made this number
    // decorative too -- measured, the same copy-through mutation still reported
    // 1,048,534 of 1,048,576 "moved" because renormalizing had touched z on
    // almost every texel.
    size_t moved = 0;
    for (size_t i = 0; i + 1 < baked->size(); i += 4) {
        if ((*baked)[i] != base.constBits()[i] || (*baked)[i + 1] != base.constBits()[i + 1]) {
            ++moved;
        }
    }

    std::filesystem::path out = exportPath;
    out.replace_extension();
    out += "_normal.png";
    const QImage img(baked->data(), base.width(), base.height(), QImage::Format_RGBA8888);
    if (!img.save(QString::fromStdString(out.string()))) {
        std::fprintf(stderr, "cannot bake the wrinkle: cannot write %s\n", out.string().c_str());
        return std::nullopt;
    }
    std::printf("baked the wrinkle into %s at %.2f (%zu of %zu texels moved)\n",
                out.filename().string().c_str(), static_cast<double>(choice.weight), moved,
                baked->size() / 4);
    return out;
}

/// Applies @p rig's pose to @p mesh in place, and says why if it cannot.
///
/// The posing itself is `rig::poseMesh`; what is left here is the two settings
/// the user owns and the reporting, which is the part that must not live in a
/// library.
bool poseInPlace(mh::core::Mesh& mesh, PoseRig& rig) {
    const mh::rig::PoseOptions options{
        .method      = gUseDualQuaternion ? mh::rig::SkinningMethod::DualQuaternion
                                          : mh::rig::SkinningMethod::Linear,
        .apply       = gApplyPose,
        .correctives = gCorrectives ? &gCorrectives->runtime : nullptr};
    const auto ok = mh::rig::poseMesh(mesh, rig, options);
    if (ok) return true;

    switch (ok.error()) {
        case mh::rig::PoseError::RefitFailed:
            std::fprintf(stderr, "cannot re-fit the rig to the morphed mesh\n");
            break;
        case mh::rig::PoseError::SkinningFailed:
            std::fprintf(stderr, "skinning failed (%s)\n",
                         gUseDualQuaternion ? "dual quaternion" : "linear blend");
            break;
        case mh::rig::PoseError::StoreFailed:
            std::fprintf(stderr, "cannot store the posed mesh: it has a different vertex count\n");
            break;
        case mh::rig::PoseError::CorrectiveFailed:
            std::fprintf(stderr, "correctives could not be applied to this mesh or pose\n");
            break;
    }
    return false;
}

/// Files of one extension in @p dir, sorted.
///
/// directory_iterator order is unspecified, so without the sort the pickers
/// reshuffle between machines. The error is reported rather than swallowed: a
/// missing asset directory used to yield an empty group and, downstream, a
/// viewport that failed its upload every frame forever.
std::vector<std::filesystem::path> filesWithExtension(const std::filesystem::path& dir,
                                                      std::string_view extension) {
    std::error_code ec;
    std::vector<std::filesystem::path> found;
    // Recursive because proxies are shipped one directory per asset
    // (data/eyes/high-poly/high-poly.mhclo). litspheres/ and poses/ are flat,
    // so this changes nothing for them.
    for (const auto& entry : std::filesystem::recursive_directory_iterator(dir, ec)) {
        if (entry.path().extension() == extension) found.push_back(entry.path());
    }
    if (ec) {
        std::fprintf(stderr, "cannot read %s: %s\n", dir.string().c_str(), ec.message().c_str());
        return {};
    }
    std::sort(found.begin(), found.end());
    return found;
}

/// "skinmat_african.png" -> "African"; "tpose.bvh" -> "Tpose".
///
/// The assets are named by convention rather than carrying a label, so one is
/// derived from the filename.
/// The stem of @p file, made readable. Thin wrapper over the UI's own
/// `prettyAssetName` -- the picker labels and this must agree, and they did not
/// when this had its own copy: underscores went untouched and the first
/// generated skin material showed up as "African_rich".
std::string prettyName(const std::filesystem::path& file, std::string_view prefix) {
    return mh::ui::prettyAssetName(file.stem().string(), prefix).toStdString();
}

/// True when @p spelling names @p file -- the id itself, or any of the short
/// aliases loadPoseRig accepts.
bool namesPose(const std::filesystem::path& file, const std::string& spelling) {
    if (spelling == file.string()) return true;
    // loadPoseRig takes "tpose" and "t-pose"; matching only the first left the
    // picker reading "A-pose (rest)" over a T-posed model, which the obvious
    // click could not fix because the index was already 0.
    std::string wanted = spelling;
    std::erase(wanted, '-');
    std::string stem = file.stem().string();
    std::erase(stem, '-');
    return !wanted.empty() && wanted == stem;
}

constexpr const char* kDefaultSkin = "caucasian";
/// The "not wearing any" entry of a proxy chooser. A sentinel rather than an
/// empty string so it is visible in a save file and cannot be confused with
/// "unset".
constexpr const char* kNoProxy     = "none";
constexpr const char* kDefaultEyes = "high-poly";

/// Eyes get their own matcap; shading them with the body's skin makes them read
/// as flesh-coloured beads.
std::filesystem::path eyeLitsphere() {
    return dataDir() / "litspheres" / "skinmat_eye.png";
}

/// A helper-cage proxy slot: a chooser, a `--<key>` flag, a `.mhm` line and a
/// matcap, all named after one directory under `data/`.
///
/// This is a TABLE rather than a block of code per slot because the second slot
/// arrived and the first was about to be copied. Everything that differs
/// between teeth and tongue lives here; everything that does not is the loops
/// below. Eyes are deliberately NOT in the table: they carry a colour choice
/// and a material override, so they keep their own path.
struct ProxySlot {
    /// `data/<key>/`, the `.mhm` slot, and the spelling `--<key>` takes.
    const char* key;
    /// What the picker shows, and the group the window emits changes for.
    const char* group;
};

constexpr std::array<ProxySlot, 5> kProxySlots{{{"teeth", "Teeth"},
                                                {"tongue", "Tongue"},
                                                {"hair", "Hair"},
                                                {"clothes", "Clothes"},
                                                {"eyelashes", "Eyelashes"}}};

/// The slot's own matcap, generated by tools/make_helper_proxies.py from the
/// skin matcap's luminance so it is lit from the same direction as the face
/// around it.
///
/// It lives beside the proxy rather than in data/litspheres, which is what the
/// body-skin chooser enumerates.
std::filesystem::path slotLitsphere(std::string_view key) {
    return dataDir() / key / ("skinmat_" + std::string(key) + ".png");
}

/// A proxy the character is wearing: the fitting data, its own geometry, and
/// the render buffers for it.
///
/// The mesh is kept per proxy because fitProxy writes positions for the proxy's
/// own vertices -- it is a second mesh in the scene, not a deformation of the
/// body.
struct WornProxy {
    mh::core::Proxy proxy;
    mh::core::Mesh mesh;
    mh::core::RenderMesh rm;
    std::filesystem::path litsphere;
    /// From the proxy's own `.mhmat`. Absent if it names none or it failed to
    /// load; exporters then fall back to the scene default.
    std::optional<mh::foundation::MaterialDesc> material;
};

/// Defined below with the other material helpers; needed here so a worn eye can
/// pick up the chosen colour.
std::filesystem::path eyeMaterialPath();

/// Loads @p path and fits it to @p body once, so the caller gets something
/// renderable or nothing.
std::optional<WornProxy> wearProxy(const std::filesystem::path& path, const mh::core::Mesh& body,
                                   std::filesystem::path litsphere, bool isEyes = false) {
    auto proxy = mh::core::loadProxy(path);
    if (!proxy) {
        std::fprintf(stderr, "cannot load proxy %s: %s\n", path.string().c_str(),
                     proxy.error().message().c_str());
        return std::nullopt;
    }
    if (proxy->maxRefIndex() >= body.vertexCount()) {
        // A proxy cut for a different base mesh would otherwise read past the
        // end of the body's vertices inside fitProxy.
        std::fprintf(stderr, "proxy %s references body vertex %u of %zu\n", path.string().c_str(),
                     proxy->maxRefIndex(), body.vertexCount());
        return std::nullopt;
    }
    auto mesh = mh::core::loadObj(proxy->objFile);
    if (!mesh) {
        std::fprintf(stderr, "cannot load proxy geometry %s\n", proxy->objFile.string().c_str());
        return std::nullopt;
    }
    // The eye colour overrides whatever material the .mhclo names. Only for
    // the eyes: nothing else has a colour choice yet, and silently repointing
    // every proxy's material would be a much larger promise.
    if (isEyes && std::filesystem::exists(eyeMaterialPath())) {
        proxy->materialFile = eyeMaterialPath();
    }
    const std::string litsphereStem = litsphere.stem().string();
    WornProxy worn{std::move(*proxy), std::move(*mesh), {}, std::move(litsphere), std::nullopt};
    if (!worn.proxy.materialFile.empty()) {
        // A proxy exported with the body's skin looks like flesh-coloured
        // clothing in every DCC tool, so this is worth reporting rather than
        // silently defaulting.
        if (auto mat = mh::core::loadMaterial(worn.proxy.materialFile)) {
            worn.material = mat->desc();
        } else {
            std::fprintf(stderr, "cannot load proxy material %s: %s\n",
                         worn.proxy.materialFile.string().c_str(), mat.error().message().c_str());
        }
    }
    worn.mesh.buildAdjacency();
    worn.rm = mh::core::RenderMesh::build(worn.mesh);
    // Said out loud, in the same spirit as "applied N targets": it is the only
    // evidence from outside the process that a proxy reached the scene, and
    // app_screenshot asserts on it.
    //
    // The MATCAP is named too, and that is not decoration. A mutation that made
    // `slotLitsphere` hand every slot the teeth matcap changed the rendered
    // tongue from R-G 61.5 to 19.0 and broke NOTHING: the render test loads the
    // matcap paths itself, so it proves the asset is red without ever proving
    // the app reaches for it. This line is the only place the app's own choice
    // is observable from outside, so the per-slot tests assert on it. Appended
    // AFTER the "(N verts)" the older tests match, so they still do.
    std::fprintf(stderr, "wearing %s (%zu verts) lit by %s\n", worn.proxy.name.c_str(),
                 worn.proxy.vertexCount(), litsphereStem.c_str());
    return worn;
}

/// Re-fits @p worn to the body's CURRENT positions and refreshes its buffers.
///
/// Called on every rebuild, so a proxy follows both morphs and the pose. It
/// fits against the unsubdivided base mesh whatever the display mesh is: a
/// proxy's reference vertices are base-mesh indices.
/// Both early returns are unreachable by construction and leave the proxy at
/// its previous positions if that ever changes: `wearProxy` rejects a proxy
/// whose maxRefIndex exceeds the body, morphs never change the body's vertex
/// count, and a proxy's own vertex count is fixed at load. They are guards
/// against a future change, not an expected path.
void refitProxy(WornProxy& worn, const mh::core::Mesh& body) {
    std::vector<mh::foundation::Vec3> fitted;
    if (!mh::core::fitProxy(worn.proxy, body.coord(), fitted)) return;
    if (!worn.mesh.changeCoords(std::move(fitted))) return;
    worn.mesh.calcNormals();
    worn.mesh.calcVertexTangents();
    worn.rm.refreshPositions(worn.mesh);
}

/// What @p groups has selected in @p name, or empty if there is no such group
/// or nothing is selected.
std::string selectedChoice(std::span<const mh::foundation::AssetGroup> groups,
                           std::string_view name) {
    for (const auto& g : groups) {
        if (g.name == name && g.selected >= 0) {
            return g.choices[static_cast<size_t>(g.selected)].id;
        }
    }
    return {};
}

/// Skins and poses, from whatever is actually on disk.
///
/// Scanned rather than hard-coded so a litsphere or pose dropped into the data
/// directory appears without a code change -- and so this does not claim assets
/// that are not there.
/// Defined further down, next to the rest of the skin-material helpers; needed
/// here so the picker can list them.
std::vector<std::string> availableSkinMaterials();
std::vector<std::string> availableEyeColours();

/// dataDir(), NOT MH_DATA_DIR. The compile-time macro is an absolute path into
/// the tree that built the binary; a bundled copy has its own assets and
/// `resolveDataDir` has already found them. Three scans here used the macro and
/// therefore ignored the bundle -- caught by running the DMG's app with the
/// source `data/` renamed away, which failed on litspheres, poses and eyes
/// while everything routed through dataDir() kept working.
std::vector<mh::foundation::AssetGroup> buildAssetGroups(
    const std::string& currentPose, const std::string& currentSkin, const std::string& currentEyes,
    const std::string& currentMaterial, const std::string& currentRig,
    const std::string& currentEyeColour, const std::map<std::string, std::string>& currentProxies) {
    namespace fs = std::filesystem;
    std::vector<mh::foundation::AssetGroup> groups;

    mh::foundation::AssetGroup skins;
    // "Litsphere", not "Skin": this group lists viewport MATCAPS, and the
    // "Skin material" group below lists the textured .mhmat files. Two things
    // called Skin in one panel is the collision the --litsphere flag rename
    // already fixed one layer down.
    //
    // Name and key at once, which is safe because nothing persists it: `.mhm`
    // records the CHOICE (`litsphere african`) and the workspace records dock
    // object names, neither of which is this string. If that ever stops being
    // true this needs directive 12.1's treatment -- a canonical id with display
    // names beside it, the way `data/naming/workspace.names` already does for
    // workspace presets.
    skins.name      = "Litsphere";
    int defaultSkin = -1;
    for (const fs::path& p : filesWithExtension(dataDir() / "litspheres", ".png")) {
        if (p.stem().string().find("eye") != std::string::npos) continue;  // not a body skin
        skins.choices.push_back({p.string(), prettyName(p, "skinmat_")});
        const int index = static_cast<int>(skins.choices.size()) - 1;
        if (p.stem().string() == std::string("skinmat_") + currentSkin) skins.selected = index;
        if (p.stem().string() == std::string("skinmat_") + kDefaultSkin) defaultSkin = index;
    }
    if (skins.selected < 0 && !skins.choices.empty()) {
        // Fall back to the DOCUMENTED default rather than whatever sorts first,
        // and say so -- a typo used to silently render a different skin.
        std::fprintf(stderr, "unknown --skin \"%s\"; using %s\n", currentSkin.c_str(),
                     kDefaultSkin);
        skins.selected = defaultSkin >= 0 ? defaultSkin : 0;
    }
    groups.push_back(std::move(skins));

    mh::foundation::AssetGroup poses;
    poses.name = "Pose";
    // The rest mesh IS the A-pose, so it is a choice with no file behind it.
    poses.choices.push_back({"rest", "A-pose (rest)"});
    poses.selected = 0;
    for (const fs::path& p : filesWithExtension(dataDir() / "poses", ".bvh")) {
        poses.choices.push_back({p.string(), prettyName(p, "")});
        if (namesPose(p, currentPose)) {
            poses.selected = static_cast<int>(poses.choices.size()) - 1;
        }
    }
    groups.push_back(std::move(poses));

    // Eyes: the first proxy chooser. Only the two shipped eye proxies exist in
    // data/, so this is a real chooser over a small set rather than a stub.
    mh::foundation::AssetGroup eyes;
    eyes.name = "Eyes";
    eyes.choices.push_back({kNoProxy, "None"});
    eyes.selected = 0;
    for (const fs::path& p : filesWithExtension(dataDir() / "eyes", ".mhclo")) {
        eyes.choices.push_back({p.string(), prettyName(p, "")});
        if (p.stem().string() == currentEyes) {
            eyes.selected = static_cast<int>(eyes.choices.size()) - 1;
        }
    }
    if (eyes.selected == 0 && currentEyes != kNoProxy) {
        // Say so rather than silently rendering no eyes for a typo, the same
        // way an unknown --skin is reported.
        std::fprintf(stderr, "unknown --eyes \"%s\"; wearing none\n", currentEyes.c_str());
    }
    groups.push_back(std::move(eyes));

    // The helper-cage slots, under directive 13.5 ("if there are no assets or
    // data from legacy, procedurally generate and wire through"). Legacy ships
    // nothing for any of them; `tools/make_helper_proxies.py` cuts each from the
    // base mesh's OWN `helper-*` cage geometry, so the shape is the reference's
    // rather than invented.
    for (const ProxySlot& slot : kProxySlots) {
        mh::foundation::AssetGroup group;
        group.name = slot.group;
        group.choices.push_back({kNoProxy, "None"});
        group.selected     = 0;
        const auto current = currentProxies.find(slot.group);
        for (const fs::path& p : filesWithExtension(dataDir() / slot.key, ".mhclo")) {
            group.choices.push_back({p.string(), prettyName(p, "")});
            if (current != currentProxies.end() && p.stem().string() == current->second) {
                group.selected = static_cast<int>(group.choices.size()) - 1;
            }
        }
        groups.push_back(std::move(group));
    }

    // The eight generated skin MATERIALS, which until now had no picker at all:
    // `--skin-material` set them, the `.mhm` saved them and the exporters wrote
    // them, and the window could not choose one. That made four of the shipped
    // African tones unreachable to anyone not using the command line.
    //
    // NOT the same thing as the "Litsphere" group above, which lists viewport
    // matcaps with no PBR data. Both collisions are gone now: the flag is
    // `--litsphere` (answering to `--skin`) and the group is "Litsphere", so
    // nothing in the panel is called Skin except the materials.
    mh::foundation::AssetGroup materials;
    materials.name = "Skin material";
    for (const std::string& stem : availableSkinMaterials()) {
        materials.choices.push_back({stem, prettyName(fs::path(stem), "")});
        if (stem == currentMaterial) {
            materials.selected = static_cast<int>(materials.choices.size()) - 1;
        }
    }
    if (materials.selected < 0 && !materials.choices.empty()) {
        materials.selected = 0;  // availableSkinMaterials() puts `default` first
    }
    groups.push_back(std::move(materials));

    // The rig. Two ship -- the reference's 163-bone `default` and our 179-bone
    // `mixamo_superset` -- `--rig` has always chosen between them, and the
    // window could not. The `.mhm` already round-trips the choice
    // (`skeleton <name>.mhskel`, written and read), so this was the only piece
    // missing.
    // Independent of the Eyes group above: that picks the GEOMETRY, this picks
    // the material worn on it. Both have to survive a save, so both are saved.
    mh::foundation::AssetGroup eyeColours;
    eyeColours.name = "Eye colour";
    for (const std::string& stem : availableEyeColours()) {
        eyeColours.choices.push_back({stem, prettyName(fs::path(stem), "")});
        if (stem == currentEyeColour) {
            eyeColours.selected = static_cast<int>(eyeColours.choices.size()) - 1;
        }
    }
    if (eyeColours.selected < 0 && !eyeColours.choices.empty()) eyeColours.selected = 0;
    groups.push_back(std::move(eyeColours));

    mh::foundation::AssetGroup skeletons;
    skeletons.name = "Skeleton";
    for (const std::string& stem : rigStems()) {
        skeletons.choices.push_back({stem, prettyName(fs::path(stem), "")});
        if (stem == currentRig) {
            skeletons.selected = static_cast<int>(skeletons.choices.size()) - 1;
        }
    }
    if (skeletons.selected < 0 && !skeletons.choices.empty()) skeletons.selected = 0;
    groups.push_back(std::move(skeletons));
    return groups;
}

/// The document to write: @p base with its modifiers refreshed from @p human.
///
/// Starting from a fresh MhmFile instead loses uuid, tags, camera and every
/// plugin line the loader kept -- open a rigged, clothed character, press Save,
/// and it reopens naked and unrigged.
/// The document to write: @p base with its modifiers refreshed from @p human.
///
/// Starting from a fresh MhmFile instead loses uuid, tags, camera and every
/// plugin line the loader kept -- open a rigged, clothed character, press Save,
/// and it reopens naked and unrigged.
///
/// @param view the live viewport framing, or nothing when there is no window.
///        **Absent means leave the camera line exactly as loaded.** The format
///        holds doubles and this renderer's camera is float, so passing a view
///        through unconditionally rewrote `camera -13.399999999999999 ...` as
///        `-13.399999618530273` on every headless save, for a framing nobody
///        had touched.
/// The `.mhm` line a proxy chooser writes, and reads back.
///
/// Format from the reference's shared chooser
/// (`apps/gui/proxychooser.py:554-556`): `<slot> <name> <uuid>`, and the loader
/// resolves the **UUID** -- `:550-552` deliberately refuses a filename. The
/// name is carried for humans and for the warning when a UUID is missing.
constexpr const char* kEyesSaveName = "eyes";

/// The proxy path a `.mhm` line names, resolved by UUID.
///
/// `AssetIndex::findByUuid` existed for exactly this and had no caller: a
/// character saved wearing Low-Poly eyes reopened wearing High-Poly, and one
/// saved wearing NONE reopened wearing eyes, because the file recorded nothing
/// and the chooser fell back to its default. Silent substitution, not an error.
std::optional<std::string> proxyFromDocument(const mh::core::MhmFile& doc,
                                             const std::string& slot) {
    for (const std::string& line : doc.unhandled) {
        std::istringstream in(line);
        std::string key;
        std::string name;
        std::string uuid;
        if (!(in >> key) || key != slot) continue;
        // `<slot> <name> <uuid>`; a name may contain spaces, so the UUID is the
        // LAST token rather than the third.
        std::vector<std::string> rest;
        for (std::string tok; in >> tok;)
            rest.push_back(tok);
        if (rest.size() < 2) {
            // A two-token line is the OLD by-filename form. The reference
            // refuses it outright -- "Loading proxies from filename is no
            // longer supported, they need to be referenced by UUID"
            // (`proxychooser.py:550-552`) -- rather than guessing a path.
            std::fprintf(stderr, "%s: proxies are referenced by UUID, not by filename; ignoring\n",
                         line.c_str());
            return std::nullopt;
        }
        uuid = rest.back();

        // The slot IS the directory: `eyes` under data/eyes, `teeth` under
        // data/teeth. This used to be a hardcoded `data/eyes` whatever the
        // slot, so the teeth line round-tripped into "no teeth proxy with UUID
        // ...; using the default" -- the save was correct and the load threw it
        // away.
        const std::array<std::filesystem::path, 1> search{dataDir() / slot};
        const auto index = mh::core::AssetIndex::build(search);
        // The STEM, because that is what the chooser matches on -- the same
        // spelling `--eyes` takes.
        if (const auto* entry = index.findByUuid(uuid)) return entry->path.stem().string();

        std::fprintf(stderr, "%s: no %s proxy with UUID %s; using the default\n", line.c_str(),
                     slot.c_str(), uuid.c_str());
        return std::nullopt;
    }
    return std::nullopt;
}

/// The first value token of @p doc's `<key> ...` line, if it has one.
std::optional<std::string> valueFromDocument(const mh::core::MhmFile& doc, std::string_view key) {
    for (const std::string& line : doc.unhandled) {
        std::istringstream in(line);
        std::string k;
        std::string v;
        if ((in >> k) && k == key && (in >> v)) return v;
    }
    return std::nullopt;
}

/// Replaces @p doc's `<key> ...` line with `<key> <value>`, or removes it when
/// @p value is empty.
///
/// Replace, not append: a document loaded with one value and saved with another
/// would otherwise carry both lines, and every reader takes the first.
void recordLine(mh::core::MhmFile& doc, std::string_view key, const std::string& value) {
    std::erase_if(doc.unhandled, [key](const std::string& line) {
        std::istringstream in(line);
        std::string k;
        return (in >> k) && k == key;
    });
    if (!value.empty()) doc.unhandled.push_back(std::string(key) + " " + value);
}

/// Rewrites @p doc's proxy line for @p slot to what is worn now.
///
/// Replaces rather than appends: a document loaded with one selection and saved
/// with another would otherwise carry both lines, and the loader takes the
/// first.
void recordProxy(mh::core::MhmFile& doc, const std::string& slot, const std::string& name,
                 const std::string& uuid) {
    // Empty name: nothing worn, and the absence of a line IS the record.
    recordLine(doc, slot, name.empty() ? std::string{} : name + " " + uuid);
}

/// The model's half-extents, which is what the `.mhm` camera translation is
/// measured in (`lib/camera.py:544-546`).
///
/// Zero on any axis for a degenerate mesh, and callers must treat that as "no
/// pan expressible" rather than dividing by it.
mh::foundation::Vec3 halfExtents(const mh::core::Mesh& mesh) {
    if (mesh.coord().empty()) return {0.0F, 0.0F, 0.0F};
    mh::foundation::Vec3 lo = mesh.coord().front();
    mh::foundation::Vec3 hi = lo;
    for (const mh::foundation::Vec3& v : mesh.coord()) {
        lo = {std::min(lo.x, v.x), std::min(lo.y, v.y), std::min(lo.z, v.z)};
        hi = {std::max(hi.x, v.x), std::max(hi.y, v.y), std::max(hi.z, v.z)};
    }
    return {(hi.x - lo.x) * 0.5F, (hi.y - lo.y) * 0.5F, (hi.z - lo.z) * 0.5F};
}

/// `.mhm` pan (fractions of half-extent) -> camera pan (mesh units).
///
/// **Negated on purpose.** The reference pans by moving the camera's CENTRE
/// (`lib/camera.py:544`): pushing the centre +x aims further right, so the model
/// appears to move LEFT. Our pan translates the VIEW, which moves the model
/// +x on screen. Matching the reference's on-screen direction therefore means
/// flipping the sign.
///
/// Reasoned from `camera.py:544`, not measured against a running MakeHuman 1.x
/// -- the same standing as the zoom mapping, which is anchored on the defaults
/// and documented as "a sensible framing, not the identical one".
void applyPan(mh::render::Camera& c, const mh::core::OrbitView& view,
              const mh::foundation::Vec3& half) {
    c.panX = -view.translation[0] * half.x;
    c.panY = -view.translation[1] * half.y;
}

/// The inverse. A zero half-extent yields zero rather than a division by it.
std::array<float, 3> panToTranslation(const mh::render::Camera& c,
                                      const mh::foundation::Vec3& half) {
    return {half.x > 0.0F ? -c.panX / half.x : 0.0F, half.y > 0.0F ? -c.panY / half.y : 0.0F, 0.0F};
}

mh::core::MhmFile documentFor(const mh::core::Human& human, const mh::core::MhmFile& base,
                              const std::filesystem::path& path,
                              const std::optional<mh::core::OrbitView>& view, bool subdivided) {
    mh::core::MhmFile out = mh::core::mhmFromHuman(human, path.stem().string());
    out.writtenBy         = base.writtenBy;
    out.uuid              = base.uuid;
    out.tags              = base.tags;
    out.unhandled         = base.unhandled;
    out.subdivide         = subdivided;
    if (!base.name.empty()) out.name = base.name;

    if (!view) {
        out.camera    = base.camera;
        out.hasCamera = base.hasCamera;
        return out;
    }

    out.camera    = mh::core::mhmCameraFrom(*view);
    out.hasCamera = true;
    // Z is carried from the loaded file: the viewport pans in x and y only, so
    // writing a zero there would discard a depth offset the file legitimately
    // holds rather than record one we measured.
    if (base.hasCamera) out.camera[4] = base.camera[4];
    return out;
}

/// Writes the mesh in whichever format @p path's extension names.
/// Everything the VIEWPORT needs from a `.mhmat`, in one read.
///
/// `MaterialDesc` is what an exporter writes: it carries no `autoBlendSkin` and
/// no AO channel. Reading the material once for all four is also one file read
/// per rebuild instead of one per question -- this runs on every slider drag.
struct ViewportMaps {
    std::filesystem::path diffuse;
    std::filesystem::path normal;
    std::filesystem::path ao;
    bool autoBlendSkin{false};
    /// `.mhmat`'s own `transparent` flag, or an opacity below 1. Either means
    /// the mesh is drawn blended.
    bool transparent{false};
    float normalMapIntensity{1.0F};
    /// Read only by the PBR shading model. Derived by the SAME function the
    /// glTF and USD writers call, so the viewport and the exported file cannot
    /// disagree about the material they are both describing.
    float metallic{0.0F};
    float roughness{0.6F};
    /// `MaterialDesc::diffuse`, which the glTF writer emits as
    /// `baseColorFactor`. Read only by the PBR shading model.
    mh::foundation::Vec3 baseColour{1.0F, 1.0F, 1.0F};
    float opacity{1.0F};
};

ViewportMaps viewportMapsOf(const std::filesystem::path& mhmat) {
    ViewportMaps maps;
    const auto mat = mh::core::loadMaterial(mhmat);
    if (!mat) return maps;
    using mh::core::TextureChannel;
    const auto slot = [&mat](TextureChannel c) {
        return mat->textures[static_cast<size_t>(c)].path;
    };
    maps.diffuse       = slot(TextureChannel::Diffuse);
    maps.normal        = slot(TextureChannel::NormalMap);
    maps.ao            = slot(TextureChannel::AoMap);
    maps.autoBlendSkin = mat->autoBlendSkin;
    // Never assigned until now, so EVERY worn thing was drawn in the opaque
    // pass with blending disabled and its alpha discarded. The shipped eye is
    // what that costs: its front geometry is a cornea whose texel is
    // alpha 0 -- invisible when blended, an opaque pale disc when not, which is
    // the blank white oval the eyes rendered as. `MeshInstance::transparent`,
    // the blend pipeline and `MaterialDesc::transparent` all existed; nothing
    // connected them.
    //
    // Either condition blends, matching what the exporters already write:
    // `.mhmat`'s own flag, or an opacity below 1.
    maps.transparent = mat->desc().transparent || mat->desc().opacity < 1.0F;
    maps.normalMapIntensity =
        mat->textures[static_cast<size_t>(TextureChannel::NormalMap)].intensity;
    maps.baseColour = mat->desc().diffuse;
    maps.opacity    = mat->desc().opacity;
    const auto mr   = mh::foundation::metallicRoughnessOf(mat->desc());
    maps.metallic   = mr.metallic;
    maps.roughness  = mr.roughness;
    // That is every field of `MaterialDesc` the viewport can honour, and the
    // audit is deliberately closed here: four properties in a row turned out to
    // be built for export and never connected to the screen (metallic/roughness,
    // `transparent`, `diffuse`, `opacity`), because the writers were written
    // against `MaterialDesc` and the renderer against `MeshInstance`.
    //
    // The two that stop here stop on purpose, not by omission. `ambient` and
    // `specular` are exported (`SceneIO.cpp:882-883`, and `.mtl`'s Ka/Ks) but a
    // metallic-roughness BRDF has no input for either: dielectric F0 is fixed at
    // 0.04 without KHR_materials_specular, and the ambient term is what an IBL
    // replaces -- which this renderer deliberately does not have. Adding a slot
    // for them would be inventing shading the exported file does not describe.
    return maps;
}

/// The three ethnic litspheres as RGBA8, decoded once.
///
/// Decoding three PNGs on every slider drag would put image decode on the
/// interactive path; the blend itself is a pass over bytes and is cheap.
struct EthnicLitspheres {
    std::vector<uint8_t> caucasian;
    std::vector<uint8_t> african;
    std::vector<uint8_t> asian;
    int width{};
    int height{};
    bool ok{false};
};

const EthnicLitspheres& ethnicLitspheres() {
    static const EthnicLitspheres cached = [] {
        EthnicLitspheres e;
        const auto read = [&e](const char* stem, std::vector<uint8_t>& into) {
            QImage img(QString::fromStdString((dataDir() / "litspheres" / stem).string()));
            if (img.isNull()) return false;
            img = img.convertToFormat(QImage::Format_RGBA8888);
            if (e.width == 0) {
                e.width  = img.width();
                e.height = img.height();
            } else if (img.width() != e.width || img.height() != e.height) {
                return false;  // the blend is per byte; differing sizes cannot mix
            }
            const size_t n = static_cast<size_t>(img.sizeInBytes());
            into.assign(img.constBits(), img.constBits() + n);
            return true;
        };
        e.ok = read("skinmat_caucasian.png", e.caucasian) &&
               read("skinmat_african.png", e.african) && read("skinmat_asian.png", e.asian);
        if (!e.ok) {
            std::fprintf(stderr,
                         "cannot decode the three ethnic litspheres; skin tone will not blend\n");
        }
        return e;
    }();
    return cached;
}

/// Blends the ethnic litspheres for @p human into @p out.
///
/// @return the image size, or nothing when blending is unavailable -- the
///         caller then keeps the plain litsphere path, which is what happened
///         before this existed.
std::optional<std::pair<int, int>> blendedSkinTone(const mh::core::Human& human,
                                                   std::vector<uint8_t>& out) {
    const EthnicLitspheres& e = ethnicLitspheres();
    if (!e.ok) return std::nullopt;

    // From `factors()`, NOT modifierValue(): the raw sliders are not
    // renormalised. Setting Caucasian to 1.0 leaves the other two at 1/3 each,
    // so reading them directly gives weights summing to 1.667 and blends a
    // "pure" caucasian skin as 1.0/0.33/0.33 of all three. The reference reads
    // the renormalised values (`human.getCaucasian()`), and so does this.
    const mh::core::EthnicWeights w{human.factors().caucasian(), human.factors().african(),
                                    human.factors().asian()};
    const std::array<std::span<const uint8_t>, 3> images{e.caucasian, e.african, e.asian};
    auto blended = mh::core::blendEthnicLitsphere(images, w);
    if (!blended) return std::nullopt;
    out = std::move(*blended);
    return std::pair{e.width, e.height};
}

/// The short name a litsphere PATH carries, e.g. `african` for
/// `.../litspheres/skinmat_african.png`.
///
/// Derived rather than stored. The chosen litsphere already lives in main()'s
/// `skin` path and the picker writes it there; a second variable holding the
/// stem would be a copy to keep in sync, and the two would disagree the first
/// time one of them was missed.
std::string litsphereName(const std::filesystem::path& litsphere) {
    const std::string stem             = litsphere.stem().string();
    constexpr std::string_view kPrefix = "skinmat_";
    return stem.starts_with(kPrefix) ? stem.substr(kPrefix.size()) : stem;
}

/// The body's own material, for export. Under the default litsphere shading the
/// viewport shows none of this -- a matcap has no material response -- which is
/// what `--shading pbr` exists to fix; either way a `.mhmat` is what a DCC tool
/// gets.
/// The chosen skin material's stem under `data/skins`, e.g. `african_deep`.
///
/// Not a litsphere. `--litsphere` picks a viewport MATCAP (and answers to
/// `--skin`, its old name); this picks the material that is textured, shaded
/// and exported. The two were uncomfortably close until the rename, and this
/// one stays spelled out in full rather than competing for the short name.
std::string& skinMaterialRef() {
    static std::string name{"default"};
    return name;
}

/// The chosen eye colour's stem under `data/eyes/materials`, e.g. `green`.
///
/// Separate from the Eyes group, which picks the PROXY (high- or low-poly
/// geometry). Colour is the material worn on whichever proxy that is, so the
/// two are independent choices and both have to survive a save.
std::string& eyeColourRef() {
    static std::string name{"brown"};
    return name;
}

std::filesystem::path eyeMaterialPath() {
    return dataDir() / "eyes" / "materials" / (eyeColourRef() + ".mhmat");
}

/// The eye colours on disk, by stem, sorted. `brown` first: it is the shipped
/// original and the one every other colour is a recolour of.
std::vector<std::string> availableEyeColours() {
    std::vector<std::string> out;
    for (const auto& p : filesWithExtension(dataDir() / "eyes" / "materials", ".mhmat"))
        out.push_back(p.stem().string());
    std::ranges::sort(out);
    if (const auto it = std::ranges::find(out, "brown"); it != out.end()) {
        std::rotate(out.begin(), it, it + 1);
    }
    return out;
}

std::filesystem::path skinMaterialPath() {
    return dataDir() / "skins" / (skinMaterialRef() + ".mhmat");
}

std::optional<mh::foundation::MaterialDesc> bodyMaterial() {
    const auto path = skinMaterialPath();
    if (auto mat = mh::core::loadMaterial(path)) return mat->desc();
    std::fprintf(stderr, "cannot load %s; exporting without a body material\n",
                 path.string().c_str());
    return std::nullopt;
}

/// The skin materials on disk, by stem, sorted. `default` first: it is the
/// untextured litsphere-shaded original and the way back from a textured one.
std::vector<std::string> availableSkinMaterials() {
    std::vector<std::string> out;
    for (const auto& p : filesWithExtension(dataDir() / "skins", ".mhmat"))
        out.push_back(p.stem().string());
    std::ranges::sort(out);
    if (const auto it = std::ranges::find(out, "default"); it != out.end()) {
        std::rotate(out.begin(), it, it + 1);
    }
    return out;
}

/// @param worn proxies to include. OBJ writes them as extra groups; the other
///        formats are still single-mesh, so they say what they are leaving out
///        rather than quietly exporting a dressed character naked.
/// Prints what our own importer sees in @p path.
///
/// `io::importScene` is five sessions of work -- multi-mesh, node transforms,
/// materials, skins, and a unit contract -- and until now **nothing in the
/// application called it**. Its only consumers were tests, so every question of
/// the form "what does our reader actually see in this file?" was answered with
/// a throwaway probe.
///
/// It reports what the file SAYS, not a guess: `metersPerUnit` is 0 for a
/// genuinely unitless format, and the real-world size is printed only when the
/// file is in a position to state one.
bool inspectFile(const std::filesystem::path& path) {
    const auto scene = mh::io::importScene(path);
    if (!scene) {
        std::fprintf(stderr, "cannot read %s: %s\n", path.string().c_str(),
                     scene.error().message().c_str());
        return false;
    }

    size_t vertices  = 0;
    size_t triangles = 0;
    for (const auto& m : scene->meshes) {
        vertices += m.mesh.vertexCount();
        triangles += m.mesh.faceCount();
    }
    std::printf("%s: %zu meshes, %zu vertices, %zu triangles\n", path.string().c_str(),
                scene->meshes.size(), vertices, triangles);

    if (scene->metersPerUnit > 0.0) {
        float lo = std::numeric_limits<float>::infinity();
        float hi = -lo;
        for (const auto& m : scene->meshes) {
            for (const auto& v : m.mesh.coord) {
                lo = std::min(lo, v.y);
                hi = std::max(hi, v.y);
            }
        }
        std::printf("  units: 1 = %.4g m, so %.4f m tall\n", scene->metersPerUnit,
                    static_cast<double>(hi - lo) * scene->metersPerUnit);
    } else {
        std::printf("  units: the format does not say\n");
    }

    for (const auto& m : scene->meshes) {
        std::printf("  mesh \"%s\": %zu verts, %zu tris, %s UVs", m.name.c_str(),
                    m.mesh.vertexCount(), m.mesh.faceCount(), m.mesh.texco.empty() ? "no" : "with");
        if (m.material) {
            std::printf(", material \"%s\"%s", m.material->name.c_str(),
                        m.material->transparent ? " (transparent)" : "");
        }
        if (m.skin) {
            std::printf(", skin of %zu bones", m.skin->bones.size());
        }
        std::printf("\n");
    }
    return true;
}

/// A skin for everything worn, derived from the body's.
///
/// A live-rig export ships REST geometry and lets the consumer pose it, so an
/// entry with no skin simply stays where it was while the body moves. Measured
/// 2026-09-07 in Maya, before this existed: the body deformed and the eyes did
/// not, so they protruded from their sockets.
///
/// A proxy vertex IS a weighted blend of three base vertices, so its weights
/// are that same blend -- `rig::proxyWeights`, which is where the reasoning and
/// its tests live. Parallel to @p worn, which is an ordered map, so entry `i`
/// here is the `i`-th entry a writer loop appends.
std::vector<mh::rig::SkinData> wornSkins(const PoseRig& rig,
                                         const std::map<QString, WornProxy>& worn) {
    std::vector<mh::rig::SkinData> skins;
    skins.reserve(worn.size());
    for (const auto& [group, proxy] : worn) {
        const std::string name = group.toLower().toStdString();
        const mh::rig::CompiledWeights w =
            mh::rig::proxyWeights(rig.weights, proxy.proxy.refVerts, proxy.proxy.weights);
        mh::rig::SkinData skin = mh::rig::buildSkinData(rig.skeleton, w, proxy.rm.vmap());
        if (skin.jointNames.empty()) {
            std::fprintf(stderr, "%s: no skin could be derived; it will not follow the pose\n",
                         name.c_str());
        } else {
            if (rig.posed()) skin.globalPose = rig.globalPose;
            std::printf("%s skin: %zu joints, %u influences/vertex\n", name.c_str(),
                        skin.globalRest.size(), static_cast<unsigned>(skin.influences));
        }
        skins.push_back(std::move(skin));
    }
    return skins;
}

/// Lower-cased extension of @p path, so every format test spells it one way.
std::string lowerExtension(const std::filesystem::path& path) {
    std::string ext = path.extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return ext;
}

/// Whether a format is **verified** to carry a live rig.
///
/// Only these get rest geometry with a posed armature. Everything else keeps
/// the baked posed mesh, which is the safe answer: a format that cannot apply
/// the pose would otherwise export a character standing in a pose nobody asked
/// for.
///
/// **Measured in Blender, 2026-09-05, not assumed from the format's spec:**
///   * `.glb`  — rest 1.0516 m wide, evaluated **1.6863** — matches our own
///     baked answer (16.8628 dm) exactly. The rig deforms.
///   * `.usda` — same, 1.6863. `usdchecker` clean.
///   * `.fbx`  — **used to fail, and no longer does.** assimp's writer emitted
///     no bind pose, so its own SDK log said "The imported scene has no initial
///     binding position (Bind Pose) for the skin. The plug-in will compute one
///     automatically" -- Maya then took the current pose as the bind pose and
///     the file was a statue. `io::writeFbxScene` writes the bind pose, and
///     **Maya reports it as `live_rig: true`** (rest 169.455, deformed 249.455
///     on the harness fixture). So .fbx joined this list the moment the writer
///     changed.
///   * `.dae`  — **unverified**: Blender 5.2 removed its Collada importer, so
///     there is no third party here to check it with. Still assimp's writer, so
///     it is excluded rather than assumed to work.
///
/// **Getting this list wrong is not a missing feature, it is a double
/// transform.** A format in the list gets REST geometry and a posed armature; a
/// format out of it gets BAKED geometry. Leave a format out while its writer
/// also writes the pose and the consumer applies the deformation twice --
/// measured, when .fbx was switched to our writer before this line was updated:
/// Maya evaluated the body to 440 x 328 x 267 cm.
bool formatCarriesRig(std::string_view ext) {
    return ext == ".glb" || ext == ".usd" || ext == ".usda" || ext == ".usdz" || ext == ".fbx";
}

/// The body's skin for export, or nothing when there is no rig to export.
///
/// The application built a complete rig -- loaded the skeleton, fitted the
/// joints to the morphed body, compiled the weights and posed with them -- and
/// then handed none of it to a writer. Every export was a statue.
///
/// **A live rig**: `globalRest` is the bind pose and `globalPose` carries where
/// the joints actually sit, so a rigged export ships REST geometry with a POSED
/// armature and the consumer computes the deformation itself.
///
/// It used to bake instead -- the bind pose was set to the pose, so the mesh
/// arrived exactly as it looked here and every DCC's skinning was a no-op. That
/// was self-consistent, and it made our LBS unverifiable by anyone else. Owner
/// decision, 2026-09-05.
///
/// Formats with no skeleton (OBJ, STL, 3MF) still get the baked posed mesh:
/// there is nothing in the file to apply a pose with.
/// @param refuseBecause null to build the skin, or the word for WHY this mesh
///        cannot carry one -- "subdivided", "decimated". Taking the REASON
///        rather than a bool keeps the refusal and its announcement in one
///        place. They were two places for one chunk, and the mutation that
///        removed the refusal while leaving the message survived: the export
///        said it was dropping the skeleton and wrote it anyway.
/// @param vmap render vertex -> BASE mesh vertex, or empty to use @p rm's own.
///        A decimated mesh needs an explicit one: its render vmap points at ITS
///        vertices, and the weights are compiled against the base mesh, so the
///        caller composes the chain and hands the answer over.
std::optional<mh::rig::SkinData> exportSkin(const PoseRig& rig, const mh::core::RenderMesh& rm,
                                            const char* refuseBecause,
                                            std::span<const uint32_t> vmap) {
    if (!rig.loaded()) return std::nullopt;
    if (refuseBecause != nullptr) {
        // Weights are per BASE vertex. A subdivided render mesh's vmap indexes
        // subdivided vertices, and a decimated one has had every vertex
        // renumbered by the collapses, so buildSkinData would silently weight
        // the wrong points either way.
        std::fprintf(stderr, "a %s mesh cannot carry the rig; exporting without a skeleton\n",
                     refuseBecause);
        return std::nullopt;
    }

    mh::rig::SkinData skin =
        mh::rig::buildSkinData(rig.skeleton, rig.weights, vmap.empty() ? rm.vmap() : vmap);
    if (skin.jointNames.empty()) {
        std::fprintf(stderr,
                     "cannot expand the weights onto the render vertices; "
                     "exporting without a skeleton\n");
        return std::nullopt;
    }
    // `globalRest` stays the BIND pose; the posed globals ride alongside it.
    // Overwriting globalRest here -- which this did until 2026-09-05 -- made the
    // bind pose equal the pose, so the mesh arrived exactly as it looked on
    // screen and every consumer's skinning was a no-op.
    if (rig.posed()) skin.globalPose = rig.globalPose;
    std::printf("skin: %zu joints, %u influences/vertex\n", skin.globalRest.size(),
                static_cast<unsigned>(skin.influences));
    return skin;
}

/// @param rig the loaded skeleton and pose, for the formats that carry a
///        SKELETON rather than a mesh. Only BVH uses it.
/// @param bodyMask which body faces to write. Every format except OBJ takes its
///        geometry from @p rm, which already has the mask applied; OBJ writes
///        from the Mesh directly and so needs it handed over. Without this the
///        OBJ was the one export carrying the helper cages -- 18,486 faces
///        against the GLB's 13,378.
/// @param skin the body's skeleton and weights, or null. **Only glTF carries it
///        today**: `GltfSceneEntry` has a skin field and the assimp and USD
///        scene entries do not, so the other formats say what they are dropping
///        rather than writing a statue in silence.
/// @param body the body's render geometry, already compacted. Every format
///        except OBJ writes from this.
/// @param morphs blendshapes for the body. **Only GLB carries them**:
///        `GltfSceneEntry` has a morphTargets field and `io::SceneEntry` does
///        not, so the assimp and USD paths say what they are dropping rather
///        than writing an expressionless mesh in silence.
/// @param provenance what the file says about itself: the product version, the
///        content-format version and the BASE topology hash. Built once by the
///        caller and handed to all four writers, so the four formats cannot
///        drift into four spellings of the same three facts.
bool exportMesh(const std::filesystem::path& path, const mh::core::Mesh& mesh,
                const mh::foundation::RenderView& body, const std::map<QString, WornProxy>& worn,
                std::span<const uint8_t> bodyMask, const mh::foundation::SkinView* skin,
                const PoseRig& rig, const mh::foundation::Provenance& provenance,
                std::span<const mh::foundation::MorphTarget> morphs = {}, bool draco = false) {
    const std::string ext = lowerExtension(path);

    // OBJ is the only format left with no blendshape channel -- its format
    // simply has none. glTF, the assimp formats and UsdSkel all carry them.
    static constexpr std::array kMorphCapable{".glb", ".fbx", ".dae", ".usd", ".usda", ".usdz"};
    if (!morphs.empty() && std::ranges::find(kMorphCapable, ext) == kMorphCapable.end()) {
        std::fprintf(stderr,
                     "%s carries no blendshapes; writing %zu expression targets needs .glb, .fbx, "
                     ".dae or .usd\n",
                     ext.c_str(), morphs.size());
    }

    const auto report = [&](const std::string& err) {
        if (err.empty()) {
            std::printf("wrote %s\n", path.string().c_str());
            return true;
        }
        std::fprintf(stderr, "export failed: %s\n", err.c_str());
        return false;
    };

    // Both writers refuse a scene where only some entries carry a material --
    // OBJ's `usemtl` is sticky, so a material-less entry would silently inherit
    // the previous one. So it is all or nothing, and if it is nothing the user
    // hears about it: the body had a perfectly good material and lost it
    // because something it was wearing did not.
    auto bodyMat = bodyMaterial();
    // The exported material points at the BAKE, not at the skin's own normal
    // map. Overridden here rather than inside `bodyMaterial()` because the bake
    // is named after the export and there is no export in that function.
    if (bodyMat.has_value()) {
        if (const auto baked = bakeWrinkleBeside(path, *bodyMat)) bodyMat->normalTexture = *baked;
    }
    const bool allDressed = bodyMat.has_value() && std::ranges::all_of(worn, [](const auto& kv) {
                                return kv.second.material.has_value();
                            });
    if (!allDressed && (bodyMat.has_value() || !worn.empty())) {
        std::fprintf(stderr, "exporting without materials: %s\n",
                     bodyMat.has_value() ? "something worn has none, and a partly-materialled "
                                           "scene cannot be written"
                                         : "the body skin could not be loaded");
    }

    // Feet on the ground, in every format. The base mesh's origin is at hip
    // height -- measured: feet at -0.82 m and head at +0.84 m -- so an export
    // without this arrives in a DCC tool buried to the waist. The reference
    // defaults it on for every one of its exporters
    // (legacy/python/core/export.py:58) and this matches that.
    //
    // Each writer takes its own options struct; there is no shared one, which
    // is why this is four assignments rather than one.
    mh::io::ObjWriteOptions objOpts;
    objOpts.feetOnGround = true;
    objOpts.provenance   = provenance;
    mh::io::GltfWriteOptions gltfOpts;
    gltfOpts.feetOnGround = true;
    gltfOpts.draco        = draco;
    gltfOpts.provenance   = provenance;
    mh::io::UsdWriteOptions usdOpts;
    usdOpts.feetOnGround = true;
    usdOpts.provenance   = provenance;
    mh::io::SceneExportOptions sceneOpts;
    sceneOpts.feetOnGround = true;

    // Everything worn rides the SAME skeleton, and both live-rig writers now
    // emit the joints once and let each entry reference them. Without a skin a
    // proxy simply stays where it was while the body moves -- measured, the
    // eyes protruded from their sockets in both formats.
    //
    // Computed once for the three formats that can carry it: OBJ, STL and 3MF
    // get the baked posed mesh, and the assimp scene entries have no skin field
    // at all.
    const bool proxiesCanFollow =
        skin != nullptr &&
        (ext == ".glb" || ext == ".fbx" || ext == ".usd" || ext == ".usda" || ext == ".usdz");
    const std::vector<mh::rig::SkinData> proxySkins =
        proxiesCanFollow ? wornSkins(rig, worn) : std::vector<mh::rig::SkinData>{};
    std::vector<mh::foundation::SkinView> proxyViews;
    proxyViews.reserve(proxySkins.size());
    for (const mh::rig::SkinData& s : proxySkins)
        proxyViews.push_back(s.view());
    // The i-th worn proxy's skin, or null when it has none.
    const auto proxySkin = [&proxyViews](size_t i) -> const mh::foundation::SkinView* {
        return i < proxyViews.size() && proxyViews[i].valid() ? &proxyViews[i] : nullptr;
    };

    // OBJ is the only format left that cannot carry a skeleton -- it has no
    // concept of one. Said here rather than per format, because silence is how
    // the rig went missing from every export for four milestones.
    if (skin != nullptr && ext == ".obj") {
        std::fprintf(stderr,
                     "obj carries no skeleton; export .glb, .fbx, .dae or .usda for a "
                     "rigged character\n");
    }

    // BVH is the odd one out: it carries a SKELETON and a pose, not geometry, so
    // it neither takes the mesh nor cares about the face mask.
    if (ext == ".bvh") {
        if (!rig.loaded()) {
            std::fprintf(stderr, "no rig loaded, so there is no skeleton to write\n");
            return false;
        }
        const auto file = mh::rig::toBvhPose(rig.skeleton, rig.localPose);
        if (file.joints.empty()) {
            std::fprintf(stderr, "cannot build a BVH from this skeleton\n");
            return false;
        }
        std::printf("bvh: %zu joints, 1 frame\n", file.joints.size());
        const auto r = mh::io::writeBvh(path, file);
        return report(r ? std::string{} : r.error().message());
    }

    if (ext == ".obj") {
        std::vector<mh::io::ObjSceneEntry> scene;
        scene.push_back({mesh.view(), "body", allDressed ? &*bodyMat : nullptr, bodyMask});
        for (const auto& [group, proxy] : worn) {
            scene.push_back({proxy.mesh.view(),
                             group.toLower().toStdString(),
                             allDressed ? &*proxy.material : nullptr,
                             {}});
        }
        const auto r = mh::io::writeObjScene(path, scene, objOpts);
        return report(r ? std::string{} : r.error().message());
    }

    // Every assimp-backed format takes the whole scene now, so the body and
    // everything worn travel together.
    const auto sceneEntries = [&] {
        std::vector<mh::io::SceneEntry> scene;
        // Only the body is rigged and only the body has expressions; exactly
        // one entry may carry each.
        scene.push_back({body, "body", allDressed ? &*bodyMat : nullptr, skin, morphs});
        for (const auto& [group, proxy] : worn) {
            scene.push_back({proxy.rm.view(), group.toLower().toStdString(),
                             allDressed ? &*proxy.material : nullptr, nullptr});
        }
        return scene;
    };

    // Every writer carries the whole scene now -- obj, glb, fbx, dae, stl, 3mf
    // and usda alike -- so nothing is silently dropped and there is no longer a
    // note to print. This is where the "exports the body only" warning lived.
    // .usdz was implemented in the writer and unreachable from here: the
    // packaging is validated by Apple's own usdchecker --arkit, and it is the
    // format an AR or Apple pipeline actually takes.
    if (ext == ".usda" || ext == ".usd" || ext == ".usdz") {
        std::vector<mh::io::UsdSceneEntry> scene;
        scene.push_back({body, "body", allDressed ? &*bodyMat : nullptr, skin, morphs});
        size_t at = 0;
        for (const auto& [group, proxy] : worn) {
            scene.push_back({proxy.rm.view(), group.toLower().toStdString(),
                             allDressed ? &*proxy.material : nullptr, proxySkin(at)});
            ++at;
        }
        const auto r = ext == ".usdz" ? mh::io::writeUsdzScene(path, scene, usdOpts)
                                      : mh::io::writeUsdaScene(path, scene, usdOpts);
        return report(r ? std::string{} : r.error().message());
    }
    if (ext == ".glb") {
        std::vector<mh::io::GltfSceneEntry> scene;
        scene.push_back({body, "body", allDressed ? &*bodyMat : nullptr, skin, morphs});
        size_t at = 0;
        for (const auto& [group, proxy] : worn) {
            scene.push_back({proxy.rm.view(), group.toLower().toStdString(),
                             allDressed ? &*proxy.material : nullptr, proxySkin(at)});
            ++at;
        }
        const auto r = mh::io::writeGlbScene(path, scene, gltfOpts);
        return report(r ? std::string{} : r.error().message());
    }

    // PLY is deliberately absent -- assimp 6.0.4 writes corrupt faces for any
    // mesh with UVs, and every mesh here has them (SceneIO.h).
    if (ext == ".fbx") {
        // OUR writer, not assimp's. The reason is the LIVE RIG: assimp's FBX
        // writer emits no bind pose, so its own SDK log says "The imported
        // scene has no initial binding position (Bind Pose) for the skin. The
        // plug-in will compute one automatically" -- Maya then takes the
        // current pose as the bind pose and the character arrives baked. Maya
        // reports ours as `live_rig: true` and assimp's as false, side by side,
        // in tools/run_maya_validation.sh.
        //
        // Everything the assimp path carried is carried here: geometry,
        // normals, UVs, materials, textures, the skin and the blend shapes.
        //
        // Everything worn is skinned too, to the SAME skeleton -- the writer
        // emits the joints once and each entry's clusters point at them.
        std::vector<mh::io::FbxSceneEntry> scene;
        scene.push_back({body, "body", allDressed ? &*bodyMat : nullptr, skin, morphs});
        size_t at = 0;
        for (const auto& [group, proxy] : worn) {
            scene.push_back({proxy.rm.view(), group.toLower().toStdString(),
                             allDressed ? &*proxy.material : nullptr, proxySkin(at)});
            ++at;
        }
        mh::io::FbxWriteOptions fbxOpts;
        fbxOpts.feetOnGround = true;
        fbxOpts.provenance   = provenance;
        const auto r         = mh::io::writeFbxScene(path, scene, fbxOpts);
        return report(r ? std::string{} : r.error().message());
    }
    if (ext == ".dae") {
        const auto r =
            mh::io::exportScene(path, sceneEntries(), mh::io::SceneFormat::Collada, sceneOpts);
        return report(r ? std::string{} : r.error().message());
    }
    if (ext == ".stl") {
        const auto r =
            mh::io::exportScene(path, sceneEntries(), mh::io::SceneFormat::StlBinary, sceneOpts);
        return report(r ? std::string{} : r.error().message());
    }
    if (ext == ".3mf") {
        const auto r =
            mh::io::exportScene(path, sceneEntries(), mh::io::SceneFormat::ThreeMf, sceneOpts);
        return report(r ? std::string{} : r.error().message());
    }
    std::fprintf(stderr, "unknown export extension \"%s\"\n", ext.c_str());
    return false;
}

}  // namespace

int main(int argc, char** argv) {
    QApplication app(argc, argv);

    // Where the assets are is a RUNTIME question. MH_DATA_DIR is an absolute
    // path into whichever source tree compiled this binary, so an installed or
    // bundled copy has no assets at all unless it looks elsewhere first.
    setDataRoot(mh::foundation::resolveDataDir(
        QCoreApplication::applicationFilePath().toStdString(), MH_DATA_DIR));
    // The tree is only CHECKED further down, after the command line is parsed:
    // --version, --help and --inspect are answers about the binary or about a
    // file they are given, and refusing them for want of assets is the one
    // answer a bug report cannot use.
    QCoreApplication::setOrganizationName(QStringLiteral("MakeHuman"));
    QCoreApplication::setApplicationName(QStringLiteral("MakeHumanCpp"));
    // From /VERSION, through the generated header. `--version` did not work at
    // all before this: the version was compiled into mh_core as a PRIVATE
    // macro, so it was in the binary and unreachable from here.
    QCoreApplication::setApplicationVersion(QString::fromUtf8(
        mh::foundation::kVersion.data(), static_cast<qsizetype>(mh::foundation::kVersion.size())));

    // Resources before any widget exists: a stylesheet applied after the fact
    // repolishes every widget, and a font registered late is not the one the
    // first layout was measured with.
    // Same runtime question as the assets, and the compile-time answer is even
    // worse here: MH_RESOURCE_DIR points into the SOURCE tree, MH_SHADER_DIR
    // into the BUILD tree. A copied `.app` finds neither.
    const std::filesystem::path resources = mh::foundation::resolveResourceDir(
        QCoreApplication::applicationFilePath().toStdString(), MH_RESOURCE_DIR);
    mh::ui::theme::setIconDir(resources / "icons" / "lucide");

    // The Dock and window icon. A BUNDLED app takes its Dock icon from
    // Info.plist's CFBundleIconFile instead, but a bare build-tree run has no
    // Info.plist -- and that is the binary a developer actually looks at all
    // day. Setting it here covers both: harmless when the bundle already
    // supplied one, and the only source when it did not.
    const std::filesystem::path appIcon = resources / "branding" / "AppIcon-1024.png";
    if (std::filesystem::exists(appIcon)) {
        QApplication::setWindowIcon(QIcon(QString::fromStdString(appIcon.string())));
    }
    const QString family = mh::ui::theme::installFonts(resources / "fonts");
    if (!family.isEmpty()) {
        QApplication::setFont(QFont(family, 13));
    }
    app.setStyleSheet(mh::ui::theme::styleSheet());

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("MakeHuman (C++/Qt6)"));
    parser.addHelpOption();
    // Qt prints "<appName> <appVersion>" and exits. Without this, `--version`
    // was reported as an unknown option.
    parser.addVersionOption();
    const std::filesystem::path shaderDir = mh::foundation::resolveShaderDir(
        QCoreApplication::applicationFilePath().toStdString(), MH_SHADER_DIR);
    const QCommandLineOption shaderOpt(
        QStringLiteral("shaders"), QStringLiteral("Directory holding the compiled .qsb files"),
        QStringLiteral("dir"), QString::fromStdString(shaderDir.string()));
    const QCommandLineOption shotOpt(
        QStringLiteral("screenshot"),
        QStringLiteral("Render one frame to this PNG and exit -- how the window is checked "
                       "without a human looking at it"),
        QStringLiteral("path"));
    const QCommandLineOption expressionOpt(
        QStringLiteral("expression"),
        QStringLiteral("A .mhpose expression file: named face pose units with weights. Layered "
                       "onto whatever --pose gives, so the two are independent."),
        QStringLiteral("file"));
    const QCommandLineOption facsOpt(
        QStringLiteral("facs"),
        QStringLiteral("A FACS Action Unit and its intensity, as <AU>=<0..1>. Repeatable, and "
                       "applied in the order given. AU12 is both mouth corners; AU12L and "
                       "AU12R are one side each. Layered onto --pose like --expression."),
        QStringLiteral("AU=weight"));
    const QCommandLineOption saveExpressionOpt(
        QStringLiteral("save-expression"),
        QStringLiteral("Write the --facs request to this .mhpose, so an expression composed "
                       "from Action Units can be kept, shared and re-loaded with --expression. "
                       "This is what the reference's expression mixer Save button does."),
        QStringLiteral("file"));
    const QCommandLineOption poseOpt(
        QStringLiteral("pose"),
        QStringLiteral("rest (the authored A-pose, default), tpose, or a path to a "
                       "single-frame .bvh"),
        QStringLiteral("pose"), QStringLiteral("rest"));
    const QCommandLineOption poseUnitOpt(
        QStringLiteral("pose-unit"),
        QStringLiteral("A face pose unit and its weight, as <unit>=<0..1>. Repeatable, and "
                       "applied in the order given. This is the expression mixer's sixty "
                       "sliders: --facs reaches only the thirty Action Units that name "
                       "them. --list-pose-units prints the names."),
        QStringLiteral("unit=weight"));
    const QCommandLineOption listPoseUnitsOpt(
        QStringLiteral("list-pose-units"),
        QStringLiteral("Print the face pose units --pose-unit accepts, and exit."));
    const QCommandLineOption spreadRootsOpt(
        QStringLiteral("spread-roots"),
        QStringLiteral("Print <n> hair-root vertices spread evenly over the scalp of the "
                       "base mesh, as \"index x y z\", and exit. The style generator "
                       "consumes these: roots are WALKED over the surface rather than "
                       "raycast at it, which is what the 2026-09-11 attempt got wrong. "
                       "Asking for more than the cap holds prints the whole cap."),
        QStringLiteral("n"));
    const QCommandLineOption poseFrameOpt(
        QStringLiteral("pose-frame"),
        QStringLiteral("Which frame of a multi-frame --pose .bvh to stand in, zero-based. "
                       "Without it a multi-frame file is refused, because frame 0 of an "
                       "animation is a plausible wrong pose rather than an error."),
        QStringLiteral("n"));
    const QCommandLineOption backgroundOpt(
        QStringLiteral("background"),
        QStringLiteral("An image to put BEHIND --render's character, scaled to cover the "
                       "frame and centred. Implies --transparent, since the character has "
                       "to have alpha for anything to show through."),
        QStringLiteral("file"));
    const QCommandLineOption renderOpt(
        QStringLiteral("render"),
        QStringLiteral("Production render to this PNG and exit. Needs a GPU but NO window, "
                       "unlike --screenshot."),
        QStringLiteral("path"));
    const QCommandLineOption transparentOpt(
        QStringLiteral("transparent"),
        QStringLiteral("Render --render's background transparent, for compositing."));
    const QCommandLineOption eyeColourOpt(
        QStringLiteral("eye-colour"),
        QStringLiteral("Iris colour worn on the eye proxy: brown, amber, hazel, green, blue or "
                       "grey. Independent of --eyes, which picks the geometry."),
        QStringLiteral("name"), QStringLiteral("brown"));
    const QCommandLineOption randomOpt(
        QStringLiteral("random"),
        QStringLiteral("Randomise the character with this seed. Deterministic: the same seed "
                       "always gives the same person."),
        QStringLiteral("seed"));
    const QCommandLineOption gridOpt(
        QStringLiteral("grid"),
        QStringLiteral("Show the ground grid and backplane in the viewport. Not drawn by "
                       "--render, which produces the character alone."));
    const QCommandLineOption wireframeOpt(
        QStringLiteral("wireframe"),
        QStringLiteral("Draw edges instead of filled faces, in the viewport and in --render. "
                       "Ignored on a device that cannot draw non-filled polygons."));
    const QCommandLineOption symmetryOpt(
        QStringLiteral("symmetry"),
        QStringLiteral("Mirror the character: l2r copies the left side onto the right, r2l the "
                       "reverse. Runs after --set, so it mirrors the finished character."),
        QStringLiteral("direction"));
    const QCommandLineOption shadingOpt(
        QStringLiteral("shading"),
        QStringLiteral("litsphere (the reference matcap, default) or pbr (metallic-roughness). "
                       "Applies to the viewport and to --render."),
        QStringLiteral("model"), QStringLiteral("litsphere"));
    // The 179-bone superset is the default rig (owner decision, 2026-09-05:
    // "use the 179-bone set, it's more rich"). It is MakeHuman's own 163-bone
    // rig plus the 16 bones Mixamo names and it lacks, so every bone of the
    // default survives and every Mixamo bone has a home -- nothing is given up
    // by defaulting to it, and a retarget to Mixamo becomes total rather than
    // lossy. `--rig default` still selects the reference's 163-bone rig, which
    // is what the parity fixtures are captured against.
    const QCommandLineOption rigOpt(
        QStringLiteral("rig"),
        QStringLiteral("Skeleton to pose and skin with: a stem under data/rigs "
                       "(mixamo_superset, default) or a path to a .mhskel."),
        QStringLiteral("name"), QStringLiteral("mixamo_superset"));
    const QCommandLineOption exportOpt(
        QStringLiteral("export"),
        QStringLiteral("Write the posed mesh here and exit. Format from the extension: "
                       ".obj .fbx .glb .usda .usdz .dae .stl .3mf .bvh. Repeatable: "
                       "each path is written from the SAME character, in order."),
        QStringLiteral("path"));
    const QCommandLineOption dracoOpt(
        QStringLiteral("draco"),
        QStringLiteral("Compress glTF geometry with KHR_draco_mesh_compression. The extension "
                       "is REQUIRED in the file, so a consumer without a decoder cannot open "
                       "it -- the geometry exists in no other form."));
    const QCommandLineOption inspectOpt(
        QStringLiteral("inspect"),
        QStringLiteral("Read a mesh file with our own importer, print what it holds, and exit. "
                       "Anything assimp reads: .obj .fbx .glb .gltf .dae .stl .3mf"),
        QStringLiteral("path"));
    const QCommandLineOption setOpt(
        QStringLiteral("set"),
        QStringLiteral("Set a modifier before rendering or exporting, as "
                       "<full/name>=<value>. Repeatable."),
        QStringLiteral("modifier=value"));
    const QCommandLineOption languageOpt(
        QStringLiteral("language"),
        QStringLiteral("Interface language: a stem under data/languages "
                       "(german_generic, arabic_generic, …). Default is English."),
        QStringLiteral("name"));

    const QCommandLineOption blendshapesOpt(
        QStringLiteral("blendshapes"),
        QStringLiteral("Export the 34 expression units as glTF morph targets (.glb only)."));

    const QCommandLineOption skinMaterialOpt(
        QStringLiteral("skin-material"),
        QStringLiteral("Skin MATERIAL under data/skins: default, or one of the eight "
                       "generated tones (fair_rose … african_rich). This is the textured, "
                       "exported material -- unlike --skin, which is a viewport matcap."),
        QStringLiteral("name"), QStringLiteral("default"));

    // `--litsphere`, with `--skin` kept as an alias (directive 13.6). It picks
    // a viewport MATCAP, not a material: `--skin african` still exports
    // DefaultSkin, which is correct and reads as a bug. Qt takes a name LIST
    // for exactly this, so one option answers to both and `parser.value()` does
    // not care which the user typed.
    //
    // The autoBlendSkin caveat is in the help text because leaving it out is
    // how the flag reads as broken: under `default.mhmat` -- which sets
    // `autoBlendSkin true` -- the litsphere is blended from the ethnicity
    // macros and this is overridden. Measured: `--skin african` and
    // `--skin caucasian` render BYTE-IDENTICALLY on the default material, and
    // differ under any of the eight textured ones.
    const QCommandLineOption skinOpt(
        QStringList{QStringLiteral("litsphere"), QStringLiteral("skin")},
        QStringLiteral("Litsphere to shade the viewport with: african, asian or caucasian "
                       "(default). Ignored by a material with autoBlendSkin on -- including "
                       "the default one, which blends the three by ethnicity. `--skin` is "
                       "the old name for this and still works."),
        QStringLiteral("name"), QStringLiteral("caucasian"));
    const QCommandLineOption loadOpt(QStringLiteral("load"),
                                     QStringLiteral("Load a .mhm character before anything else."),
                                     QStringLiteral("path"));
    const QCommandLineOption saveOpt(QStringLiteral("save"),
                                     QStringLiteral("Write the character as .mhm and exit."),
                                     QStringLiteral("path"));
    const QCommandLineOption subdivOpt(
        QStringLiteral("subdivide"),
        QStringLiteral("Draw and export the Catmull-Clark subdivided mesh."));
    const QCommandLineOption lodOpt(
        QStringLiteral("lod"),
        QStringLiteral("One level of an LOD chain, as the fraction of the body's triangles to "
                       "keep. Repeatable, and the level NUMBER is the order given: the files are "
                       "named <export>_lod0, _lod1, ... Needs --export, and the chain is written "
                       "as .glb or .fbx."),
        QStringLiteral("ratio"));
    const QCommandLineOption decimateOpt(
        QStringLiteral("decimate"),
        QStringLiteral("Write a level of detail: keep this fraction of the BODY's triangles, in "
                       "(0, 1]. Worn proxies keep their own resolution. EXPORT only -- the "
                       "viewport and --render always draw the full mesh. Carries no skeleton and "
                       "no blendshapes, because an edge collapse renumbers every vertex they are "
                       "indexed by."),
        QStringLiteral("ratio"));
    const QCommandLineOption namingOpt(
        QStringLiteral("naming"),
        QStringLiteral("Which set of user-facing names to use: legacy (the default, so nothing "
                       "existing breaks) or modern. Either profile still accepts the other's "
                       "names, warning once, so old- and new-named things coexist."),
        QStringLiteral("profile"), QStringLiteral("legacy"));
    const QCommandLineOption workspaceOpt(
        QStringLiteral("workspace"),
        QStringLiteral("Start in a workspace preset: Modelling, Rigging, Materials or Export."),
        QStringLiteral("name"));
    const QCommandLineOption eyesOpt(
        QStringLiteral("eyes"),
        QStringLiteral("Eye proxy to wear: none, or the stem of a .mhclo under data/eyes "
                       "(default high-poly)"),
        QStringLiteral("name"), QString::fromLatin1(kDefaultEyes));
    // One `--<key>` per helper-cage slot, from the same table the choosers use,
    // so a new slot cannot arrive with a chooser but no flag (or the reverse).
    std::vector<QCommandLineOption> proxyOpts;
    proxyOpts.reserve(kProxySlots.size());
    for (const ProxySlot& slot : kProxySlots) {
        const QString key = QString::fromLatin1(slot.key);
        proxyOpts.emplace_back(key,
                               QStringLiteral("%1 proxy to wear: none (default), or the stem of a "
                                              ".mhclo under data/%2")
                                   .arg(QString::fromLatin1(slot.group), key),
                               QStringLiteral("name"), QStringLiteral("none"));
    }

    parser.addOption(workspaceOpt);
    parser.addOption(namingOpt);
    parser.addOption(subdivOpt);
    parser.addOption(decimateOpt);
    parser.addOption(lodOpt);
    parser.addOption(loadOpt);
    parser.addOption(saveOpt);
    parser.addOption(skinOpt);
    parser.addOption(skinMaterialOpt);
    parser.addOption(eyesOpt);
    for (const QCommandLineOption& opt : proxyOpts)
        parser.addOption(opt);
    parser.addOption(setOpt);
    parser.addOption(renderOpt);
    parser.addOption(backgroundOpt);
    parser.addOption(transparentOpt);
    parser.addOption(eyeColourOpt);
    parser.addOption(randomOpt);
    parser.addOption(symmetryOpt);
    parser.addOption(wireframeOpt);
    parser.addOption(gridOpt);
    // DUAL QUATERNION is the default, and this literal is where that default
    // actually lives -- gUseDualQuaternion is assigned from it below.
    //
    // Linear blending collapses a twisted limb, and on the base mesh that is
    // visible rather than theoretical: a T-pose render differs in 18,641 pixels
    // concentrated on the deltoid and the armpit fold, and the DQS deltoid is
    // the fuller of the two (measured and looked at). It costs 0.28 ms against
    // LBS's 0.12 ms for 19,158 vertices x 4 influences -- benchmarks/bench_core,
    // release, repeatable to the printed precision. 2.3x of a number that is
    // 1.7% of a 60 Hz frame. `linear` stays for anyone who wants exactly what
    // the reference did, which is the only thing it is now for.
    const QCommandLineOption skinningOpt(
        QStringLiteral("skinning"),
        QStringLiteral("Skinning method: dqs (default) or linear. Dual quaternion skinning keeps "
                       "a twisted limb's volume where linear blending collapses it; linear is "
                       "what the reference did."),
        QStringLiteral("method"), QStringLiteral("dqs"));
    const QCommandLineOption customTargetsOpt(
        QStringLiteral("custom-targets"),
        QStringLiteral("Directory of your own .target morphs. Each file becomes a modifier "
                       "named custom/<filename>, settable with --set like any other."),
        QStringLiteral("dir"));
    parser.addOption(customTargetsOpt);
    parser.addOption(skinningOpt);
    const QCommandLineOption correctivesOpt(
        QStringLiteral("correctives"),
        QStringLiteral("Pose-space correctives: the path to a corrective manifest "
                       "(docs/formats/corrective-manifest.md). The compiled blob is written "
                       "beside it and reused until the manifest changes."),
        QStringLiteral("manifest"));
    parser.addOption(correctivesOpt);
    const QCommandLineOption lookAtOpt(
        QStringLiteral("look-at"),
        QStringLiteral("Aim the eyes at a point in model space, as X,Y,Z in decimetres "
                       "(Y-up, the model facing +Z). Each eye is aimed from its OWN position, "
                       "so a near target converges them. Clamped to the human range."),
        QStringLiteral("x,y,z"));
    parser.addOption(lookAtOpt);
    parser.addOption(shadingOpt);
    parser.addOption(rigOpt);
    parser.addOption(poseOpt);
    parser.addOption(poseUnitOpt);
    parser.addOption(listPoseUnitsOpt);
    parser.addOption(spreadRootsOpt);
    parser.addOption(poseFrameOpt);
    parser.addOption(expressionOpt);
    parser.addOption(facsOpt);
    parser.addOption(saveExpressionOpt);
    parser.addOption(exportOpt);
    parser.addOption(languageOpt);
    parser.addOption(blendshapesOpt);
    parser.addOption(dracoOpt);
    parser.addOption(inspectOpt);
    parser.addOption(shaderOpt);
    parser.addOption(shotOpt);
    parser.process(app);

    // An unrecognised model is refused rather than defaulted: silently falling
    // back to the litsphere would make `--shading pbrr` produce a plausible
    // image that is not what was asked for.
    const QString skinningName = parser.value(skinningOpt).toLower();
    if (skinningName != QLatin1String("linear") && skinningName != QLatin1String("dqs")) {
        std::fprintf(stderr, "unknown skinning method \"%s\" (linear or dqs)\n",
                     skinningName.toStdString().c_str());
        return 1;
    }
    gUseDualQuaternion = skinningName == QLatin1String("dqs");

    // The stored skinning preference belongs to the WINDOW, and a headless run
    // builds no window, so `--skinning` alone decides what this run does. That
    // is deliberate: a scripted export must not change meaning because of what
    // someone last clicked in a menu, or one command would produce different
    // geometry on two machines.
    //
    // What was wrong is that it happened silently -- pick Linear in the menu,
    // script an export, get dual quaternion and never be told. Saying so costs
    // one line and removes the only way this can surprise someone. Only a
    // DISAGREEMENT is reported; a stored preference that matches the run, and
    // the overwhelmingly common case of no preference at all, stay quiet.
    if (const auto stored = mh::ui::storedSkinning()) {
        const bool storedIsDqs = *stored == mh::ui::Skinning::DualQuaternion;
        if (storedIsDqs != gUseDualQuaternion) {
            std::fprintf(stderr,
                         "stored skinning preference is %s, but this run is using %s "
                         "(the menu is a window preference; --skinning decides here)\n",
                         storedIsDqs ? "dqs" : "linear", gUseDualQuaternion ? "dqs" : "linear");
        }
    }

    const QString shadingName        = parser.value(shadingOpt);
    mh::render::ShadingModel shading = mh::render::ShadingModel::Litsphere;
    if (shadingName == QLatin1String("pbr")) {
        shading = mh::render::ShadingModel::Pbr;
    } else if (shadingName != QLatin1String("litsphere")) {
        std::fprintf(stderr, "unknown --shading %s; expected litsphere or pbr\n",
                     shadingName.toStdString().c_str());
        return 1;
    }

    // Before anything else, because it is about the file it is given rather
    // than about a character: no base mesh, no rig, no assets.
    if (parser.isSet(inspectOpt)) {
        return inspectFile(parser.value(inspectOpt).toStdString()) ? 0 : 1;
    }

    // Everything past here needs the asset tree, so this is where the check
    // belongs -- not before the parser, where it also killed --version and
    // --help, and where it made the promise three lines up false.
    if (!std::filesystem::exists(dataDir() / "3dobjs" / "base.obj")) {
        std::fprintf(stderr,
                     "cannot find the asset tree (looked at %s)\n"
                     "set MH_DATA_DIR to point at it\n",
                     dataDir().string().c_str());
        return 1;
    }

    // Set once, before anything loads a skeleton.
    setRigName(parser.value(rigOpt).toStdString());

    auto mesh = mh::core::loadObj(dataDir() / "3dobjs" / "base.obj");
    if (!mesh) {
        std::fprintf(stderr, "cannot load the base mesh: %s\n", mesh.error().message().c_str());
        return 1;
    }
    // The modelling panel. mh_ui never sees a Modifier -- core resolves the
    // registry and hands down plain TaskViewSpecs, which is what keeps the UI
    // module Apache-2.0. loadStandardLayout also puts the task views in the
    // reference's tab order, which is NOT the order the files are written in.
    auto standard = mh::core::loadStandardLayout(dataDir() / "modifiers");
    if (!standard) {
        std::fprintf(stderr, "cannot load the modifier registry: %s\n",
                     standard.error().message().c_str());
        return 1;
    }
    const std::vector<mh::foundation::TaskViewSpec>& views = standard->views;

    if (parser.isSet(eyeColourOpt)) {
        const std::string want = parser.value(eyeColourOpt).toStdString();
        const auto available   = availableEyeColours();
        if (std::ranges::find(available, want) == available.end()) {
            std::string list;
            for (const auto& n : available)
                list += (list.empty() ? "" : ", ") + n;
            std::fprintf(stderr, "unknown --eye-colour %s; available: %s\n", want.c_str(),
                         list.c_str());
            return 1;
        }
        eyeColourRef() = want;
    }

    if (parser.isSet(skinMaterialOpt)) {
        const std::string want = parser.value(skinMaterialOpt).toStdString();
        const auto available   = availableSkinMaterials();
        if (std::ranges::find(available, want) == available.end()) {
            std::string list;
            for (const auto& n : available)
                list += (list.empty() ? "" : ", ") + n;
            std::fprintf(stderr, "unknown --skin-material %s; available: %s\n", want.c_str(),
                         list.c_str());
            return 1;
        }
        skinMaterialRef() = want;
    }

    const mh::core::TargetIndex index = mh::core::TargetIndex::build(dataDir() / "targets");

    // A user's own morphs, appended to the shipped sliders. Reported rather
    // than silent: a directory with nothing usable in it looks identical to a
    // mistyped path from the outside, and the reference says so too
    // (`0_modeling_9_custom_targets.py:190`).
    std::vector<mh::core::Modifier> allModifiers = standard->modifiers;
    if (parser.isSet(customTargetsOpt)) {
        const std::filesystem::path dir = parser.value(customTargetsOpt).toStdString();
        auto custom                     = mh::core::customModifiers(dir);
        std::fprintf(stderr, "custom targets: %zu in %s\n", custom.size(), dir.string().c_str());
        allModifiers.insert(allModifiers.end(), std::make_move_iterator(custom.begin()),
                            std::make_move_iterator(custom.end()));
    }
    mh::core::Human human(&index, std::move(allModifiers));
    // Lazy: only the targets a slider actually reaches are read from disk, so
    // start-up does not pay for all 1,280.
    mh::core::TargetLibrary targets(dataDir() / "targets");

    // The character as a file: modifiers are refreshed from `human` on save, but
    // everything else -- uuid, tags, camera, and the skeleton/proxy/material
    // lines this build cannot yet interpret -- survives only by being carried.
    mh::core::MhmFile document;

    if (parser.isSet(loadOpt)) {
        const std::filesystem::path file = parser.value(loadOpt).toStdString();
        const auto loaded                = mh::core::loadMhm(file);
        if (!loaded) {
            std::fprintf(stderr, "cannot load %s: %s\n", file.string().c_str(),
                         loaded.error().message().c_str());
            return 1;
        }
        // Reset first, as the reference does (human.py:1486): a modifier the file
        // does not mention must go back to its default, not keep what was there.
        human.resetToDefaults();
        uint32_t unknown       = 0;
        const uint32_t applied = mh::core::applyMhm(*loaded, human, &unknown);
        document               = *loaded;
        std::printf("loaded %s: %u modifiers applied", file.string().c_str(), applied);
        if (unknown > 0) std::printf(", %u unknown", unknown);
        std::printf("\n");

        // The document's own rig, unless the user named one. `setRigName` ran
        // before the load -- it has to, the flag is available first -- so this
        // is where a file's choice gets its say.
        //
        // Format from the reference (`skeletonlibrary.py:336-339`):
        // `skeleton <relative path>`, e.g. `default.mhskel`. We resolve by stem,
        // which is also what `--rig` takes.
        if (!parser.isSet(rigOpt)) {
            if (const auto skel = valueFromDocument(document, "skeleton")) {
                setRigName(std::filesystem::path(*skel).stem().string());
            }
        }
    }

    // Before --set, so an explicit value on the command line still wins -- the
    // same precedence --set already has over a loaded file. After --load for
    // the same reason: randomising is an edit, and an edit applies to whatever
    // was opened.
    if (parser.isSet(randomOpt)) {
        bool ok               = false;
        const qulonglong seed = parser.value(randomOpt).toULongLong(&ok);
        if (!ok) {
            std::fprintf(stderr, "--random wants a number, got \"%s\"\n",
                         parser.value(randomOpt).toStdString().c_str());
            return 1;
        }
        const auto changed = mh::core::randomize(human, mh::core::RandomOptions{}, seed);
        std::printf("randomised %zu modifiers (seed %llu)\n", changed.size(),
                    static_cast<unsigned long long>(seed));
    }

    // --set runs after --load deliberately, so an explicit value on the command
    // line wins over the file.
    // Collected so the panel can be moved to match: a mesh that is morphed
    // while the sliders show their defaults is a UI that lies, and the first
    // nudge of such a slider snaps the model back.
    std::vector<std::pair<QString, float>> presets;
    for (const QString& assignment : parser.values(setOpt)) {
        const QStringList halves = assignment.split(QLatin1Char('='));
        bool ok                  = false;
        const float v            = halves.size() == 2 ? halves[1].toFloat(&ok) : 0.0F;
        // std::isfinite matters: QString::toFloat accepts "nan", and
        // std::clamp passes NaN straight through (both comparisons are false),
        // so every vertex ends up NaN and the export still exits 0.
        if (halves.size() != 2 || halves[0].isEmpty() || !ok || !std::isfinite(v)) {
            std::fprintf(stderr, "--set wants <modifier>=<finite number>, got \"%s\"\n",
                         assignment.toStdString().c_str());
            return 1;
        }
        if (!human.setModifierValue(halves[0].toStdString(), v)) {
            std::fprintf(stderr, "no such modifier: \"%s\"\n", halves[0].toStdString().c_str());
            return 1;
        }
        presets.emplace_back(halves[0], v);
    }

    // Refused rather than clamped: 0 asks for no triangles at all and 2 asks
    // for twice as many, and neither is a level of detail. QString::toFloat
    // also accepts "nan", which would pass every comparison it is put through.
    float decimateRatio = 0.0F;
    if (parser.isSet(decimateOpt)) {
        bool ok           = false;
        const float ratio = parser.value(decimateOpt).toFloat(&ok);
        if (!ok || !std::isfinite(ratio) || ratio <= 0.0F || ratio > 1.0F) {
            std::fprintf(stderr, "--decimate wants a fraction in (0, 1], got \"%s\"\n",
                         parser.value(decimateOpt).toStdString().c_str());
            return 1;
        }
        decimateRatio = ratio;
    }

    // Refused rather than defaulted, like --shading and --skinning: silently
    // falling back to legacy would make `--naming moderm` produce the old names
    // and look like the flag did nothing.
    const QString namingName = parser.value(namingOpt).toLower();
    if (namingName != QLatin1String("legacy") && namingName != QLatin1String("modern")) {
        std::fprintf(stderr, "unknown --naming profile \"%s\" (legacy or modern)\n",
                     namingName.toStdString().c_str());
        return 1;
    }
    const auto namingProfile = namingName == QLatin1String("modern")
                                   ? mh::foundation::NamingProfile::Modern
                                   : mh::foundation::NamingProfile::Legacy;

    // An LOD CHAIN. The level number is the ORDER given rather than the sorted
    // ratio, so a caller deciding that level 1 is the coarse one and level 2
    // the fine one is obeyed rather than corrected.
    std::vector<float> lodRatios;
    for (const QString& value : parser.values(lodOpt)) {
        bool ok           = false;
        const float ratio = value.toFloat(&ok);
        if (!ok || !std::isfinite(ratio) || ratio <= 0.0F || ratio > 1.0F) {
            std::fprintf(stderr, "--lod wants a fraction in (0, 1], got \"%s\"\n",
                         value.toStdString().c_str());
            return 1;
        }
        lodRatios.push_back(ratio);
    }
    if (!lodRatios.empty()) {
        // Two ways to say "reduce the body", and no reading of the pair that is
        // not either a contradiction or a repetition.
        if (parser.isSet(decimateOpt)) {
            std::fprintf(stderr, "--lod and --decimate both reduce the body; give one\n");
            return 1;
        }
        if (!parser.isSet(exportOpt)) {
            std::fprintf(stderr, "--lod writes a chain of files, so it needs --export\n");
            return 1;
        }
        // Owner decision, 2026-09-08: a chain ships as separate GLB and FBX
        // files. Refused rather than widened here, because the formats are the
        // decision -- a chain of OBJs is a different product question.
        for (const QString& path : parser.values(exportOpt)) {
            const std::string ext = lowerExtension(path.toStdString());
            if (ext != ".glb" && ext != ".fbx") {
                std::fprintf(stderr, "an LOD chain is written as .glb or .fbx, not %s\n",
                             ext.c_str());
                return 1;
            }
        }
    }

    // Symmetry LAST, so it mirrors whatever --load, --random and --set between
    // them produced. The spelling names both sides rather than just the target,
    // because "--symmetry r" reads as "make it right-handed" to everyone who
    // has not read `human.py:1238`.
    if (parser.isSet(expressionOpt)) {
        expressionFileRef() = parser.value(expressionOpt).toStdString();
    }

    // Both at once is refused rather than resolved. They are two spellings of
    // one thing, and each applies its own blend and then REPLACES the face
    // bones, so whichever ran second would silently be the only one that
    // showed -- a face missing half of what was asked for, exit 0.
    if (parser.isSet(listPoseUnitsOpt)) {
        const auto names =
            mh::rig::loadPoseUnitNames(dataDir() / "poseunits" / "face-poseunits.json");
        if (!names) {
            std::fprintf(stderr, "cannot read the pose units: %s\n",
                         names.error().message().c_str());
            return 1;
        }
        for (const std::string& n : *names)
            std::printf("%s\n", n.c_str());
        return 0;
    }

    // Hair roots, for the style generator.
    //
    // `mh::core::spreadOverSurface` exists because raycasting at the scalp
    // failed: the scalp is not a closed dome, a direction grid misses straight
    // up and dead front, and the nearest-direction fallback for a miss quietly
    // piles every miss onto the rim. Walking the surface has no rays and so no
    // misses. This flag is what lets the style generator use it instead of
    // reimplementing Dijkstra: every asset generator in `tools/` is Python --
    // the five `make_*.py` -- so whatever generates the styles cannot call
    // `mh::core` directly.
    //
    // The BASE mesh in rest, deliberately: a proxy binds to the base mesh, so
    // generation time is the only time these roots mean anything. Roots for a
    // posed or morphed body would be the same vertices in different places.
    if (parser.isSet(spreadRootsOpt)) {
        bool ok          = false;
        const int wanted = parser.value(spreadRootsOpt).toInt(&ok);
        if (!ok || wanted < 1) {
            std::fprintf(stderr, "--spread-roots wants a count above 0, got \"%s\"\n",
                         parser.value(spreadRootsOpt).toStdString().c_str());
            return 1;
        }
        const auto base = mh::core::loadObj(dataDir() / "3dobjs" / "base.obj");
        if (!base) {
            std::fprintf(stderr, "cannot read the base mesh: %s\n", base.error().message().c_str());
            return 1;
        }
        // The cranium cap: everything above the cranium centre's height, which
        // `memory/todo.md` measured at (0, 7.75, 0.50). A hairline is a
        // narrower region than this and belongs to the style, not to the
        // primitive -- this is the scalp every style starts from.
        std::vector<uint32_t> scalp;
        const auto coords = base->coord();
        for (uint32_t v = 0; v < coords.size(); ++v) {
            if (coords[v].y > 7.75F) scalp.push_back(v);
        }
        for (const uint32_t root :
             mh::core::spreadOverSurface(*base, scalp, static_cast<size_t>(wanted))) {
            const auto& p = coords[root];
            std::printf("%u %.4f %.4f %.4f\n", root, static_cast<double>(p.x),
                        static_cast<double>(p.y), static_cast<double>(p.z));
        }
        return 0;
    }

    // Three vocabularies for one face. Layering two of them would apply both to
    // the same bones, so whichever ran second would silently be the only one
    // that showed -- the reason --expression and --facs already refuse each
    // other, and it applies just as much to raw units.
    if ((parser.isSet(poseUnitOpt) ? 1 : 0) + (parser.isSet(expressionOpt) ? 1 : 0) +
            (parser.isSet(facsOpt) ? 1 : 0) >
        1) {
        std::fprintf(stderr,
                     "--pose-unit, --expression and --facs all describe the face; give one\n");
        return 1;
    }
    for (const QString& assignment : parser.values(poseUnitOpt)) {
        const QStringList halves = assignment.split(QLatin1Char('='));
        bool ok                  = false;
        const float w            = halves.size() == 2 ? halves[1].toFloat(&ok) : 0.0F;
        // Range-checked like --facs: a unit weight is how far along its single
        // authored shape the face travels, so outside 0..1 the blend
        // extrapolates into a face nobody authored.
        if (halves.size() != 2 || halves[0].isEmpty() || !ok || !std::isfinite(w) || w < 0.0F ||
            w > 1.0F) {
            std::fprintf(stderr, "--pose-unit wants <unit>=<weight in 0..1>, got \"%s\"\n",
                         assignment.toStdString().c_str());
            return 1;
        }
        // The NAME is checked where every expression's names are checked, in
        // applyExpressionUnits, so a typo is reported the same way whether it
        // came from a file or the command line.
        poseUnitsRef().push_back({halves[0].toStdString(), w});
    }

    if (parser.isSet(expressionOpt) && parser.isSet(facsOpt)) {
        std::fprintf(stderr, "--expression and --facs both describe the face; give one\n");
        return 1;
    }
    for (const QString& assignment : parser.values(facsOpt)) {
        const QStringList halves = assignment.split(QLatin1Char('='));
        bool ok                  = false;
        const float w            = halves.size() == 2 ? halves[1].toFloat(&ok) : 0.0F;
        // Range-checked, unlike --set: an Action Unit is a muscle contraction,
        // graded A to E between none and full, so 5.0 is not a strong request
        // but a meaningless one -- and the blend would extrapolate it into a
        // face nobody asked for.
        if (halves.size() != 2 || halves[0].isEmpty() || !ok || !std::isfinite(w) || w < 0.0F ||
            w > 1.0F) {
            std::fprintf(stderr, "--facs wants <AU>=<weight in 0..1>, got \"%s\"\n",
                         assignment.toStdString().c_str());
            return 1;
        }
        facsRef().push_back({halves[0].toStdString(), w});
    }

    // Saving is the missing half of the expression mixer: the port could read a
    // .mhpose and derive one from FACS, but never write one, so an expression a
    // user composed could not be kept -- and nothing under data/ is a .mhpose
    // because there was no way to make one.
    //
    // It saves what --facs composed, not what --expression loaded: re-writing a
    // file that was just read is a copy, and refusing says so rather than
    // producing one.
    if (parser.isSet(saveExpressionOpt)) {
        if (facsRef().empty() && poseUnitsRef().empty()) {
            std::fprintf(stderr,
                         "--save-expression writes the composed face as a reusable "
                         "expression; give at least one --facs or --pose-unit\n");
            return 1;
        }
        const std::filesystem::path out = parser.value(saveExpressionOpt).toStdString();
        auto expr                       = requestedExpression();
        if (!expr) {
            std::fprintf(stderr, "cannot build the expression: %s\n", expr.error().c_str());
            return 1;
        }
        // What was ASKED FOR is the description: "AU12=1" or "LeftBrowDown=1"
        // is re-derivable, where "Smile" is someone's guess about what it was.
        std::string from;
        const bool fromUnits = !poseUnitsRef().empty();
        if (fromUnits) {
            for (const auto& u : poseUnitsRef()) {
                if (!from.empty()) from += ", ";
                from += std::format("{}={}", u.name, u.weight);
            }
        } else {
            for (const auto& au : facsRef()) {
                if (!from.empty()) from += ", ";
                from += std::format("{}={}", au.code, au.weight);
            }
        }
        expr->name        = prettyName(out, {});
        expr->description = (fromUnits ? "Pose units: " : "Action Units: ") + from;
        expr->tags        = {fromUnits ? "units" : "facs"};
        if (const auto saved = mh::rig::saveExpression(out, *expr); !saved) {
            std::fprintf(stderr, "cannot write %s: %s\n", out.string().c_str(),
                         saved.error().message().c_str());
            return 1;
        }
        std::printf("wrote %s\n", out.string().c_str());
        return 0;
    }

    // The backdrop, loaded once. An unreadable file is an error rather than a
    // silently missing background: a user who passed --background and got a
    // plain render would have no way to tell which of the two went wrong.
    QImage backdrop;
    if (parser.isSet(backgroundOpt)) {
        const QString file = parser.value(backgroundOpt);
        if (!backdrop.load(file)) {
            std::fprintf(stderr, "cannot read --background image \"%s\"\n",
                         file.toStdString().c_str());
            return 1;
        }
        if (!parser.isSet(renderOpt)) {
            std::fprintf(stderr, "--background is the backdrop for --render; give one\n");
            return 1;
        }
    }

    if (parser.isSet(poseFrameOpt)) {
        // A frame with no file to take it from is a mistake worth naming: the
        // default pose is the authored A-pose, which has no frames at all.
        if (!parser.isSet(poseOpt)) {
            std::fprintf(stderr, "--pose-frame needs a --pose .bvh to take the frame from\n");
            return 1;
        }
        bool ok         = false;
        const int frame = parser.value(poseFrameOpt).toInt(&ok);
        if (!ok || frame < 0) {
            std::fprintf(stderr, "--pose-frame wants a frame index from 0, got \"%s\"\n",
                         parser.value(poseFrameOpt).toStdString().c_str());
            return 1;
        }
        poseFrameRef() = static_cast<size_t>(frame);
    }

    if (parser.isSet(symmetryOpt)) {
        const QString direction = parser.value(symmetryOpt).toLower();
        if (direction != QLatin1String("l2r") && direction != QLatin1String("r2l")) {
            std::fprintf(stderr, "unknown --symmetry direction \"%s\" (l2r or r2l)\n",
                         direction.toStdString().c_str());
            return 1;
        }
        const auto mirrored =
            mh::core::symmetrise(human, direction == QLatin1String("l2r") ? 'r' : 'l');
        std::printf("mirrored %zu modifiers (%s)\n", mirrored.size(),
                    direction.toStdString().c_str());
        // The sliders have to follow, exactly as --set's do: a panel showing
        // the pre-mirror value snaps the model back on the first nudge.
        for (const auto& [name, value] : mirrored)
            presets.emplace_back(QString::fromStdString(name), value);
    }

    if (human.stackSize() > 0) {
        uint32_t missing       = 0;
        const uint32_t applied = human.applyStack(*mesh, targets, &missing);
        std::printf("applied %u targets (%u missing)\n", applied, missing);
    }

    PoseRig rig;
    // `pose <relative path>` (`3_libraries_pose.py:265-268`). We write and read
    // whatever `--pose` takes -- a stem like `tpose`, or a path -- so the value
    // round-trips exactly. A reference-written path relative to ITS pose library
    // will not resolve here; that is a real limit, not a silent one, because
    // loadPoseRig reports what it could not open.
    std::string poseChoice = parser.value(poseOpt).toStdString();
    if (!parser.isSet(poseOpt)) {
        if (const auto fromDoc = valueFromDocument(document, "pose")) poseChoice = *fromDoc;
    }

    // The skin material the file names, unless the command line overrode it --
    // same precedence as the rig and the pose. The line holds a relative path
    // (`skins/african_deep.mhmat`); we select by stem, so take the stem and
    // ignore a material from a directory we do not ship rather than failing the
    // load over it.
    // Same precedence as the skin material: the command line wins over the file.
    if (!parser.isSet(eyeColourOpt)) {
        if (const auto fromDoc = valueFromDocument(document, "eyeMaterial")) {
            const std::string stem = std::filesystem::path(*fromDoc).stem().string();
            const auto available   = availableEyeColours();
            if (std::ranges::find(available, stem) != available.end()) {
                eyeColourRef() = stem;
                std::printf("eye colour %s (from the file)\n", stem.c_str());
            }
        }
    }

    if (!parser.isSet(skinMaterialOpt)) {
        if (const auto fromDoc = valueFromDocument(document, "skinMaterial")) {
            const std::string stem = std::filesystem::path(*fromDoc).stem().string();
            const auto available   = availableSkinMaterials();
            if (std::ranges::find(available, stem) != available.end()) {
                skinMaterialRef() = stem;
                std::printf("skin material %s (from the file)\n", stem.c_str());
            } else {
                std::fprintf(stderr, "the file names skin material %s, which is not installed\n",
                             fromDoc->c_str());
            }
        }
    }

    // The litsphere the file names, unless the command line overrode it -- the
    // same precedence as the rig, the pose, the eye colour and the material.
    // Applied by REPLACING the option's value, because that is what
    // `buildAssetGroups` is handed a few lines below and what the picker then
    // starts on.
    std::string litsphereChoice = parser.value(skinOpt).toStdString();
    if (!parser.isSet(skinOpt)) {
        if (const auto fromDoc = valueFromDocument(document, "litsphere")) {
            litsphereChoice = *fromDoc;
            std::printf("litsphere %s (from the file)\n", fromDoc->c_str());
        }
    }
    // Parsed BEFORE the rig is loaded, because `loadPoseRig` is what applies it.
    // It was five lines below this call first time out, and the app rendered a
    // character staring straight ahead while reporting nothing at all.
    if (parser.isSet(lookAtOpt)) {
        const QStringList parts = parser.value(lookAtOpt).split(QLatin1Char(','));
        bool ok                 = parts.size() == 3;
        mh::foundation::Vec3 at{};
        if (ok) {
            bool okX = false;
            bool okY = false;
            bool okZ = false;
            at       = mh::foundation::Vec3{parts[0].toFloat(&okX), parts[1].toFloat(&okY),
                                      parts[2].toFloat(&okZ)};
            ok       = okX && okY && okZ;
        }
        if (!ok) {
            std::fprintf(stderr, "--look-at wants three numbers, as X,Y,Z\n");
            return 1;
        }
        gLookAt = at;
    }

    if (!loadPoseRig(*mesh, poseChoice, rig)) return 1;

    // Correctives, after the rig: binding needs the skeleton the drivers name
    // and the mesh the deltas index. Before any posing, because `poseInPlace`
    // reads `gCorrectives`.
    if (parser.isSet(correctivesOpt)) {
        const std::filesystem::path manifest = parser.value(correctivesOpt).toStdString();
        auto cache                           = mh::core::loadOrCompileCorrectives(manifest);
        if (!cache) {
            std::fprintf(stderr, "correctives: %s\n", cache.error().message().c_str());
            return 1;
        }
        auto bound   = std::make_unique<BoundCorrectives>();
        bound->bytes = std::move(cache->bytes);
        auto blob    = mh::core::readCorrectiveBlob(bound->bytes);
        if (!blob) {
            // The cache only ever hands back a blob it has just read or just
            // compiled, so this is a "cannot happen" that is reported rather
            // than asserted -- a wrong answer here would be a silently
            // corrective-free character.
            std::fprintf(stderr, "correctives: %s\n", blob.error().message().c_str());
            return 1;
        }
        bound->blob  = std::move(*blob);
        auto runtime = mh::rig::CorrectiveRuntime::bind(
            bound->blob, rig.skeleton, mh::core::topologyHash(*mesh), mesh->coord());
        if (!runtime) {
            std::fprintf(stderr, "correctives: %s\n", runtime.error().message().c_str());
            return 1;
        }
        bound->runtime  = std::move(*runtime);
        bound->manifest = std::move(cache->manifest);
        gCorrectives    = std::move(bound);
        std::printf(
            "correctives: %zu poses, %zu drivers, blob %s (%s)\n", gCorrectives->blob.poseCount,
            gCorrectives->blob.drivers.size(), cache->blobPath.filename().string().c_str(),
            cache->status == mh::core::CorrectiveCacheStatus::Reused ? "reused" : "rebuilt");
    }

    // Adjacency is topology and survives both morphing and posing, so it is
    // built once. Normals are not, and are recomputed on every rebuild.
    mesh->buildAdjacency();
    if (!poseInPlace(*mesh, rig)) return 1;
    mesh->calcNormals();
    // `calcVertexTangents` had no caller anywhere in src/, so the tangent array
    // was always empty: the viewport's normal-map branch had no basis to build a
    // TBN from, and no export carried a TANGENT. The tangents themselves are the
    // good ones -- Lengyel's method with the reference's three bugs fixed --
    // which is exactly why a consumer should get ours rather than regenerate
    // its own. Measured cost: 0.19 ms on the base mesh, per rebuild.
    mesh->calcVertexTangents();

    // Subdivision, honoured rather than merely stored. The .mhm carries the flag
    // and this build parsed it for three milestones without acting on it.
    bool subdivided = document.subdivide || parser.isSet(subdivOpt);
    std::optional<mh::core::Subdivider> subdiv;

    /// The mesh that actually gets drawn and exported.
    const auto displayMesh = [&]() -> const mh::core::Mesh& {
        if (!subdivided) return *mesh;
        // build() is 6.9 ms and refresh() 0.5 ms, so the topology is built once
        // and only the positions are recomputed as the character changes.
        if (!subdiv || !subdiv->matches(*mesh)) {
            auto built = mh::core::Subdivider::build(*mesh);
            if (!built) {
                std::fprintf(stderr, "cannot subdivide; drawing the base mesh\n");
                subdivided = false;
                return *mesh;
            }
            subdiv = std::move(*built);
        } else {
            subdiv->refresh(*mesh);
        }
        return subdiv->mesh();
    };

    const mh::core::Mesh& initial = displayMesh();
    auto rm                       = mh::core::RenderMesh::build(initial);
    // The face mask is applied below, not here: half of it is the helper-group
    // mask this mesh already knows, and half is what the worn proxies delete,
    // which is not decided until the choosers have run.

    // Built before the window so the initial litsphere is the one the picker
    // shows -- otherwise the panel and the viewport start out disagreeing --
    // and before --export and --save, both of which have to know what the
    // character is wearing.
    //
    // The loaded document's own selection wins over the chooser's default, and
    // an explicit --eyes wins over both: a file that recorded Low-Poly must not
    // reopen wearing the default, and a flag the user typed must not be
    // overridden by the file.
    std::string eyesChoice = parser.value(eyesOpt).toStdString();
    if (!parser.isSet(eyesOpt)) {
        if (const auto fromDoc = proxyFromDocument(document, kEyesSaveName)) {
            eyesChoice = *fromDoc;
        }
    }

    // Every helper-cage slot takes the same shape as the eyes, including the
    // `.mhm` round trip: `recordProxy` and `proxyFromDocument` are keyed by slot
    // name, so a slot costs a table entry and no new machinery.
    std::map<std::string, std::string> proxyChoices;
    for (size_t i = 0; i < kProxySlots.size(); ++i) {
        std::string choice = parser.value(proxyOpts[i]).toStdString();
        if (!parser.isSet(proxyOpts[i])) {
            if (const auto fromDoc = proxyFromDocument(document, kProxySlots[i].key)) {
                choice = *fromDoc;
            }
        }
        proxyChoices.emplace(kProxySlots[i].group, std::move(choice));
    }
    // skinMaterialRef() rather than the raw option: by here it already carries
    // whatever --skin-material or the loaded .mhm chose, so the picker starts
    // on the material actually in use instead of on `default`.
    const auto assetGroups =
        buildAssetGroups(parser.value(poseOpt).toStdString(), litsphereChoice, eyesChoice,
                         skinMaterialRef(), rigNameRef(), eyeColourRef(), proxyChoices);
    // Announced so a test can see the picker was built at all -- a group that
    // silently ends up empty renders as a disabled combo box nobody notices.
    // The NAMES, not just the count. Nothing printed them, so the panel's
    // groups could be renamed with no test able to see it -- and one of them
    // needed renaming. Bracketed and comma-separated so "Skin material" cannot
    // satisfy a check meant for "Skin".
    std::string groupNames;
    for (const auto& g : assetGroups) {
        if (!groupNames.empty()) groupNames += ", ";
        groupNames += g.name;
    }
    std::printf("asset groups: %zu [%s] (skin materials: %zu, rigs: %zu)\n", assetGroups.size(),
                groupNames.c_str(), availableSkinMaterials().size(), rigStems().size());

    // The body's material and everything worn, read by every rebuild. Skin is
    // a path rather than a call to setLitsphere because the viewport now takes
    // one material per mesh: the body's has to travel with the body.
    // Seeded from the CHOOSER, not from the raw option, and here rather than in
    // the window setup: `--save` runs below and needs to know which litsphere is
    // in play. It used to be assigned only on the window path, so a headless
    // save wrote an EMPTY litsphere line -- which `recordLine` reads as "remove
    // the key", so the file recorded nothing at all.
    //
    // The chooser's value because it is the VALIDATED one: an unknown
    // `--litsphere` has already fallen back to the documented default, with a
    // warning, by the time the group is built.
    std::filesystem::path skin = selectedChoice(assetGroups, "Litsphere");
    std::map<QString, WornProxy> wornProxies;

    // Put on whatever the choosers start with. Done here, before both --export
    // and the window, so a headless export dresses the character exactly as the
    // window would: exporting a dressed character naked was the bug.
    if (const std::string eyes = selectedChoice(assetGroups, "Eyes");
        !eyes.empty() && eyes != kNoProxy) {
        if (auto worn = wearProxy(eyes, *mesh, eyeLitsphere(), /*isEyes=*/true)) {
            wornProxies.insert_or_assign(QStringLiteral("Eyes"), std::move(*worn));
        }
    }
    // Each slot gets its own matcap for the same reason the eyes do. Wiring the
    // teeth to the body's skin litsphere was tried first and RENDERED: they came
    // out at RGB (233, 155, 123), the same orange as the lip in front of them,
    // because these materials carry no diffuse texture and the litsphere shader
    // multiplies the matcap by the texture alone (litsphere.frag:157-162). A
    // fixed matcap also means a worn proxy does not go stale when the Litsphere
    // picker changes `skin` under it.
    for (const ProxySlot& slot : kProxySlots) {
        const std::string choice = selectedChoice(assetGroups, slot.group);
        if (choice.empty() || choice == kNoProxy) continue;
        if (auto worn = wearProxy(choice, *mesh, slotLitsphere(slot.key))) {
            wornProxies.insert_or_assign(QString::fromLatin1(slot.group), std::move(*worn));
        }
    }

    // AFTER the choosers, deliberately. This used to run before them, so a save
    // could not know what was worn and wrote no proxy line at all -- and a
    // character saved wearing Low-Poly reopened wearing High-Poly, while one
    // saved wearing NONE reopened wearing eyes.
    if (parser.isSet(saveOpt)) {
        const std::filesystem::path file = parser.value(saveOpt).toStdString();
        // No window, so no framing to record: the camera line stays as loaded.
        mh::core::MhmFile doc = documentFor(human, document, file, std::nullopt, subdivided);
        const auto* eyesWorn  = wornProxies.count(QStringLiteral("Eyes")) != 0
                                    ? &wornProxies.at(QStringLiteral("Eyes")).proxy
                                    : nullptr;
        recordProxy(doc, kEyesSaveName, eyesWorn != nullptr ? eyesWorn->name : std::string{},
                    eyesWorn != nullptr ? eyesWorn->uuid : std::string{});
        // Same shape for every helper-cage slot, and it is what makes
        // `proxyFromDocument` above reachable: without these lines a character
        // saved wearing teeth reopened without them, the record silently lost.
        for (const ProxySlot& slot : kProxySlots) {
            const auto it = wornProxies.find(QString::fromLatin1(slot.group));
            const bool on = it != wornProxies.end();
            recordProxy(doc, slot.key, on ? it->second.proxy.name : std::string{},
                        on ? it->second.proxy.uuid : std::string{});
        }
        // The rig and the pose, which a fresh save recorded not at all: saved
        // with `--rig mixamo_superset --pose tpose`, the file named neither and
        // reopened as the 163-bone default in the rest pose.
        // `skinMaterial <relative path>`, the line the reference writes
        // (3_libraries_material_chooser.py:305). Without it a textured
        // character reopened as the untextured default and the choice was gone.
        // The litsphere, under its own honest key. It is NOT `skinMaterial`: that
        // line names a .mhmat and this names a viewport matcap, which is the
        // whole reason the flag was renamed. An unknown key to MakeHuman 1.x,
        // which keeps lines it does not interpret.
        recordLine(doc, "litsphere", litsphereName(skin));
        recordLine(doc, "skinMaterial", "skins/" + skinMaterialRef() + ".mhmat");
        recordLine(doc, "eyeMaterial", "eyes/materials/" + eyeColourRef() + ".mhmat");
        recordLine(doc, "skeleton", rig.loaded() ? rigNameRef() + ".mhskel" : std::string{});
        recordLine(doc, "pose", rig.posed() ? poseChoice : std::string{});
        if (const auto ok = mh::core::saveMhm(file, doc); !ok) {
            std::fprintf(stderr, "cannot save %s: %s\n", file.string().c_str(),
                         ok.error().message().c_str());
            return 1;
        }
        std::printf("wrote %s (%zu modifiers, eyes: %s)\n", file.string().c_str(),
                    doc.modifiers.size(), eyesWorn != nullptr ? eyesWorn->name.c_str() : "none");
        return 0;  // --save means save and exit, as its help says
    }

    // Which body faces exist. base.obj is 138 parts helper geometry to 1 part
    // body -- drawing it raw gives a figure in a solid skirt with a box over its
    // face -- and on top of that a worn proxy may delete the body underneath it.
    // Both halves come from mh::core::bodyFaceMask so the viewport and every
    // writer get the same answer.
    //
    // Kept alive because it is also handed to the OBJ writer as a span.
    std::vector<uint8_t> bodyMask;
    const auto applyBodyMask = [&](const mh::core::Mesh& shown) -> bool {
        std::vector<const mh::core::Proxy*> worn;
        worn.reserve(wornProxies.size());
        for (const auto& [group, w] : wornProxies)
            worn.push_back(&w.proxy);

        auto mask = mh::core::bodyFaceMask(*mesh, shown, worn);
        if (!mask) return false;
        // The mask only changes when something is put on or taken off, so
        // everything below this line -- including the announcement and the
        // per-garment masks -- would otherwise repeat on every slider drag.
        if (*mask == bodyMask) return true;
        bodyMask = std::move(*mask);

        // Clothes hiding clothes. Each garment is masked by the layers above it
        // (`wornVertexMasks` walks the z_depth stack outermost first); without
        // this a jacket's delete_verts cut a hole in the BODY and the shirt
        // underneath went on rendering straight through it.
        //
        // Below the early-out on purpose: this depends on WHAT IS WORN, exactly
        // as the body mask does, and nothing here changes when a slider moves.
        const mh::core::WornMasks masks = mh::core::wornVertexMasks(worn, mesh->vertexCount());
        size_t slot                     = 0;
        for (auto& [group, w] : wornProxies) {
            const std::vector<uint8_t>& visible = masks.perProxy[slot++];
            // Nothing above it, or nothing above it deletes anything -- the
            // common case, and the one where re-uploading an index buffer would
            // buy nothing.
            if (std::ranges::all_of(visible, [](uint8_t v) { return v != 0U; })) continue;
            const auto faces = w.mesh.faceMaskForVisibleVertices(visible);
            // Announced rather than swallowed: `visible` is built from this
            // proxy's own vertex count, so a mismatch here means the fit and
            // the mesh disagree -- a garment that silently keeps rendering
            // through a hole looks like a modelling problem, not a bug.
            if (!faces || !w.rm.setFaceMask(w.mesh, *faces)) {
                std::fprintf(stderr, "cannot mask %s against the layers above it\n",
                             group.toLocal8Bit().constData());
                continue;
            }
            std::printf("%s: %zu of %zu faces visible under the layers above\n",
                        group.toLocal8Bit().constData(),
                        static_cast<size_t>(std::count(faces->begin(), faces->end(), uint8_t{1})),
                        faces->size());
        }
        // Announced, because a mask that silently stopped being applied still
        // renders a plausible picture and still writes a valid file -- it just
        // has the helper cages in it. app_smoke asserts on this line.
        std::printf("body: %zu of %zu faces visible\n",
                    static_cast<size_t>(std::count(bodyMask.begin(), bodyMask.end(), uint8_t{1})),
                    bodyMask.size());
        return rm.setFaceMask(shown, bodyMask);
    };
    if (!applyBodyMask(initial)) {
        std::fprintf(stderr, "cannot apply the body face mask\n");
        return 1;
    }

    // ONE export path, two triggers: `--export` and File > Export. Duplicating
    // it is how the menu and the command line quietly stop producing the same
    // file, and there are ~90 lines of live-rig restore, vertex compaction,
    // skin remapping and blendshape building to disagree about.
    const bool wantDraco = parser.isSet(dracoOpt);
    // @param decimateTo the fraction of the body's triangles to keep, or 0 for
    //        none. A parameter rather than the captured flag because an LOD
    //        chain calls this once per level with a different one each time.
    const auto exportTo = [&](const std::filesystem::path& outPath, bool wantBlendshapes,
                              float decimateTo) -> bool {
        // A LIVE RIG ships REST geometry with a POSED armature, so for the
        // formats that carry a skeleton the mesh goes back to its unposed
        // positions before it is written. Normals and tangents are recomputed
        // with it: they belong to the geometry in the file, and the posed ones
        // would light a rest mesh as though it were still bent.
        //
        // Only for those formats. An OBJ has nothing to apply a pose with, so
        // it keeps the baked posed mesh -- see formatCarriesRig.
        const std::string outExt = lowerExtension(outPath);
        const bool liveRig = rig.posed() && !rig.restCoords.empty() && formatCarriesRig(outExt);
        std::vector<mh::foundation::Vec3> posedBackup;
        if (liveRig) {
            // The rest geometry restored below is UNCORRECTED, and deliberately
            // so: `rig.restCoords` is captured before the correctives run, and
            // a pose-space corrective is not a rest shape -- baked into the
            // rest mesh it would put this pose's bulge on every pose the
            // consumer sets afterwards. None of glTF, FBX or UsdSkel has a
            // pose-driven shape to carry it in instead.
            //
            // So the DEFORMATION genuinely cannot travel in this file. Saying
            // nothing was the part that was wrong: the app printed "correctives:
            // N poses" and then wrote a file with none of them in it. Measured:
            // the .glb is byte-identical to one exported without --correctives
            // (app_correctives_live_rig_unchanged pins that, against a manifest
            // with no wrinkle in it).
            //
            // A WRINKLE SHEET is the exception, and the message says so: it
            // reaches every format, baked into the material's normal map by
            // `bakeWrinkleBeside`, because a texture has somewhere to go in
            // these formats and a pose-driven vertex delta does not.
            if (gCorrectives != nullptr) {
                std::fprintf(stderr,
                             "warning: corrective geometry does not reach a live-rig %s -- the "
                             "file carries REST geometry and a pose-space corrective is not a "
                             "rest shape. Export .obj for the corrected, baked mesh. (A wrinkle "
                             "sheet DOES travel, baked into the normal map.)\n",
                             outExt.c_str());
            }
            // Kept so the interactive path can undo this; see the restore below.
            posedBackup.assign(mesh->coord().begin(), mesh->coord().end());
            if (!mesh->changeCoords(std::vector<mh::foundation::Vec3>(rig.restCoords))) {
                std::fprintf(stderr, "cannot restore the rest mesh for a live rig\n");
                return false;
            }
            mesh->calcNormals();
            mesh->calcVertexTangents();
            std::printf("live rig: rest geometry + posed armature (%zu joints)\n",
                        rig.globalPose.size());
        }

        for (auto& [group, worn] : wornProxies)
            refitProxy(worn, *mesh);

        // After the restore, so the render mesh carries the vertices that will
        // be written rather than the ones that were on screen.
        if (liveRig) rm.refreshPositions(displayMesh());

        // The LOD, if one was asked for. Built HERE and not earlier for two
        // reasons: the live-rig swap above decides which vertices get written,
        // and the face mask has to be baked into the geometry BEFORE the
        // collapse. A mask is a per-face array, and an edge collapse destroys
        // the face-to-face correspondence it is expressed in -- so the order is
        // mask, then compact, then decimate. Decimating first and masking
        // afterwards is not a slower way to the same file; it is a crash on a
        // mask of the wrong length.
        std::optional<mh::core::Mesh> lod;
        std::optional<mh::core::RenderMesh> lodRm;
        std::optional<mh::io::CompactedMesh> lodCompact;
        // LOD render vertex -> BASE mesh vertex, so the weights compiled
        // against the base mesh still apply. Composed from the two mappings
        // below; empty when nothing was decimated.
        std::vector<uint32_t> lodVmap;
        if (decimateTo > 0.0F) {
            std::vector<uint32_t> maskSource;
            std::vector<uint32_t> lodSource;
            const auto masked = displayMesh().compactToFaces(bodyMask, &maskSource);
            if (!masked) {
                std::fprintf(stderr, "cannot bake the face mask before decimating\n");
                return false;
            }
            auto reduced = mh::core::decimate(*masked, {.ratio = decimateTo}, &lodSource);
            if (!reduced) {
                std::fprintf(stderr, "cannot decimate: %s\n", reduced.error().message().c_str());
                return false;
            }
            // Announced with BOTH counts, and in the units each one is
            // actually in. The input is what says the mask was applied first:
            // 13,378 faces is the masked BODY, against 18,486 for the whole
            // mesh including the 138 helper cages.
            //
            // Faces in, triangles out, rather than triangles in: converting
            // would mean re-deriving the decimator's own triangulation rule
            // here -- a quad is two triangles unless corner 3 repeats corner 0
            // -- and a second copy of that rule is a number that goes quietly
            // wrong on a mesh mixing the two.
            std::printf("decimated %zu faces to %zu triangles (%.0f%% asked for)\n",
                        masked->faceCount(), reduced->faceCount(),
                        static_cast<double>(decimateTo) * 100.0);
            lod = std::move(*reduced);
            lod->calcNormals();
            lod->calcVertexTangents();
            lodRm      = mh::core::RenderMesh::build(*lod);
            lodCompact = mh::io::compactUnusedVertices(lodRm->view());

            // The weights survive the reduction by composing three mappings
            // that each already exist:
            //
            //   LOD render vertex --lodRm.vmap()--> LOD mesh vertex
            //                     --lodSource-----> masked mesh vertex
            //                     --maskSource----> display mesh vertex
            //
            // and the display mesh IS the base mesh here, because a subdivided
            // one is refused below for its own reason. Each step is a plain
            // selection, so the composition is one lookup per vertex.
            //
            // What this MEANS is "the survivor keeps its own weights". A
            // collapse of edge (a, b) leaves `a`, so the reduced mesh is
            // weighted as `a` was -- the standard answer, and the only one
            // that needs no rule for two endpoints on different bones.
            for (const uint32_t r : lodRm->vmap()) {
                lodVmap.push_back(maskSource[lodSource[r]]);
            }
        }

        // A decimated mesh CAN carry the rig now: its weights come from the
        // vertices its own survived from. A subdivided one still cannot --
        // its vmap indexes subdivided vertices the weights know nothing about
        // -- and the two together are refused for the subdivision's reason.
        const auto skinData = lod && !subdivided
                                  ? exportSkin(rig, *lodRm, nullptr, lodVmap)
                                  : exportSkin(rig, rm, subdivided ? "subdivided" : nullptr, {});

        // A third of the body's vertex buffer is not referenced by any visible
        // triangle: setFaceMask filters INDICES and leaves the vertex buffer
        // alone, which is right for the renderer and wrong for a file. Measured
        // on the default character: 21,833 vertices, 14,517 referenced, 7,316
        // written for nothing -- and a consumer that bounds the buffer sees the
        // hidden helper cages rather than the body.
        const auto compact = mh::io::compactUnusedVertices(rm.view());
        // Not announced when a LOD is being written: this describes `rm`, the
        // full-resolution mesh, which is not what is about to be exported. The
        // line would name a vertex count no consumer of the file will see.
        if (!lod && compact.dropped() > 0) {
            std::printf("compacted %zu of %zu vertices (%zu unreferenced)\n", compact.coord.size(),
                        compact.remap.size(), compact.dropped());
        }

        // Everything per-vertex below -- the skin, and the blendshape deltas --
        // travels through the SAME compaction the geometry did. Named once
        // rather than chosen three times: the LOD has its own, and mixing the
        // two writes attributes whose vertex count does not match the mesh.
        // glTF catches that and no other format would; it caught both of these
        // while they were being written.
        const mh::io::CompactedMesh& written = lod ? *lodCompact : compact;

        // The skin's joints and weights are per RENDER vertex, so they move
        // with them or every vertex past the first dropped one is weighted to
        // the wrong bone.
        std::vector<uint32_t> joints;
        std::vector<float> weights;
        std::optional<mh::foundation::SkinView> skinView;
        if (skinData) {
            // Against the SAME compaction the geometry went through. The LOD
            // has its own, and using the full mesh's here writes a skin whose
            // vertex count does not match the mesh -- which the glTF writer
            // catches ("skin does not describe this mesh") and every other
            // format would not.
            std::tie(joints, weights) = mh::io::compactSkinAttributes(
                skinData->view(), written.remap, written.coord.size());
            skinView = mh::foundation::SkinView{.jointNames   = skinData->jointNames,
                                                .jointParents = skinData->jointParents,
                                                .globalRest   = skinData->globalRest,
                                                .globalPose   = skinData->globalPose,
                                                .joints       = joints,
                                                .weights      = weights,
                                                .influences   = skinData->influences};
        }

        // Blendshapes: 34 expression units, each blended across the three
        // ethnicities by the character's own macro factors -- NOT the 102 files
        // on disk, which would give a DCC three near-duplicate keys per unit.
        std::vector<mh::core::Blendshape> shapes;
        std::vector<std::vector<mh::foundation::Vec3>> shapeDeltas;
        std::vector<mh::foundation::MorphTarget> morphs;
        if (wantBlendshapes) {
            if (subdivided) {
                // Targets index the BASE mesh; a subdivided vmap indexes
                // subdivided vertices, so expanding them would move the wrong
                // vertices. Same reason the rig is refused above.
                std::fprintf(stderr,
                             "a subdivided mesh cannot carry blendshapes; "
                             "exporting without them\n");
            } else {
                // The same composed mapping the skin uses.
                // `buildExpressionBlendshapes` expands per-BASE-vertex target
                // deltas onto render vertices through a vmap, so handing it the
                // LOD's puts each delta on the vertex it belongs to. The
                // targets still index the base mesh, which is why the count
                // stays `mesh->vertexCount()`.
                shapes = mh::core::buildExpressionBlendshapes(
                    index, human.factors(), lod ? std::span<const uint32_t>(lodVmap) : rm.vmap(),
                    mesh->vertexCount());
                shapeDeltas.reserve(shapes.size());
                morphs.reserve(shapes.size());
                for (auto& sh : shapes) {
                    // Deltas are per render vertex, so they move with the
                    // compaction or every one past the first dropped vertex
                    // lands on the wrong vertex.
                    shapeDeltas.push_back(
                        mh::io::compactDeltas(sh.deltas, written.remap, written.coord.size()));
                    morphs.push_back({sh.name, shapeDeltas.back()});
                }
                std::printf("%zu blendshapes (34 expression units, ethnicity-blended)\n",
                            morphs.size());
            }
        }

        // An EMPTY mask when decimating: the mask is already baked into the
        // geometry, and handing the old one over would index 13,378 faces of a
        // mesh that now has a few thousand.
        // The BASE mesh's topology, not the one being written: a decimated LOD
        // has its own, and the number's job is to say which topology the
        // deltas, weights and correctives in the file are INDEXED AGAINST.
        const mh::foundation::Provenance provenance{.application  = mh::foundation::kVersion,
                                                    .topologyHash = mh::core::topologyHash(*mesh)};
        const bool ok =
            exportMesh(outPath, lod ? *lod : displayMesh(), written.view(), wornProxies,
                       lod ? std::span<const uint8_t>{} : std::span(bodyMask),
                       skinView ? &*skinView : nullptr, rig, provenance, morphs, wantDraco);

        // Put the character back the way it was. The CLI exits straight after
        // this so it never noticed, but File > Export happens with the window
        // open: leaving the body in its REST pose after exporting a live rig
        // would look like the export had un-posed the model.
        if (!posedBackup.empty()) {
            // Cannot fail: it is the vertex array this mesh was carrying a
            // moment ago, so the size already matches.
            (void)mesh->changeCoords(std::move(posedBackup));
            mesh->calcNormals();
            mesh->calcVertexTangents();
            for (auto& [group, worn] : wornProxies)
                refitProxy(worn, *mesh);
            rm.refreshPositions(displayMesh());
        }
        return ok;
    };

    if (parser.isSet(exportOpt)) {
        // Every path from ONE character. A live-rig format swaps the mesh to
        // its rest positions and puts the posed ones back afterwards, so
        // writing several formats in a row only agrees with writing each alone
        // if that restore is exact -- which is what makes it observable at all
        // (`app_restore_matches`).
        for (const QString& path : parser.values(exportOpt)) {
            if (lodRatios.empty()) {
                if (!exportTo(path.toStdString(), parser.isSet(blendshapesOpt), decimateRatio)) {
                    return 1;
                }
                continue;
            }
            // A chain: one file per level, named by the level's INDEX. The
            // suffix goes before the extension so the format still reads from
            // it -- `body_lod2.glb`, not `body.glb_lod2`.
            for (size_t level = 0; level < lodRatios.size(); ++level) {
                std::filesystem::path out = path.toStdString();
                out.replace_filename(out.stem().string() + "_lod" + std::to_string(level) +
                                     out.extension().string());
                // Ratio 1.0 takes the ORDINARY path rather than a decimation
                // that removes nothing: the decimator still triangulates and
                // recompacts, so level 0 would otherwise be the same shape in a
                // different vertex order. This way it is byte-identical to what
                // `--export` alone writes.
                const float ratio = lodRatios[level] >= 1.0F ? 0.0F : lodRatios[level];
                if (!exportTo(out, parser.isSet(blendshapesOpt), ratio)) return 1;
            }
        }
        return 0;
    }

    // The one rebuild path: sliders, pose and skin all go through it, so the
    // three controls cannot disagree about what is on screen.
    //
    // Declared ABOVE `window` and taking it as an argument rather than
    // capturing it. The panels are `window`'s children and their connections
    // reference this lambda, so this must outlive the window -- capturing the
    // window would force the opposite order and leave those connections holding
    // a dangling reference through teardown.
    // Owns the blended skin tone: MeshInstance holds a non-owning span, so the
    // bytes must outlive every render, not just the rebuild that made them.
    std::vector<uint8_t> toneBuf;

    const auto buildScene = [&]() -> std::vector<mh::render::MeshInstance> {
        human.applyStack(*mesh, targets);
        if (!poseInPlace(*mesh, rig)) return {};
        mesh->calcNormals();
        mesh->calcVertexTangents();

        const mh::core::Mesh& shown = displayMesh();
        // refreshPositions keeps the existing buffers only while the topology is
        // the same one they were built from; toggling subdivision changes it.
        if (rm.matches(shown)) {
            rm.refreshPositions(shown);
        } else {
            rm = mh::core::RenderMesh::build(shown);
            // The fresh render mesh has no mask, so force a reapply rather than
            // letting the unchanged-mask shortcut skip it.
            bodyMask.clear();
        }
        if (!applyBodyMask(shown)) {
            // Silently leaving every face visible draws the helper geometry as
            // a solid skirt with no clue why.
            std::fprintf(stderr, "cannot apply the body face mask\n");
        }
        // Worn proxies follow the body: re-fitted against the posed, morphed
        // base mesh every rebuild, not just when they are put on.
        // The diffuse map comes from each thing's own material, not from a
        // single shared one: the body wears the skin `.mhmat`, a proxy wears
        // its own. Empty is normal -- the shipped DefaultSkin names no texture
        // at all (`shaderConfig diffuse false`), so the body is pure matcap
        // until a skin with a real albedo map is installed.
        std::vector<mh::render::MeshInstance> scene;
        // skinMaterialPath(), NOT a hard-coded default.mhmat: the picker and
        // --skin-material chose the material the EXPORTERS wrote and nothing
        // else, so every one of the eight shipped tones rendered as the same
        // untextured body while the .mhm and the .glb said otherwise.
        const ViewportMaps bodyMaps = viewportMapsOf(skinMaterialPath());
        mh::render::MeshInstance body;
        body.mesh               = rm.view();
        body.litsphere          = skin;
        body.diffuse            = bodyMaps.diffuse;
        body.normalMap          = bodyMaps.normal;
        body.normalMapIntensity = bodyMaps.normalMapIntensity;
        body.aoMap              = bodyMaps.ao;
        body.transparent        = bodyMaps.transparent;
        body.metallic           = bodyMaps.metallic;
        body.baseColour         = bodyMaps.baseColour;
        body.opacity            = bodyMaps.opacity;
        body.roughness          = bodyMaps.roughness;

        // The BODY only. A wrinkle sheet is authored in the base mesh's UV
        // space, so handing it to a worn proxy would sample someone else's
        // layout and crease the clothing along whatever happened to be there.
        const mh::rig::WrinkleChoice wrinkle = wrinkleForFrame();
        body.wrinkleMap                      = wrinkle.map;
        body.wrinkleWeight                   = wrinkle.weight;

        // autoBlendSkin: the tone follows the ethnic sliders, so it is a blend
        // of the three ethnic litspheres and has no file behind it. `toneBuf`
        // owns the bytes because MeshInstance's span does not, and the render
        // outlives this scope.
        if (bodyMaps.autoBlendSkin) {
            if (const auto size = blendedSkinTone(human, toneBuf)) {
                body.litsphereRgba   = toneBuf;
                body.litsphereWidth  = size->first;
                body.litsphereHeight = size->second;
            }
        }
        scene.push_back(std::move(body));
        for (auto& [group, worn] : wornProxies) {
            refitProxy(worn, *mesh);
            // Each worn thing carries its own maps, so clothing detail does
            // not inherit the body's.
            const ViewportMaps wornMaps = viewportMapsOf(worn.proxy.materialFile);
            mh::render::MeshInstance inst;
            inst.mesh               = worn.rm.view();
            inst.litsphere          = worn.litsphere;
            inst.diffuse            = wornMaps.diffuse;
            inst.normalMap          = wornMaps.normal;
            inst.normalMapIntensity = wornMaps.normalMapIntensity;
            inst.aoMap              = wornMaps.ao;
            inst.transparent        = wornMaps.transparent;
            inst.metallic           = wornMaps.metallic;
            inst.baseColour         = wornMaps.baseColour;
            inst.opacity            = wornMaps.opacity;
            inst.roughness          = wornMaps.roughness;
            scene.push_back(std::move(inst));
        }
        // Reported from the INSTANCES, not from the materials they came from.
        // An earlier version of this printed `worn.material->transparent`
        // directly and was therefore blind to the bug it was meant to cover:
        // `ViewportMaps::transparent` was never assigned, so the flag never
        // reached `MeshInstance` and every worn thing drew opaque. Reading the
        // same field the renderer reads is the whole point.
        const size_t blended = static_cast<size_t>(
            std::ranges::count_if(scene, [](const auto& m) { return m.transparent; }));
        std::fprintf(stderr, "scene: %zu meshes (%zu blended)\n", scene.size(), blended);
        return scene;
    };

    // ONE render path, two triggers: `--render` and File > Render. Same reason
    // exportTo is shared -- a menu and a command line that build the scene
    // separately quietly stop producing the same picture.
    //
    // Split in two because the two triggers now want different things: the
    // command line wants a file, and the window wants the IMAGE, to show it.
    // The error is a message either way, because both callers do the same thing
    // with it -- put it in front of someone.
    const auto renderImage =
        [&](const mh::ui::RenderRequest& req) -> std::expected<QImage, std::string> {
        auto renderer = mh::render::OffscreenRenderer::create(shaderDir);
        if (!renderer) return std::unexpected(renderer.error().message());

        const mh::render::RenderSettings rs = mh::ui::renderSettingsFor(req, skin);

        const auto scene = buildScene();
        if (scene.empty()) return std::unexpected(std::string{"nothing to draw"});
        auto img = (*renderer)->render(scene, rs);
        if (!img) return std::unexpected(img.error().message());

        // The same guard `--screenshot` has always had. A blank frame saves as
        // a perfectly valid PNG, so without this a production render of nothing
        // writes a file and exits 0 -- which is how a rendering regression
        // passes CI. Reported rather than saved: the caller shows the message.
        std::string stats;
        if (!mh::ui::describeFrame(*img, stats)) return std::unexpected(stats);
        std::printf("%s\n", stats.c_str());
        return std::move(*img);
    };

    const auto renderTo = [&](const std::filesystem::path& out,
                              const mh::ui::RenderRequest& req) -> std::string {
        const auto img = renderImage(req);
        if (!img) return img.error();
        // AFTER renderImage, never before. It is renderImage that calls
        // describeFrame, and describeFrame calls a frame blank when it is a
        // flat fill of the clear colour. A backdrop composited first makes a
        // render that drew NOTHING look like a frame full of photograph, so the
        // guard would pass on exactly the failure it exists to catch.
        const QImage framed = backdrop.isNull() ? *img : mh::ui::overBackground(*img, backdrop);
        if (!framed.save(QString::fromStdString(out.string()))) {
            return "cannot write " + out.string();
        }
        std::printf("rendered %s (%dx%d%s, %s)\n", out.string().c_str(), img->width(),
                    img->height(), req.transparent ? ", transparent" : "",
                    req.shading == mh::render::ShadingModel::Pbr ? "pbr" : "litsphere");
        return {};
    };

    // The window path and the headless production render assemble the SAME
    // scene; only the destination differs. Sharing it is what stops a
    // production render quietly disagreeing with what the viewport shows.
    // The macro numbers travel with the geometry, so they are refreshed at the
    // one place every slider drag already funnels through. Height in particular
    // has to be: it is the MESH's Y extent, not a slider value, so it moves when
    // any modifier does -- including ones that are not "Height".
    const auto rebuildInto = [&](mh::ui::MainWindow& w) {
        w.setMeshes(buildScene());

        mh::ui::MacroStats stats;
        const auto& f  = human.factors();
        stats.gender   = f.gender();
        stats.ageYears = f.ageYears();
        stats.muscle   = f.muscle();
        stats.weight   = f.weight();
        // Mesh::heightCm(), not a bounding box computed here: it masks the
        // helper cages exactly as the reference does, which is worth 2.87 cm on
        // the shipped mesh. Recomputing it here is how the two drift apart.
        stats.heightCm = mesh->heightCm();
        // Measured from the same masked body the height is, so the two agree
        // about what "the figure" is.
        stats.weightKg = mesh->weightKg();
        w.setMacroStatus(mh::ui::macroStatusLine(stats, w.units(), w.weightMode()));
    };

    // Above `window` for the same reason rebuildInto is: the File-menu
    // connections are owned by the window and reference this, so it has to
    // outlive it.
    QString documentPath = parser.value(loadOpt);

    // Undo state, declared ABOVE `window` for the reason stated there: the
    // QUndoStack is the window's child and every command holds a copy of
    // applyModifier, so these must outlive it. `panel` and `shell` are filled
    // in once the window exists.
    int mergeGroup               = 0;
    mh::ui::AssetPanel* assets   = nullptr;
    mh::ui::ModifierPanel* panel = nullptr;
    mh::ui::MainWindow* shell    = nullptr;
    const auto applyModifier     = [&](const QString& id, float value) {
        // No re-entrancy guard: ModifierPanel::setValue blocks the slider's
        // signals, so this cannot come back round through valueChanged. The
        // "setValue moves the slider without emitting" test pins that.
        panel->setValue(id, value);
        human.setModifierValue(id.toStdString(), value);
        // Order matters: the stack resets the mesh to its morph base, so posing
        // has to come after it or the pose is thrown away on every change.
        rebuildInto(*shell);
    };

    // The BATCHED form, for randomisation. Every slider is moved and every
    // modifier set BEFORE the single rebuild at the end -- calling
    // applyModifier 245 times instead would rebuild the mesh 245 times, which
    // is the whole reason MultiValueChangeCommand exists.
    const auto applyModifiers = [&](const std::vector<std::pair<QString, float>>& values) {
        for (const auto& [id, value] : values) {
            panel->setValue(id, value);  // blocks the slider's signals; see applyModifier
            human.setModifierValue(id.toStdString(), value);
        }
        rebuildInto(*shell);
    };

    // Skin and pose go through the undo stack too, so Cmd+Z means the same
    // thing whichever panel the user last touched.
    const auto applyChoice = [&](const QString& group, const QString& id) {
        assets->setChoice(group, id);  // does not emit; see AssetPanel::setChoice
        if (group == QLatin1String("Litsphere")) {
            // Rebuild rather than setLitsphere: the body's material is carried
            // by its MeshInstance now, so changing it without rebuilding would
            // re-upload the old one and render an unchanged picture.
            skin = id.toStdString();
            rebuildInto(*shell);
            return;
        }
        if (group == QLatin1String("Skin material")) {
            // The material, not the matcap: this is what carries the albedo,
            // normal and roughness maps the PBR viewport and every exporter
            // read. Rebuild so the body's MeshInstance picks up the new maps.
            skinMaterialRef() = id.toStdString();
            rebuildInto(*shell);
            return;
        }
        if (group == QLatin1String("Skeleton")) {
            // Same shape as the Pose branch below: try it first, and put the
            // picker back if it fails, so a rig that will not load does not
            // become an undo entry that does nothing.
            //
            // The CURRENT pose is re-applied, not dropped: a user switching rig
            // mid-pose expects to keep the pose. loadPoseRig reads the rig from
            // rigNameRef(), so the name has to be set before the call and put
            // back if it fails.
            const std::string previous = rigNameRef();
            setRigName(id.toStdString());
            PoseRig next;
            if (!loadPoseRig(*mesh, poseChoice, next)) {
                setRigName(previous);
                assets->setChoice(group, QString::fromStdString(previous));
                return;
            }
            rig = std::move(next);
            rebuildInto(*shell);
            return;
        }
        if (group == QLatin1String("Eye colour")) {
            eyeColourRef() = id.toStdString();
            // Re-wear rather than patch: the material is read when the proxy is
            // loaded, so the worn copy has to be rebuilt to pick up the change.
            // Nothing to do when no eyes are worn -- the choice still sticks and
            // applies the moment a pair is put on.
            if (const auto it = wornProxies.find(QStringLiteral("Eyes")); it != wornProxies.end()) {
                const std::string current = it->second.proxy.objFile.parent_path().string();
                if (auto worn = wearProxy(
                        current + "/" + it->second.proxy.objFile.stem().string() + ".mhclo", *mesh,
                        eyeLitsphere(), /*isEyes=*/true)) {
                    wornProxies.insert_or_assign(QStringLiteral("Eyes"), std::move(*worn));
                }
            }
            rebuildInto(*shell);
            return;
        }
        if (const auto slot = std::ranges::find_if(
                kProxySlots, [&](const ProxySlot& s) { return group == QLatin1String(s.group); });
            slot != kProxySlots.end()) {
            // Same lifetime argument as the Eyes branch below: safe only
            // because the erase and the rebuild happen inside one slot.
            if (id == QLatin1String(kNoProxy)) {
                wornProxies.erase(group);
            } else if (auto worn = wearProxy(id.toStdString(), *mesh, slotLitsphere(slot->key))) {
                wornProxies.insert_or_assign(group, std::move(*worn));
            } else {
                wornProxies.erase(group);
            }
            rebuildInto(*shell);
            return;
        }
        if (group == QLatin1String("Eyes")) {
            // Erasing or replacing frees geometry the viewport still holds
            // non-owning spans into. That is safe only because this runs to
            // completion inside one slot: setMeshes below replaces the list
            // before the event loop can repaint, and update() schedules rather
            // than paints. Anything that lets the event loop run between the
            // erase and the rebuild reintroduces a use-after-free.
            if (id == QLatin1String(kNoProxy)) {
                wornProxies.erase(group);
            } else if (auto worn = wearProxy(id.toStdString(), *mesh, eyeLitsphere(),
                                             /*isEyes=*/true)) {
                wornProxies.insert_or_assign(group, std::move(*worn));
            } else {
                // Loading failed and said why. Take the proxy off rather than
                // leaving the picker naming something that is not on screen.
                wornProxies.erase(group);
            }
            rebuildInto(*shell);
            return;
        }
        if (group != QLatin1String("Pose")) return;
        // Back to the morph base FIRST. loadPoseRig fits the skeleton to
        // whatever the mesh currently holds, and the mesh is left posed after
        // every rebuild -- so switching pose to pose was conjugating into the
        // previous pose's rest frame. Measured error: 33 cm maximum.
        human.applyStack(*mesh, targets);
        PoseRig next;
        if (!loadPoseRig(*mesh, id.toStdString(), next)) {
            // applyStack has already reset the mesh to its morph base, so
            // returning here would leave the viewport showing the old pose over
            // an unposed mesh -- three surfaces disagreeing. Rebuild with the
            // rig we still have so at least they agree.
            //
            // The order cannot be swapped: applyStack must precede loadPoseRig,
            // because fitting the skeleton to an already-posed mesh is the 33 cm
            // bug from session 038.
            rebuildInto(*shell);
            return;
        }
        rig = std::move(next);
        // A pose arriving or leaving is what enables the toolbar's Pose toggle.
        // Its STATE is deliberately not touched: someone who switched posing
        // off and then picks a different pose expects it to stay off.
        shell->setPoseAvailable(rig.posed());
        rebuildInto(*shell);
    };

    // What each picker last settled on, so a command knows where to go back to.
    // Seeded after the panel exists; see below.
    QHash<QString, QString> currentChoice;

    // The task registry: which panels exist, in what order. Declared here rather
    // than falling out of a filename, which is how the reference decided it
    // (core/mhmain.py:562).
    // Named once, used for both registration and lookup: setPanel takes a
    // free-form string, so a typo in one place could not otherwise disagree
    // with the other.
    const QString kModelling = QStringLiteral("Modelling");
    const QString kMaterials = QStringLiteral("Materials");

    mh::ui::TaskRegistry tasks;
    // The id stays "Materials" forever -- `saveState` keys on it, so changing
    // it would drop that panel out of every workspace already saved. Only the
    // TITLE changes: the dock holds Skin, Pose, Eyes, Skin material and
    // Skeleton, and three of those are not materials. "Assets" is what our own
    // code calls the widget inside it (`ui::AssetPanel`); the reference has no
    // single name for this set, splitting it across Materials, Geometries and
    // Pose/Animate.
    if (!tasks.add(kModelling) || !tasks.add(kMaterials, QStringLiteral("Assets"))) {
        std::fprintf(stderr, "duplicate task category\n");
        return 1;
    }

    mh::ui::MainWindow window(parser.value(shaderOpt).toStdString(), tasks);

    // The 20 shipped `data/languages/*.json` files, offered live. `mh_ui` has
    // no data path of its own, so the directory comes from here.
    window.setLanguageChoices(dataDir(), mh::ui::availableLanguages(dataDir()));
    if (parser.isSet(languageOpt)) {
        const QString lang = parser.value(languageOpt);
        if (!window.setLanguage(dataDir(), lang)) {
            std::fprintf(
                stderr, "unknown --language %s; available: %s\n", lang.toLocal8Bit().constData(),
                mh::ui::availableLanguages(dataDir()).join(", ").toLocal8Bit().constData());
            return 1;
        }
    }

    // rm outlives the window, so the non-owning view stays valid.
    window.setMesh(rm.view());

    // The directive is about the VIEWPORT, so the window honours --shading as
    // well as --render does; the two share buildScene() and now share the
    // shading model, which is what stops the on-screen image and a production
    // render disagreeing.
    window.viewport()->setShadingModel(shading);
    shell = &window;

    // Skinning. The stored preference is the default and `--skinning` wins for
    // the run, because a flag typed now is a decision about now; the menu is
    // told either way, so it never claims linear while the run is dual.
    //
    // The preference is a WINDOW preference: a headless `--export` builds no
    // window, does not read it, and takes `--skinning` alone. Recorded in
    // memory/todo.md rather than hidden.
    if (parser.isSet(skinningOpt)) {
        window.setSkinning(gUseDualQuaternion ? mh::ui::Skinning::DualQuaternion
                                              : mh::ui::Skinning::Linear);
    } else {
        gUseDualQuaternion = window.skinning() == mh::ui::Skinning::DualQuaternion;
    }
    // Re-posing live is the whole point, and it costs nothing extra: buildScene
    // resets the mesh to its morph base with applyStack before posing it
    // (src/app/main.cpp, buildScene), so the second method is applied to the
    // REST mesh rather than on top of the first one's result.
    QObject::connect(&window, &mh::ui::MainWindow::skinningChanged, [&](mh::ui::Skinning method) {
        gUseDualQuaternion = method == mh::ui::Skinning::DualQuaternion;
        rebuildInto(window);
    });

    // Smooth. `--subdivide` and a `.mhm` that carries one both arrive as
    // `subdivided` before the window exists, so the button is told rather than
    // asked -- a tick that disagreed with the body on screen would be worse
    // than no button. `displayMesh` reads this flag on every rebuild.
    window.setSmooth(subdivided);
    QObject::connect(&window, &mh::ui::MainWindow::smoothChanged, [&](bool on) {
        subdivided = on;
        rebuildInto(window);
    });

    // Wireframe. A view mode, so it needs no rebuild at all -- the geometry is
    // unchanged and only the pipeline differs. `update()` inside
    // ViewportWidget::setWireframe is the whole cost.
    if (parser.isSet(wireframeOpt)) {
        window.viewport()->setWireframe(true);
        window.setWireframe(true);
    }
    // The pose toggle. Availability, not state: the button starts checked and
    // greys out when there is nothing to un-pose.
    window.setPoseAvailable(rig.posed());
    QObject::connect(&window, &mh::ui::MainWindow::poseEnabledChanged, [&](bool on) {
        gApplyPose = on;
        rebuildInto(window);
    });

    // The grid. A view mode like wireframe: no rebuild, only a repaint.
    if (parser.isSet(gridOpt)) {
        window.viewport()->setGrid(true);
        window.setGrid(true);
    }
    QObject::connect(&window, &mh::ui::MainWindow::gridChanged,
                     [&](bool on) { window.viewport()->setGrid(on); });

    QObject::connect(&window, &mh::ui::MainWindow::wireframeChanged, [&](bool on) {
        window.viewport()->setWireframe(on);
        // Refused rather than pretended. Without this the button ticks, the
        // body stays solid, and the user is left to guess -- which is exactly
        // the painted no-op the rest of that toolbar group is waiting to avoid.
        // Asked only when turning it ON, and only after a frame exists: the
        // answer comes from a pipeline built with the scene.
        if (on && !window.viewport()->wireframeSupported()) {
            std::fprintf(stderr, "this device cannot draw non-filled polygons\n");
            window.viewport()->setWireframe(false);
            window.setWireframe(false);
            window.statusBar()->showMessage(QObject::tr("This device cannot draw a wireframe"),
                                            4000);
        }
    });
    panel = new mh::ui::ModifierPanel(views);
    for (const auto& [id, v] : presets)
        panel->setValue(id, v);
    if (!window.setPanel(kModelling, panel)) {
        std::fprintf(stderr, "no dock for %s\n", kModelling.toStdString().c_str());
        return 1;
    }

    // Closes the merge group, so a drag is one undo step but two deliberate
    // nudges of the same slider are two.
    QObject::connect(panel, &mh::ui::ModifierPanel::editingFinished, [&] { ++mergeGroup; });

    // Reset touches every slider; a macro makes that one Ctrl+Z instead of 291.
    QObject::connect(panel, &mh::ui::ModifierPanel::resetInProgress, [&](bool active) {
        if (active) {
            window.undoStack()->beginMacro(QObject::tr("Reset all sliders"));
        } else {
            window.undoStack()->endMacro();
            ++mergeGroup;
        }
    });

    QObject::connect(
        panel, &mh::ui::ModifierPanel::valueChanged, [&](const QString& id, float value) {
            const float before = human.modifierValue(id.toStdString());
            if (before == value) return;  // nothing to record

            // Symmetry mode mirrors the edit onto the opposite side as it
            // happens. `mirroredEdit` returns just the edit when there is no
            // opposite, so the branch below is about which COMMAND to push --
            // one value or several -- not about whether to mirror.
            const auto edit = window.symmetryMode()
                                  ? mh::core::mirroredEdit(human, id.toStdString(), value)
                                  : std::vector<mh::core::ModifierEdit>{};
            if (edit.size() < 2) {
                window.undoStack()->push(
                    new mh::ui::ValueChangeCommand(id, before, value, mergeGroup, applyModifier));
                return;
            }

            std::vector<mh::ui::MultiValueChangeCommand::Change> changes;
            changes.reserve(edit.size());
            // Every value comes from `mirroredEdit`, including what each modifier
            // is REPLACING. This used to read the "before" off the panel, where
            // the dragged slider has already moved -- so undo restored the edit
            // instead of reversing it. There is nothing left here to get wrong.
            for (const mh::core::ModifierEdit& e : edit) {
                changes.push_back({QString::fromStdString(e.fullName), e.before, e.after});
            }
            // The SAME merge group the single-sided path uses, so a drag is one
            // undo entry either way and switching the mode mid-session does not
            // change how undo behaves.
            window.undoStack()->push(new mh::ui::MultiValueChangeCommand(
                QObject::tr("Symmetric edit"), std::move(changes), applyModifiers, mergeGroup));
        });

    // The Materials dock: skin and pose. Both re-run the same rebuild the
    // sliders do, so the three controls cannot disagree about what is shown.
    assets = new mh::ui::AssetPanel(assetGroups);
    if (!window.setPanel(kMaterials, assets)) {
        std::fprintf(stderr, "no dock for %s\n", kMaterials.toStdString().c_str());
        return 1;
    }
    for (const auto& group : assetGroups) {
        const QString name = QString::fromStdString(group.name);
        currentChoice.insert(name, assets->choice(name));
    }
    // Taken from the picker, so the viewport and the panel cannot start out
    // disagreeing about which skin is shown.
    const QString chosenSkin = assets->choice(QStringLiteral("Litsphere"));
    if (chosenSkin.isEmpty()) {
        std::fprintf(stderr,
                     "no litspheres found in %s -- the viewport cannot shade anything "
                     "without one\n",
                     (dataDir() / "litspheres").string().c_str());
        return 1;
    }
    skin = chosenSkin.toStdString();

    rebuildInto(window);

    QObject::connect(
        assets, &mh::ui::AssetPanel::chosen, [&](const QString& group, const QString& id) {
            // No `before == id` guard: QComboBox only emits on an
            // actual index change and currentChoice mirrors it, so
            // they cannot be equal. And no insert here -- push()
            // runs redo() synchronously, and the callback below
            // writes the map. Two writers would let one go stale.
            const QString before = currentChoice.value(group);

            // A pose that will not load must not become an undo
            // entry that does nothing. Try it first; on failure put
            // the picker back and record nothing.
            if (group == QLatin1String("Pose") && id != QLatin1String("rest")) {
                PoseRig probe;
                if (!loadPoseRig(*mesh, id.toStdString(), probe)) {
                    assets->setChoice(group, before);
                    return;
                }
            }

            window.undoStack()->push(new mh::ui::ChoiceChangeCommand(
                group, before, id, mergeGroup, [&](const QString& g, const QString& value) {
                    currentChoice.insert(g, value);
                    applyChoice(g, value);
                }));
        });

    window.setDocumentPath(documentPath);

    const auto applyLoaded = [&](const QString& file) {
        const auto loaded = mh::core::loadMhm(file.toStdString());
        if (!loaded) {
            QMessageBox::warning(&window, QObject::tr("Cannot open"),
                                 QString::fromStdString(loaded.error().message()));
            return;
        }
        // Reset first (human.py:1486). Without it a modifier the new file does
        // not mention keeps the previous character's value, and what loads is a
        // blend of the two.
        human.resetToDefaults();
        mh::core::applyMhm(*loaded, human, nullptr);
        document = *loaded;

        // Sync the panel from `human`, not from the file: a slider the file
        // omits has just been reset, and walking only the file's own lines
        // would leave it showing the previous character's value.
        for (const mh::core::Modifier& m : human.modifiers()) {
            panel->setValue(QString::fromStdString(m.fullName), human.modifierValue(m.fullName));
        }
        if (loaded->hasCamera) {
            const mh::core::OrbitView view = mh::core::orbitFromMhmCamera(loaded->camera);
            mh::render::Camera c           = window.viewport()->camera();
            // Clamped to the same limits the mouse obeys. MakeHuman's maximum
            // zoomFactor of 15 maps to a distance of 3 -- inside the head --
            // and setCamera does no clamping of its own.
            c.pitchDegrees =
                std::clamp(view.pitchDegrees, -mh::ui::ViewportWidget::kMaxPitchDegrees,
                           mh::ui::ViewportWidget::kMaxPitchDegrees);
            c.yawDegrees = view.yawDegrees;
            c.distance   = std::clamp(view.distance, mh::ui::ViewportWidget::kMinDistance,
                                      mh::ui::ViewportWidget::kMaxDistance);
            applyPan(c, view, halfExtents(*mesh));
            window.viewport()->setCamera(c);
        }
        subdivided = loaded->subdivide;
        // The file decides, so the button has to follow it. Without this,
        // opening a subdivided character leaves Smooth reading "off".
        window.setSmooth(subdivided);

        // The history belongs to the document that produced it. Kept, Ctrl+Z
        // would write the previous character's values into this one.
        window.undoStack()->clear();
        mergeGroup = 0;

        documentPath = file;
        window.setDocumentPath(file);
        rebuildInto(window);
    };

    const auto writeTo = [&](const QString& file) {
        // What the user is looking at, so Save records the framing.
        const mh::render::Camera c = window.viewport()->camera();
        const mh::core::MhmFile doc =
            documentFor(human, document, std::filesystem::path(file.toStdString()),
                        mh::core::OrbitView{c.pitchDegrees, c.yawDegrees, c.distance,
                                            panToTranslation(c, halfExtents(*mesh))},
                        subdivided);

        if (const auto ok = mh::core::saveMhm(file.toStdString(), doc); !ok) {
            QMessageBox::warning(&window, QObject::tr("Cannot save"),
                                 QString::fromStdString(ok.error().message()));
            return;
        }
        documentPath = file;
        window.setDocumentPath(file);
    };

    const QString filter = QObject::tr("MakeHuman character (*.mhm)");
    QObject::connect(&window, &mh::ui::MainWindow::openRequested, [&] {
        const QString file =
            QFileDialog::getOpenFileName(&window, QObject::tr("Open character"), {}, filter);
        if (!file.isEmpty()) applyLoaded(file);
    });
    QObject::connect(&window, &mh::ui::MainWindow::saveAsRequested, [&] {
        const QString file =
            QFileDialog::getSaveFileName(&window, QObject::tr("Save character"), {}, filter);
        if (!file.isEmpty()) writeTo(file);
    });
    // The toolbar's "grab screen". window.grab() rather than the viewport's own
    // framebuffer, for the same reason --screenshot saves that one: the chrome
    // is half of what a screenshot is for. The viewport's error is checked
    // first, because saving a blank PNG and reporting success is worse than
    // saying nothing.
    QObject::connect(&window, &mh::ui::MainWindow::screenshotRequested, [&] {
        const QString file = QFileDialog::getSaveFileName(&window, QObject::tr("Grab screen"), {},
                                                          QObject::tr("PNG image (*.png)"));
        if (file.isEmpty()) return;
        const QString err = window.viewport()->lastError();
        if (!err.isEmpty()) {
            window.statusBar()->showMessage(QObject::tr("Cannot grab: %1").arg(err), 4000);
            return;
        }
        if (!window.grab().save(file)) {
            window.statusBar()->showMessage(QObject::tr("Could not write %1").arg(file), 4000);
            return;
        }
        window.statusBar()->showMessage(QObject::tr("Wrote %1").arg(file), 3000);
    });
    // The SAME exportTo the command line uses. The filter lists exactly the
    // extensions exportMesh dispatches on, so a user cannot pick a format the
    // writer will then refuse.
    QObject::connect(&window, &mh::ui::MainWindow::exportRequested, [&] {
        const QString file = QFileDialog::getSaveFileName(
            &window, QObject::tr("Export character"), {},
            QObject::tr("glTF binary (*.glb);;Wavefront OBJ (*.obj);;"
                        "USD (*.usd *.usda *.usdz);;FBX (*.fbx);;Collada (*.dae)"));
        if (file.isEmpty()) return;
        const std::filesystem::path out = file.toStdString();
        if (exportTo(out, parser.isSet(blendshapesOpt), decimateRatio)) {
            window.statusBar()->showMessage(QObject::tr("Exported %1").arg(file), 3000);
            // The live rig restore inside exportTo moved the mesh back, so the
            // viewport has to be told: it holds spans over those vertices.
            rebuildInto(window);
        } else {
            window.statusBar()->showMessage(QObject::tr("Could not export %1").arg(file), 4000);
        }
    });
    // The SAME renderTo the command line uses. The dialog's defaults are
    // --render's defaults, so pressing Enter twice produces exactly what the
    // command line would.
    // Changing units reformats the line from the numbers already computed --
    // no rebuild, because nothing about the character changed.
    QObject::connect(&window, &mh::ui::MainWindow::unitsChanged,
                     [&](mh::ui::Units) { rebuildInto(window); });

    // Randomise. A fresh seed each time, from the same clock the user's other
    // non-repeatable choices come from -- `--random <seed>` is where
    // reproducibility lives, and a button that always produced the same person
    // would be useless.
    QObject::connect(&window, &mh::ui::MainWindow::randomiseRequested, [&] {
        const uint64_t seed =
            static_cast<uint64_t>(std::chrono::steady_clock::now().time_since_epoch().count());

        // Read the BEFORE values first: randomize() mutates `human` in place,
        // so asking afterwards would record the new value as the old one and
        // undo would do nothing.
        const auto changed = mh::core::randomize(human, mh::core::RandomOptions{}, seed);
        std::vector<mh::ui::MultiValueChangeCommand::Change> changes;
        changes.reserve(changed.size());
        for (const auto& [name, value] : changed) {
            const QString key = QString::fromStdString(name);
            changes.push_back({key, panel->value(key), value});
        }
        if (changes.empty()) return;

        // push() runs redo() synchronously, which applies the whole batch and
        // rebuilds once. `human` is already randomised, so redo re-setting the
        // same values is a no-op on it -- and the panel and the mesh need it.
        window.undoStack()->push(new mh::ui::MultiValueChangeCommand(
            QObject::tr("Randomise"), std::move(changes), applyModifiers));
        window.statusBar()->showMessage(QObject::tr("Randomised %1 modifiers").arg(changed.size()),
                                        3000);
    });

    // Symmetry. Same shape as Randomise and for the same reason: it moves many
    // sliders at once, so it is ONE undo step and one rebuild rather than 61.
    QObject::connect(&window, &mh::ui::MainWindow::symmetryRequested, [&](char targetSide) {
        // BEFORE values first, as above: symmetrise mutates `human` in place.
        std::vector<mh::ui::MultiValueChangeCommand::Change> changes;
        for (const auto& [name, value] : mh::core::symmetrise(human, targetSide)) {
            const QString key = QString::fromStdString(name);
            changes.push_back({key, panel->value(key), value});
        }
        if (changes.empty()) {
            // Not silence: an already symmetric character is the common case
            // for a fresh model, and a menu item that appears to do nothing is
            // indistinguishable from one that is broken.
            window.statusBar()->showMessage(QObject::tr("Already symmetric"), 3000);
            return;
        }
        // Counted BEFORE the move: `changes` is empty afterwards, and the
        // message would read "Mirrored 0 modifiers" every time.
        const auto mirrored = static_cast<qsizetype>(changes.size());
        window.undoStack()->push(new mh::ui::MultiValueChangeCommand(
            targetSide == 'r' ? QObject::tr("Symmetry Left \u2192 Right")
                              : QObject::tr("Symmetry Right \u2192 Left"),
            std::move(changes), applyModifiers));
        window.statusBar()->showMessage(QObject::tr("Mirrored %1 modifiers").arg(mirrored), 3000);
    });

    // The finished render is SHOWN, not filed. It used to demand a path before
    // rendering and then report the write in the status bar, so the one thing a
    // render is for -- looking at it -- meant leaving the application. The
    // reference hands its image to a viewer task and switches to it
    // (`legacy/python/plugins/4_rendering_opengl/mh2opengl.py:122-123`); this is
    // that, as a window, because our shell keeps a viewport in the middle
    // rather than a tab stack. Saving moved into the viewer, where the user can
    // decide after seeing the result.
    //
    // Parented to the window so it closes with it, `Qt::Window` so it is a
    // window rather than a child pasted over the viewport.
    auto* renderViewer = new mh::ui::ImageViewer(&window);
    renderViewer->setWindowFlag(Qt::Window);
    QObject::connect(&window, &mh::ui::MainWindow::renderRequested, [&, renderViewer] {
        mh::ui::RenderRequest req;
        req.shading = shading;  // whatever the viewport is currently showing
        mh::ui::RenderDialog dlg(req, &window);
        if (dlg.exec() != QDialog::Accepted) return;

        const auto img = renderImage(dlg.request());
        if (!img) {
            window.statusBar()->showMessage(
                QObject::tr("Cannot render: %1").arg(QString::fromStdString(img.error())), 4000);
            return;
        }
        renderViewer->setImage(*img);
        renderViewer->show();
        renderViewer->raise();
        renderViewer->activateWindow();
        window.statusBar()->showMessage(
            QObject::tr("Rendered %1 × %2").arg(img->width()).arg(img->height()), 3000);
    });
    QObject::connect(&window, &mh::ui::MainWindow::saveRequested, [&] {
        // Save with no path yet is Save As -- silently writing somewhere the
        // user did not choose is worse than asking.
        if (documentPath.isEmpty()) {
            emit window.saveAsRequested();
            return;
        }
        writeTo(documentPath);
    });

    // Before restoreWorkspace and before show(): the menu is built in the
    // constructor with the legacy labels, and this is what makes the profile
    // visible in the half of the application a user actually reads.
    if (workspaceNames()) {
        const QStringList shown = window.setWorkspaceNames(*workspaceNames(), namingProfile);
        // The NAMES, not a count: it tells the user exactly what the Workspace
        // menu will read, and it is the only thing here a mutation cannot fake
        // -- "5 workspace names" passed on a hardcoded 5.
        std::printf("naming: %s profile (%s)\n", namingName.toStdString().c_str(),
                    shown.join(QStringLiteral(", ")).toStdString().c_str());
    }

    window.restoreWorkspace();
    // After restoreWorkspace, so an explicit preset wins over the saved layout.
    if (parser.isSet(workspaceOpt)) {
        const QString name = parser.value(workspaceOpt);
        // Through the name table, so `--workspace Assets` and
        // `--workspace Materials` both reach the same preset whatever the
        // profile -- and so the preset a user names is resolved in exactly one
        // place rather than compared against a list of display strings.
        const QString resolved = resolveWorkspaceName(namingProfile, name);
        if (!window.applyWorkspacePreset(resolved)) {
            std::fprintf(stderr, "no such workspace preset: \"%s\"\n", name.toStdString().c_str());
            return 1;
        }
    }
    window.show();

    // Headless production render. Deliberately BEFORE the window path: it needs
    // a GPU but no surface, so it works where --screenshot cannot -- and it
    // renders the same scene the viewport would, via buildScene().

    if (parser.isSet(renderOpt)) {
        // The CLI's own defaults, unchanged: 1024 square, and whatever
        // --transparent and --shading said.
        const mh::ui::RenderRequest req{
            .width       = 1024,
            .height      = 1024,
            .transparent = parser.isSet(transparentOpt) || parser.isSet(backgroundOpt),
            .shading     = shading,
            .wireframe   = parser.isSet(wireframeOpt)};
        if (const std::string err = renderTo(parser.value(renderOpt).toStdString(), req);
            !err.empty()) {
            std::fprintf(stderr, "cannot render: %s\n", err.c_str());
            return 1;
        }
        return 0;
    }

    if (parser.isSet(shotOpt)) {
        const QString out = parser.value(shotOpt);
        // Let the widget initialise its RHI and draw before grabbing, then quit.
        QTimer::singleShot(600, &app, [&app, &window, out] {
            // Two grabs, deliberately. grabFramebuffer is the viewport's own
            // output and is what the blank-frame guard must judge -- on a
            // platform with no RHI, window.grab() returns chrome over a hole
            // and would pass a blank check. window.grab() is what gets saved,
            // because the chrome is half of what a screenshot is for.
            const QImage frame = window.viewport()->grabFramebuffer();
            const QPixmap shot = window.grab();

            // Errors first: reporting success and then contradicting it makes
            // the tool useless as a check.
            const QString err = window.viewport()->lastError();
            if (!err.isEmpty()) {
                std::fprintf(stderr, "viewport error: %s\n", err.toStdString().c_str());
                app.exit(2);
                return;
            }
            std::string stats;
            const bool drew = mh::ui::describeFrame(frame, stats);
            std::fprintf(drew ? stdout : stderr, "%s\n", stats.c_str());
            if (!drew) {
                // A blank frame saves as a perfectly valid PNG. Exiting 0 here
                // is how a rendering regression passes CI.
                app.exit(3);
                return;
            }
            if (!shot.save(out)) {
                std::fprintf(stderr, "could not write %s\n", out.toStdString().c_str());
                app.exit(1);
                return;
            }
            std::printf("wrote %s (%dx%d)\n", out.toStdString().c_str(), shot.width(),
                        shot.height());
            app.quit();
        });
    }

    const int rc = app.exec();
    // Neither a screenshot run nor an explicit --workspace is a session: both
    // would overwrite the layout the user arranged by hand, and `--workspace
    // Export` hides every dock, so the next plain launch would come up empty.
    if (!parser.isSet(shotOpt) && !parser.isSet(workspaceOpt)) window.saveWorkspace();
    return rc;
}
