// SPDX-License-Identifier: AGPL-3.0-or-later
//
// The application. This is the AGPL side: it loads the mesh through mh::core
// and hands the UI a plain view, which is what lets mh_ui and mh_render stay
// Apache-2.0.
#include "makehuman/core/Material.h"
#include "makehuman/core/Mesh.h"
#include "makehuman/core/Mhm.h"
#include "makehuman/core/ObjReader.h"
#include "makehuman/core/Proxy.h"
#include "makehuman/core/RenderMesh.h"
#include "makehuman/core/SliderLayout.h"
#include "makehuman/core/Subdivider.h"
#include "makehuman/core/Target.h"
#include "makehuman/core/TargetIndex.h"
#include "makehuman/io/GltfWriter.h"
#include "makehuman/io/ObjWriter.h"
#include "makehuman/io/SceneIO.h"
#include "makehuman/io/UsdWriter.h"
#include "makehuman/rig/PoseUnits.h"
#include "makehuman/rig/Skeleton.h"
#include "makehuman/rig/Skinning.h"
#include "makehuman/rig/VertexWeights.h"
#include "makehuman/ui/AssetPanel.h"
#include "makehuman/ui/MainWindow.h"
#include "makehuman/ui/ModifierPanel.h"
#include "makehuman/ui/TaskRegistry.h"
#include "makehuman/ui/Theme.h"
#include "makehuman/ui/UndoCommands.h"

#include <QApplication>
#include <QCommandLineParser>
#include <QFileDialog>
#include <QFont>
#include <QHash>
#include <QMessageBox>
#include <QPixmap>
#include <QTimer>
#include <QUndoStack>
#include "makehuman/ui/ViewportWidget.h"

#include <QImage>

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <map>
#include <optional>
#include <ranges>
#include <span>
#include <string>
#include <string_view>

namespace {

/// Pixel statistics for the rendered frame. A window that came up but drew
/// nothing still saves a perfectly valid PNG, so "it ran" is not evidence --
/// coverage and spread are.
/// Returns false when the frame carries no drawn geometry at all.
bool describe(const QImage& img, std::string& out) {
    if (img.isNull()) {
        out = "empty frame -- nothing was rendered";
        return false;
    }
    const QImage rgb      = img.convertToFormat(QImage::Format_RGB32);
    const QRgb bg         = rgb.pixel(0, 0);
    const long long total = static_cast<long long>(rgb.width()) * rgb.height();
    long long covered     = 0;
    double sum            = 0.0;
    int lo = 255, hi = 0;
    for (int y = 0; y < rgb.height(); ++y) {
        for (int x = 0; x < rgb.width(); ++x) {
            const QRgb p = rgb.pixel(x, y);
            if (p == bg) continue;
            ++covered;
            const int l = qGray(p);
            sum += l;
            lo = std::min(lo, l);
            hi = std::max(hi, l);
        }
    }
    char buf[256];
    if (covered == 0) {
        std::snprintf(buf, sizeof buf, "%dx%d frame is a flat fill -- nothing drew", rgb.width(),
                      rgb.height());
        out = buf;
        return false;
    }
    std::snprintf(buf, sizeof buf,
                  "%dx%d covered %lld of %lld pixels (%.1f%%) luminance min=%d max=%d mean=%.1f",
                  rgb.width(), rgb.height(), covered, total,
                  100.0 * static_cast<double>(covered) / static_cast<double>(total), lo, hi,
                  sum / static_cast<double>(covered));
    out = buf;
    return true;
}

/// Everything needed to re-pose a morphed mesh.
///
/// Held together because the rig has to be re-fitted after each morph:
/// `updateJoints` makes the skeleton follow the body, and skinning a changed
/// mesh with a stale rig rotates it about joints that have moved.
struct PoseRig {
    mh::rig::Skeleton skeleton;
    mh::rig::CompiledWeights weights;
    std::vector<mh::foundation::Mat4> localPose;
    bool active{false};
};

/// Loads the rig and pose named by @p pose, or leaves @p out inactive.
///
/// "A-pose" is not a file: the MakeHuman base mesh is authored in one, so the
/// rest mesh IS the A-pose and posing it would be posing it twice. Only a pose
/// that differs from the authored rest needs a BVH.
bool loadPoseRig(const mh::core::Mesh& mesh, const std::string& pose, PoseRig& out) {
    if (pose == "rest" || pose == "apose" || pose == "a-pose") return true;

    std::filesystem::path file = pose;
    if (pose == "tpose" || pose == "t-pose")
        file = std::filesystem::path(MH_DATA_DIR) / "poses" / "tpose.bvh";

    auto skel =
        mh::rig::loadSkeleton(std::filesystem::path(MH_DATA_DIR) / "rigs" / "default.mhskel");
    if (!skel) {
        std::fprintf(stderr, "cannot load the rig: %s\n", skel.error().message().c_str());
        return false;
    }
    if (!skel->updateJoints(mesh.coord()) || !skel->buildRestMatrices()) {
        std::fprintf(stderr, "cannot build the rest pose\n");
        return false;
    }

    auto weights = mh::rig::loadWeights(
        std::filesystem::path(MH_DATA_DIR) / "rigs" / "default_weights.mhw", mesh.vertexCount());
    if (!weights) {
        std::fprintf(stderr, "cannot load weights: %s\n", weights.error().message().c_str());
        return false;
    }

    const auto bodyPose = mh::rig::loadBodyPose(file, *skel);
    if (!bodyPose) {
        std::fprintf(stderr, "cannot load pose: %s\n", bodyPose.error().message().c_str());
        return false;
    }

    // The file's rotations are in model space; skinning wants them in each
    // bone's rest frame. Skipping this yields a plausible but wrong pose.
    out.localPose = mh::rig::poseToBoneLocal(*skel, *bodyPose);
    out.weights   = weights->compile(*skel, 4);
    out.skeleton  = std::move(*skel);
    out.active    = true;
    return true;
}

/// Applies @p rig's pose to @p mesh in place. A no-op when no pose is loaded.
bool poseInPlace(mh::core::Mesh& mesh, PoseRig& rig) {
    if (!rig.active) return true;

    if (!rig.skeleton.updateJoints(mesh.coord()) || !rig.skeleton.buildRestMatrices()) {
        std::fprintf(stderr, "cannot re-fit the rig to the morphed mesh\n");
        return false;
    }
    const auto skinning = mh::rig::computeSkinningMatrices(rig.skeleton, rig.localPose);

    std::vector<mh::foundation::Vec3> posed;
    if (!mh::rig::skinPositions(mesh.coord(), rig.weights, skinning, posed)) {
        std::fprintf(stderr, "skinning failed\n");
        return false;
    }
    if (const auto ok = mesh.setCoords(std::move(posed)); !ok) {
        std::fprintf(stderr, "cannot store the posed mesh (MeshError %d)\n",
                     static_cast<int>(ok.error()));
        return false;
    }
    return true;
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
std::string prettyName(const std::filesystem::path& file, std::string_view prefix) {
    std::string stem = file.stem().string();
    if (stem.starts_with(prefix)) stem = stem.substr(prefix.size());
    if (!stem.empty()) {
        stem[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(stem[0])));
    }
    return stem;
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
    return std::filesystem::path(MH_DATA_DIR) / "litspheres" / "skinmat_eye.png";
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

/// Loads @p path and fits it to @p body once, so the caller gets something
/// renderable or nothing.
std::optional<WornProxy> wearProxy(const std::filesystem::path& path, const mh::core::Mesh& body,
                                   std::filesystem::path litsphere) {
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
    std::fprintf(stderr, "wearing %s (%zu verts)\n", worn.proxy.name.c_str(),
                 worn.proxy.vertexCount());
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
    if (!worn.mesh.setCoords(std::move(fitted))) return;
    worn.mesh.calcNormals();
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
std::vector<mh::foundation::AssetGroup> buildAssetGroups(const std::string& currentPose,
                                                         const std::string& currentSkin,
                                                         const std::string& currentEyes) {
    namespace fs = std::filesystem;
    std::vector<mh::foundation::AssetGroup> groups;

    mh::foundation::AssetGroup skins;
    skins.name      = "Skin";
    int defaultSkin = -1;
    for (const fs::path& p : filesWithExtension(fs::path(MH_DATA_DIR) / "litspheres", ".png")) {
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
    for (const fs::path& p : filesWithExtension(fs::path(MH_DATA_DIR) / "poses", ".bvh")) {
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
    for (const fs::path& p : filesWithExtension(fs::path(MH_DATA_DIR) / "eyes", ".mhclo")) {
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
    // This renderer has no camera pan, so mhmCameraFrom writes zeros there.
    // Carrying the loaded translation forward keeps a value we cannot represent
    // rather than silently flattening it to the origin.
    if (base.hasCamera) std::copy_n(base.camera.begin() + 2, 3, out.camera.begin() + 2);
    return out;
}

/// Writes the mesh in whichever format @p path's extension names.
/// The body's own material, for export. The viewport shades with a litsphere,
/// which carries no PBR data at all, so a `.mhmat` is what a DCC tool gets.
std::optional<mh::foundation::MaterialDesc> bodyMaterial() {
    const auto path = std::filesystem::path(MH_DATA_DIR) / "skins" / "default.mhmat";
    if (auto mat = mh::core::loadMaterial(path)) return mat->desc();
    std::fprintf(stderr, "cannot load %s; exporting without a body material\n",
                 path.string().c_str());
    return std::nullopt;
}

/// @param worn proxies to include. OBJ writes them as extra groups; the other
///        formats are still single-mesh, so they say what they are leaving out
///        rather than quietly exporting a dressed character naked.
bool exportMesh(const std::filesystem::path& path, const mh::core::Mesh& mesh,
                const mh::core::RenderMesh& rm, const std::map<QString, WornProxy>& worn) {
    std::string ext = path.extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

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
    const auto skin       = bodyMaterial();
    const bool allDressed = skin.has_value() && std::ranges::all_of(worn, [](const auto& kv) {
                                return kv.second.material.has_value();
                            });
    if (!allDressed && (skin.has_value() || !worn.empty())) {
        std::fprintf(stderr, "exporting without materials: %s\n",
                     skin.has_value() ? "something worn has none, and a partly-materialled scene "
                                        "cannot be written"
                                      : "the body skin could not be loaded");
    }

    if (ext == ".obj") {
        std::vector<mh::io::ObjSceneEntry> scene;
        scene.push_back({mesh.view(), "body", allDressed ? &*skin : nullptr, {}});
        for (const auto& [group, proxy] : worn) {
            scene.push_back({proxy.mesh.view(),
                             group.toLower().toStdString(),
                             allDressed ? &*proxy.material : nullptr,
                             {}});
        }
        const auto r = mh::io::writeObjScene(path, scene);
        return report(r ? std::string{} : r.error().message());
    }

    // Said plainly, and only for a format we are actually going to write.
    // Silently dropping what the character is wearing is the failure this whole
    // change exists to fix; announcing it for an extension we then reject would
    // just be noise before an error.
    static constexpr std::array kSingleMeshFormats{".usda", ".usd", ".fbx", ".dae", ".stl", ".3mf"};
    if (!worn.empty() && std::ranges::find(kSingleMeshFormats, ext) != kSingleMeshFormats.end()) {
        std::fprintf(stderr,
                     "note: %s exports the body only; %zu worn item(s) omitted "
                     "(multi-mesh is implemented for .obj and .glb so far)\n",
                     ext.c_str(), worn.size());
    }
    if (ext == ".usda" || ext == ".usd") {
        const auto r = mh::io::writeUsda(path, rm.view());
        return report(r ? std::string{} : r.error().message());
    }
    if (ext == ".glb") {
        std::vector<mh::io::GltfSceneEntry> scene;
        scene.push_back({rm.view(), "body", allDressed ? &*skin : nullptr});
        for (const auto& [group, proxy] : worn) {
            scene.push_back({proxy.rm.view(), group.toLower().toStdString(),
                             allDressed ? &*proxy.material : nullptr});
        }
        const auto r = mh::io::writeGlbScene(path, scene);
        return report(r ? std::string{} : r.error().message());
    }

    // PLY is deliberately absent -- assimp 6.0.4 writes corrupt faces for any
    // mesh with UVs, and every mesh here has them (SceneIO.h).
    if (ext == ".fbx") {
        const auto r = mh::io::exportScene(path, rm.view(), mh::io::SceneFormat::FbxBinary, {},
                                           skin ? &*skin : nullptr);
        return report(r ? std::string{} : r.error().message());
    }
    if (ext == ".dae") {
        const auto r = mh::io::exportScene(path, rm.view(), mh::io::SceneFormat::Collada, {},
                                           skin ? &*skin : nullptr);
        return report(r ? std::string{} : r.error().message());
    }
    if (ext == ".stl") {
        const auto r = mh::io::exportScene(path, rm.view(), mh::io::SceneFormat::StlBinary, {},
                                           skin ? &*skin : nullptr);
        return report(r ? std::string{} : r.error().message());
    }
    if (ext == ".3mf") {
        const auto r = mh::io::exportScene(path, rm.view(), mh::io::SceneFormat::ThreeMf, {},
                                           skin ? &*skin : nullptr);
        return report(r ? std::string{} : r.error().message());
    }
    std::fprintf(stderr, "unknown export extension \"%s\"\n", ext.c_str());
    return false;
}

}  // namespace

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("MakeHuman"));
    QCoreApplication::setApplicationName(QStringLiteral("MakeHumanCpp"));

    // Resources before any widget exists: a stylesheet applied after the fact
    // repolishes every widget, and a font registered late is not the one the
    // first layout was measured with.
    const std::filesystem::path resources(MH_RESOURCE_DIR);
    mh::ui::theme::setIconDir(resources / "icons" / "lucide");
    const QString family = mh::ui::theme::installFonts(resources / "fonts");
    if (!family.isEmpty()) {
        QApplication::setFont(QFont(family, 13));
    }
    app.setStyleSheet(mh::ui::theme::styleSheet());

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("MakeHuman (C++/Qt6)"));
    parser.addHelpOption();
    const QCommandLineOption shaderOpt(QStringLiteral("shaders"),
                                       QStringLiteral("Directory holding the compiled .qsb files"),
                                       QStringLiteral("dir"), QStringLiteral(MH_SHADER_DIR));
    const QCommandLineOption shotOpt(
        QStringLiteral("screenshot"),
        QStringLiteral("Render one frame to this PNG and exit -- how the window is checked "
                       "without a human looking at it"),
        QStringLiteral("path"));
    const QCommandLineOption poseOpt(
        QStringLiteral("pose"),
        QStringLiteral("rest (the authored A-pose, default), tpose, or a path to a "
                       "single-frame .bvh"),
        QStringLiteral("pose"), QStringLiteral("rest"));
    const QCommandLineOption exportOpt(
        QStringLiteral("export"),
        QStringLiteral("Write the posed mesh here and exit. Format from the extension: "
                       ".obj .fbx .glb .usda .dae .stl .3mf"),
        QStringLiteral("path"));
    const QCommandLineOption setOpt(
        QStringLiteral("set"),
        QStringLiteral("Set a modifier before rendering or exporting, as "
                       "<full/name>=<value>. Repeatable."),
        QStringLiteral("modifier=value"));
    const QCommandLineOption skinOpt(
        QStringLiteral("skin"),
        QStringLiteral("Litsphere to shade with: african, asian or caucasian (default)."),
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
    const QCommandLineOption workspaceOpt(
        QStringLiteral("workspace"),
        QStringLiteral("Start in a workspace preset: Modelling, Rigging, Materials or Export."),
        QStringLiteral("name"));
    const QCommandLineOption eyesOpt(
        QStringLiteral("eyes"),
        QStringLiteral("Eye proxy to wear: none, or the stem of a .mhclo under data/eyes "
                       "(default high-poly)"),
        QStringLiteral("name"), QString::fromLatin1(kDefaultEyes));

    parser.addOption(workspaceOpt);
    parser.addOption(subdivOpt);
    parser.addOption(loadOpt);
    parser.addOption(saveOpt);
    parser.addOption(skinOpt);
    parser.addOption(eyesOpt);
    parser.addOption(setOpt);
    parser.addOption(poseOpt);
    parser.addOption(exportOpt);
    parser.addOption(shaderOpt);
    parser.addOption(shotOpt);
    parser.process(app);

    auto mesh = mh::core::loadObj(std::filesystem::path(MH_DATA_DIR) / "3dobjs" / "base.obj");
    if (!mesh) {
        std::fprintf(stderr, "cannot load the base mesh: %s\n", mesh.error().message().c_str());
        return 1;
    }
    // The modelling panel. mh_ui never sees a Modifier -- core resolves the
    // registry and hands down plain TaskViewSpecs, which is what keeps the UI
    // module Apache-2.0. loadStandardLayout also puts the task views in the
    // reference's tab order, which is NOT the order the files are written in.
    auto standard = mh::core::loadStandardLayout(std::filesystem::path(MH_DATA_DIR) / "modifiers");
    if (!standard) {
        std::fprintf(stderr, "cannot load the modifier registry: %s\n",
                     standard.error().message().c_str());
        return 1;
    }
    const std::vector<mh::foundation::TaskViewSpec>& views = standard->views;

    const mh::core::TargetIndex index =
        mh::core::TargetIndex::build(std::filesystem::path(MH_DATA_DIR) / "targets");
    mh::core::Human human(&index, standard->modifiers);
    // Lazy: only the targets a slider actually reaches are read from disk, so
    // start-up does not pay for all 1,280.
    mh::core::TargetLibrary targets(std::filesystem::path(MH_DATA_DIR) / "targets");

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

    if (human.stackSize() > 0) {
        uint32_t missing       = 0;
        const uint32_t applied = human.applyStack(*mesh, targets, &missing);
        std::printf("applied %u targets (%u missing)\n", applied, missing);
    }

    PoseRig rig;
    if (!loadPoseRig(*mesh, parser.value(poseOpt).toStdString(), rig)) return 1;

    // Adjacency is topology and survives both morphing and posing, so it is
    // built once. Normals are not, and are recomputed on every rebuild.
    mesh->buildAdjacency();
    if (!poseInPlace(*mesh, rig)) return 1;
    mesh->calcNormals();

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
    // base.obj is 138 parts helper geometry to 1 part body; drawing it raw
    // gives a figure in a solid skirt with a box over its face.
    if (!rm.setFaceMask(initial, initial.staticFaceMask())) {
        std::fprintf(stderr, "cannot apply the static face mask\n");
        return 1;
    }

    if (parser.isSet(saveOpt)) {
        const std::filesystem::path file = parser.value(saveOpt).toStdString();
        // No window, so no framing to record: the camera line stays as loaded.
        const mh::core::MhmFile doc = documentFor(human, document, file, std::nullopt, subdivided);
        if (const auto ok = mh::core::saveMhm(file, doc); !ok) {
            std::fprintf(stderr, "cannot save %s: %s\n", file.string().c_str(),
                         ok.error().message().c_str());
            return 1;
        }
        std::printf("wrote %s (%zu modifiers)\n", file.string().c_str(), doc.modifiers.size());
        return 0;  // --save means save and exit, as its help says
    }

    // Built before the window so the initial litsphere is the one the picker
    // shows -- otherwise the panel and the viewport start out disagreeing --
    // and before --export, which has to know what the character is wearing.
    const auto assetGroups =
        buildAssetGroups(parser.value(poseOpt).toStdString(), parser.value(skinOpt).toStdString(),
                         parser.value(eyesOpt).toStdString());

    // The body's material and everything worn, read by every rebuild. Skin is
    // a path rather than a call to setLitsphere because the viewport now takes
    // one material per mesh: the body's has to travel with the body.
    std::filesystem::path skin;
    std::map<QString, WornProxy> wornProxies;

    // Put on whatever the choosers start with. Done here, before both --export
    // and the window, so a headless export dresses the character exactly as the
    // window would: exporting a dressed character naked was the bug.
    if (const std::string eyes = selectedChoice(assetGroups, "Eyes");
        !eyes.empty() && eyes != kNoProxy) {
        if (auto worn = wearProxy(eyes, *mesh, eyeLitsphere())) {
            wornProxies.insert_or_assign(QStringLiteral("Eyes"), std::move(*worn));
        }
    }

    if (parser.isSet(exportOpt)) {
        for (auto& [group, worn] : wornProxies)
            refitProxy(worn, *mesh);
        return exportMesh(parser.value(exportOpt).toStdString(), displayMesh(), rm, wornProxies)
                   ? 0
                   : 1;
    }

    // The one rebuild path: sliders, pose and skin all go through it, so the
    // three controls cannot disagree about what is on screen.
    //
    // Declared ABOVE `window` and taking it as an argument rather than
    // capturing it. The panels are `window`'s children and their connections
    // reference this lambda, so this must outlive the window -- capturing the
    // window would force the opposite order and leave those connections holding
    // a dangling reference through teardown.
    const auto rebuildInto = [&](mh::ui::MainWindow& w) {
        human.applyStack(*mesh, targets);
        if (!poseInPlace(*mesh, rig)) return;
        mesh->calcNormals();

        const mh::core::Mesh& shown = displayMesh();
        // refreshPositions keeps the existing buffers only while the topology is
        // the same one they were built from; toggling subdivision changes it.
        if (rm.matches(shown)) {
            rm.refreshPositions(shown);
        } else {
            rm = mh::core::RenderMesh::build(shown);
            if (!rm.setFaceMask(shown, shown.staticFaceMask())) {
                // Unreachable today -- rm was just built from `shown` -- but
                // silently leaving every face visible draws the helper geometry
                // as a solid skirt with no clue why.
                std::fprintf(stderr, "cannot apply the static face mask after a topology change\n");
            }
        }
        // Worn proxies follow the body: re-fitted against the posed, morphed
        // base mesh every rebuild, not just when they are put on.
        std::vector<mh::render::MeshInstance> scene;
        scene.push_back({rm.view(), skin});
        for (auto& [group, worn] : wornProxies) {
            refitProxy(worn, *mesh);
            scene.push_back({worn.rm.view(), worn.litsphere});
        }
        w.setMeshes(std::move(scene));
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

    // Skin and pose go through the undo stack too, so Cmd+Z means the same
    // thing whichever panel the user last touched.
    const auto applyChoice = [&](const QString& group, const QString& id) {
        assets->setChoice(group, id);  // does not emit; see AssetPanel::setChoice
        if (group == QLatin1String("Skin")) {
            // Rebuild rather than setLitsphere: the body's material is carried
            // by its MeshInstance now, so changing it without rebuilding would
            // re-upload the old one and render an unchanged picture.
            skin = id.toStdString();
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
            } else if (auto worn = wearProxy(id.toStdString(), *mesh, eyeLitsphere())) {
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
    if (!tasks.add(kModelling) || !tasks.add(kMaterials)) {
        std::fprintf(stderr, "duplicate task category\n");
        return 1;
    }

    mh::ui::MainWindow window(parser.value(shaderOpt).toStdString(), tasks);

    // rm outlives the window, so the non-owning view stays valid.
    window.setMesh(rm.view());

    shell = &window;
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
            window.undoStack()->push(
                new mh::ui::ValueChangeCommand(id, before, value, mergeGroup, applyModifier));
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
    const QString chosenSkin = assets->choice(QStringLiteral("Skin"));
    if (chosenSkin.isEmpty()) {
        std::fprintf(stderr,
                     "no litspheres found in %s -- the viewport cannot shade anything "
                     "without one\n",
                     (std::filesystem::path(MH_DATA_DIR) / "litspheres").string().c_str());
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
            window.viewport()->setCamera(c);
        }
        subdivided = loaded->subdivide;

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
                        mh::core::OrbitView{c.pitchDegrees, c.yawDegrees, c.distance}, subdivided);

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
    QObject::connect(&window, &mh::ui::MainWindow::saveRequested, [&] {
        // Save with no path yet is Save As -- silently writing somewhere the
        // user did not choose is worse than asking.
        if (documentPath.isEmpty()) {
            emit window.saveAsRequested();
            return;
        }
        writeTo(documentPath);
    });

    window.restoreWorkspace();
    // After restoreWorkspace, so an explicit preset wins over the saved layout.
    if (parser.isSet(workspaceOpt)) {
        const QString name = parser.value(workspaceOpt);
        if (!window.applyWorkspacePreset(name)) {
            std::fprintf(stderr, "no such workspace preset: \"%s\"\n", name.toStdString().c_str());
            return 1;
        }
    }
    window.show();

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
            const bool drew = describe(frame, stats);
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
