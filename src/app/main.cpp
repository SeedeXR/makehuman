// SPDX-License-Identifier: AGPL-3.0-or-later
//
// The application. This is the AGPL side: it loads the mesh through mh::core
// and hands the UI a plain view, which is what lets mh_ui and mh_render stay
// Apache-2.0.
#include "makehuman/core/Mesh.h"
#include "makehuman/core/Mhm.h"
#include "makehuman/core/ObjReader.h"
#include "makehuman/core/RenderMesh.h"
#include "makehuman/core/SliderLayout.h"
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
#include "makehuman/ui/Theme.h"

#include <QApplication>
#include <QCommandLineParser>
#include <QFileDialog>
#include <QFont>
#include <QMessageBox>
#include <QPixmap>
#include <QTimer>
#include "makehuman/ui/ViewportWidget.h"

#include <QImage>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <filesystem>
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
    for (const auto& entry : std::filesystem::directory_iterator(dir, ec)) {
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

/// Skins and poses, from whatever is actually on disk.
///
/// Scanned rather than hard-coded so a litsphere or pose dropped into the data
/// directory appears without a code change -- and so this does not claim assets
/// that are not there.
std::vector<mh::foundation::AssetGroup> buildAssetGroups(const std::string& currentPose,
                                                         const std::string& currentSkin) {
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
    return groups;
}

/// The document to write: @p base with its modifiers refreshed from @p human.
///
/// Starting from a fresh MhmFile instead loses uuid, tags, camera and every
/// plugin line the loader kept -- open a rigged, clothed character, press Save,
/// and it reopens naked and unrigged.
mh::core::MhmFile documentFor(const mh::core::Human& human, const mh::core::MhmFile& base,
                              const std::filesystem::path& path) {
    mh::core::MhmFile out = mh::core::mhmFromHuman(human, path.stem().string());
    out.writtenBy         = base.writtenBy;
    out.uuid              = base.uuid;
    out.tags              = base.tags;
    out.camera            = base.camera;
    out.hasCamera         = base.hasCamera;
    out.subdivide         = base.subdivide;
    out.unhandled         = base.unhandled;
    if (!base.name.empty()) out.name = base.name;
    return out;
}

/// Writes the mesh in whichever format @p path's extension names.
bool exportMesh(const std::filesystem::path& path, const mh::core::Mesh& mesh,
                const mh::core::RenderMesh& rm) {
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

    if (ext == ".obj") {
        const auto r = mh::io::writeObj(path, mesh.view());
        return report(r ? std::string{} : r.error().message());
    }
    if (ext == ".usda" || ext == ".usd") {
        const auto r = mh::io::writeUsda(path, rm.view());
        return report(r ? std::string{} : r.error().message());
    }
    if (ext == ".glb") {
        const auto r = mh::io::writeGlb(path, rm.view());
        return report(r ? std::string{} : r.error().message());
    }

    // PLY is deliberately absent -- assimp 6.0.4 writes corrupt faces for any
    // mesh with UVs, and every mesh here has them (SceneIO.h).
    if (ext == ".fbx") {
        const auto r = mh::io::exportScene(path, rm.view(), mh::io::SceneFormat::FbxBinary);
        return report(r ? std::string{} : r.error().message());
    }
    if (ext == ".dae") {
        const auto r = mh::io::exportScene(path, rm.view(), mh::io::SceneFormat::Collada);
        return report(r ? std::string{} : r.error().message());
    }
    if (ext == ".stl") {
        const auto r = mh::io::exportScene(path, rm.view(), mh::io::SceneFormat::StlBinary);
        return report(r ? std::string{} : r.error().message());
    }
    if (ext == ".3mf") {
        const auto r = mh::io::exportScene(path, rm.view(), mh::io::SceneFormat::ThreeMf);
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
    parser.addOption(loadOpt);
    parser.addOption(saveOpt);
    parser.addOption(skinOpt);
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

    auto rm = mh::core::RenderMesh::build(*mesh);
    // base.obj is 138 parts helper geometry to 1 part body; drawing it raw
    // gives a figure in a solid skirt with a box over its face.
    if (!rm.setFaceMask(*mesh, mesh->staticFaceMask())) {
        std::fprintf(stderr, "cannot apply the static face mask\n");
        return 1;
    }

    if (parser.isSet(saveOpt)) {
        const std::filesystem::path file = parser.value(saveOpt).toStdString();
        const mh::core::MhmFile doc      = documentFor(human, document, file);
        if (const auto ok = mh::core::saveMhm(file, doc); !ok) {
            std::fprintf(stderr, "cannot save %s: %s\n", file.string().c_str(),
                         ok.error().message().c_str());
            return 1;
        }
        std::printf("wrote %s (%zu modifiers)\n", file.string().c_str(), doc.modifiers.size());
        return 0;  // --save means save and exit, as its help says
    }

    if (parser.isSet(exportOpt)) {
        return exportMesh(parser.value(exportOpt).toStdString(), *mesh, rm) ? 0 : 1;
    }

    // Built before the window so the initial litsphere is the one the picker
    // shows -- otherwise the panel and the viewport start out disagreeing.
    const auto assetGroups =
        buildAssetGroups(parser.value(poseOpt).toStdString(), parser.value(skinOpt).toStdString());

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
        rm.refreshPositions(*mesh);
        w.setMesh(rm.view());
    };

    // Above `window` for the same reason rebuildInto is: the File-menu
    // connections are owned by the window and reference this, so it has to
    // outlive it.
    QString documentPath = parser.value(loadOpt);

    mh::ui::MainWindow window(parser.value(shaderOpt).toStdString());

    // rm outlives the window, so the non-owning view stays valid.
    window.setMesh(rm.view());

    auto* panel = new mh::ui::ModifierPanel(views);
    for (const auto& [id, v] : presets)
        panel->setValue(id, v);
    window.setModellingWidget(panel);
    QObject::connect(panel, &mh::ui::ModifierPanel::valueChanged,
                     [&](const QString& id, float value) {
                         human.setModifierValue(id.toStdString(), value);
                         // Order matters: the stack resets the mesh to its
                         // morph base, so posing has to come after it or the
                         // pose is silently thrown away every time a slider
                         // moves.
                         rebuildInto(window);
                     });

    // Skin and pose. Both re-run the same rebuild the sliders do, so the three
    // controls cannot disagree about what is on screen.
    auto* assets = new mh::ui::AssetPanel(assetGroups);
    window.setMaterialsWidget(assets);
    // Taken from the picker, so the viewport and the panel cannot start out
    // disagreeing about which skin is shown.
    const QString skin = assets->choice(QStringLiteral("Skin"));
    if (skin.isEmpty()) {
        std::fprintf(stderr,
                     "no litspheres found in %s -- the viewport cannot shade "
                     "anything without one\n",
                     (std::filesystem::path(MH_DATA_DIR) / "litspheres").string().c_str());
        return 1;
    }
    window.setLitsphere(skin.toStdString());

    QObject::connect(assets, &mh::ui::AssetPanel::chosen,
                     [&](const QString& group, const QString& id) {
                         if (group == QLatin1String("Skin")) {
                             window.setLitsphere(id.toStdString());
                             return;
                         }
                         if (group != QLatin1String("Pose")) return;
                         // Back to the morph base FIRST. loadPoseRig fits the
                         // skeleton to whatever the mesh currently holds, and
                         // the mesh is left posed after every rebuild -- so
                         // switching pose to pose was conjugating into the
                         // previous pose's rest frame. Measured error: 33 cm
                         // maximum, 8 cm mean, across all 19,158 vertices.
                         human.applyStack(*mesh, targets);
                         PoseRig next;
                         if (!loadPoseRig(*mesh, id.toStdString(), next)) return;
                         rig = std::move(next);
                         rebuildInto(window);
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
        documentPath = file;
        window.setDocumentPath(file);
        rebuildInto(window);
    };

    const auto writeTo = [&](const QString& file) {
        const mh::core::MhmFile doc =
            documentFor(human, document, std::filesystem::path(file.toStdString()));
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
    // A screenshot run is not a session: saving its layout would overwrite the
    // one the user arranged by hand.
    if (!parser.isSet(shotOpt)) window.saveWorkspace();
    return rc;
}
