// SPDX-License-Identifier: AGPL-3.0-or-later
//
// The application. This is the AGPL side: it loads the mesh through mh::core
// and hands the UI a plain view, which is what lets mh_ui and mh_render stay
// Apache-2.0.
#include "makehuman/core/Mesh.h"
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
#include "makehuman/ui/MainWindow.h"
#include "makehuman/ui/ModifierPanel.h"
#include "makehuman/ui/Theme.h"

#include <QApplication>
#include <QCommandLineParser>
#include <QFont>
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

    if (parser.isSet(exportOpt)) {
        return exportMesh(parser.value(exportOpt).toStdString(), *mesh, rm) ? 0 : 1;
    }

    mh::ui::MainWindow window(parser.value(shaderOpt).toStdString());
    window.setLitsphere(std::filesystem::path(MH_DATA_DIR) / "litspheres" /
                        "skinmat_caucasian.png");
    // rm outlives the window, so the non-owning view stays valid.
    window.setMesh(rm.view());

    auto* panel = new mh::ui::ModifierPanel(views);
    for (const auto& [id, v] : presets)
        panel->setValue(id, v);
    window.setModellingWidget(panel);
    // Every object this lambda captures is declared ABOVE `window`, and `panel`
    // is `window`'s child -- so reverse destruction tears down the connection
    // before any capture dies. That ordering is load-bearing; do not move the
    // declarations.
    QObject::connect(panel, &mh::ui::ModifierPanel::valueChanged,
                     [&](const QString& id, float value) {
                         human.setModifierValue(id.toStdString(), value);
                         // Order matters: the stack resets the mesh to its
                         // morph base, so posing has to come after it or the
                         // pose is silently thrown away every time a slider
                         // moves.
                         human.applyStack(*mesh, targets);
                         if (!poseInPlace(*mesh, rig)) return;
                         mesh->calcNormals();
                         rm.refreshPositions(*mesh);
                         window.setMesh(rm.view());
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
