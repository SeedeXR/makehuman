// SPDX-License-Identifier: Apache-2.0
//
// The material editor panel, and the property that makes it non-decorative.
//
// `mh_ui` is Apache-2.0 and may never include a core header, so the PANEL takes
// `foundation::MaterialProperty` and knows nothing about `.mhmat`. The LIBRARY
// boundary is the rule, not the test binary's -- `mh_ui_tests` links both, and
// that is what lets the whole loop be asserted in one place:
//
//   Material -> editableProperties -> panel rows -> the panel's emitted spec
//            -> setMaterialProperty -> the SAME material back
//
// Without that loop the panel would be a set of widgets that look right.
#include "makehuman/core/Material.h"
#include "makehuman/ui/MainWindow.h"
#include "makehuman/ui/MaterialPanel.h"
#include "makehuman/ui/TaskRegistry.h"

#include <catch2/catch_test_macros.hpp>

#include <QDockWidget>
#include <QLineEdit>
#include <QSettings>
#include <QTabBar>
#include <QTemporaryDir>

#include <algorithm>
#include <string>
#include <vector>

using namespace mh;

namespace {

/// A material with non-default values in every kind of field, so a row that
/// reports the struct default rather than the material's cannot pass.
core::Material sample() {
    core::Material m;
    m.name         = "edited_skin";
    m.diffuse      = core::Vec3{0.8F, 0.1F, 0.1F};
    m.ambient      = core::Vec3{0.2F, 0.3F, 0.4F};
    m.specular     = core::Vec3{0.5F, 0.5F, 0.6F};
    m.emissive     = core::Vec3{0.05F, 0.0F, 0.1F};
    m.shininess    = 0.31F;
    m.opacity      = 0.75F;
    m.translucency = 0.25F;
    m.transparent  = true;
    m.backfaceCull = false;
    m.shadeless    = true;
    m.textures[static_cast<size_t>(core::TextureChannel::NormalMap)].path      = "/tex/n.png";
    m.textures[static_cast<size_t>(core::TextureChannel::NormalMap)].intensity = 0.6F;
    m.textures[static_cast<size_t>(core::TextureChannel::Diffuse)].path        = "/tex/d.png";
    return m;
}

bool sameMaterial(const core::Material& a, const core::Material& b) {
    const auto v = [](const core::Vec3& x, const core::Vec3& y) {
        return x.x == y.x && x.y == y.y && x.z == y.z;
    };
    if (a.name != b.name || a.description != b.description) return false;
    if (!v(a.diffuse, b.diffuse) || !v(a.ambient, b.ambient)) return false;
    if (!v(a.specular, b.specular) || !v(a.emissive, b.emissive)) return false;
    if (a.shininess != b.shininess || a.opacity != b.opacity) return false;
    if (a.translucency != b.translucency) return false;
    if (a.transparent != b.transparent || a.shadeless != b.shadeless) return false;
    if (a.backfaceCull != b.backfaceCull || a.wireframe != b.wireframe) return false;
    if (a.alphaToCoverage != b.alphaToCoverage || a.depthless != b.depthless) return false;
    if (a.autoBlendSkin != b.autoBlendSkin) return false;
    for (size_t i = 0; i < core::kTextureChannelCount; ++i) {
        if (a.textures[i].path != b.textures[i].path) return false;
        if (a.textures[i].intensity != b.textures[i].intensity) return false;
    }
    return a.uvMap == b.uvMap;
}

/// Modelling, Materials and Material -- the three the application registers.
mh::ui::TaskRegistry shippedTasksForLayout() {
    mh::ui::TaskRegistry tasks;
    (void)tasks.add(QStringLiteral("Modelling"));
    (void)tasks.add(QStringLiteral("Materials"));
    (void)tasks.add(QStringLiteral("Material"));
    return tasks;
}

}  // namespace

TEST_CASE("the editor offers the reference's own property list", "[ui][material]") {
    const auto props = core::editableProperties(sample());

    // PINNED BY NAME, not just by count: a row renamed to something the
    // reference's parser does not recognise would keep a count assertion green
    // and silently write a file MakeHuman 1.x ignores. This is the same trap
    // the 52 ARKit names hit.
    const std::vector<std::string> expected{"diffuseColor",
                                            "diffuseTexture",
                                            "ambientColor",
                                            "specularColor",
                                            "shininess",
                                            "emissiveColor",
                                            "opacity",
                                            "translucency",
                                            "shadeless",
                                            "wireframe",
                                            "transparent",
                                            "alphaToCoverage",
                                            "backfaceCull",
                                            "depthless",
                                            "autoBlendSkin",
                                            "transparencymapTexture",
                                            "transparencymapIntensity",
                                            "bumpmapTexture",
                                            "bumpmapIntensity",
                                            "normalmapTexture",
                                            "normalmapIntensity",
                                            "displacementmapTexture",
                                            "displacementmapIntensity",
                                            "specularmapTexture",
                                            "specularmapIntensity",
                                            "aomapTexture",
                                            "aomapIntensity",
                                            "uvMap",
                                            "name"};

    std::vector<std::string> got;
    got.reserve(props.size());
    for (const auto& p : props)
        got.push_back(p.id);
    CHECK(got == expected);

    // There is no `diffuseIntensity` -- the reference has no such row and our
    // parser has no such key (it is one of the two deprecated names
    // material.py:377-382 warns on). A row for it would write a line nothing
    // reads.
    CHECK(std::ranges::find(got, "diffuseIntensity") == got.end());

    for (const auto& p : props) {
        INFO("property " << p.id);
        CHECK_FALSE(p.label.empty());
    }
}

TEST_CASE("editableProperties is the exact inverse of setMaterialProperty",
          "[ui][material][inverse]") {
    // THE gate. Every row the panel can show must, fed straight back, be a
    // no-op -- otherwise opening the editor and touching nothing would change
    // the material, and every "the panel shows the right thing" assertion would
    // be measuring the wrong half.
    const core::Material original = sample();
    const auto props              = core::editableProperties(original);
    REQUIRE_FALSE(props.empty());

    for (const auto& p : props) {
        core::Material m       = original;
        const std::string spec = p.id + "=" + p.value;
        INFO("round trip " << spec);
        const auto ok = core::setMaterialProperty(m, spec, "/tex");
        REQUIRE(ok);
        CHECK(sameMaterial(m, original));
    }
}

TEST_CASE("the panel shows a row per property", "[ui][material]") {
    const auto props = core::editableProperties(sample());
    ui::MaterialPanel panel(props);
    CHECK(panel.rowCount() == static_cast<int>(props.size()));

    for (const auto& p : props) {
        INFO("row " << p.id);
        CHECK(panel.value(QString::fromStdString(p.id)).toStdString() == p.value);
    }
}

TEST_CASE("an edit leaves the panel as a spec the core accepts", "[ui][material][inverse]") {
    // The panel's APPLY path. `applyChoice` is not reachable headlessly and no
    // ctest covers any chooser's apply path, which is how the Expression
    // chooser shipped doing nothing; this panel emits a plain string, so its
    // apply path IS testable and is tested.
    const auto props = core::editableProperties(sample());
    ui::MaterialPanel panel(props);

    // A lambda rather than QSignalSpy: that class lives in Qt6::Test, which
    // mh_ui_tests does not link, and recording one string needs three lines.
    QStringList seen;
    QObject::connect(&panel, &ui::MaterialPanel::edited, &panel,
                     [&seen](const QString& spec) { seen.append(spec); });

    panel.setValue(QStringLiteral("shininess"), QStringLiteral("0.9"));
    REQUIRE(seen.isEmpty());  // setValue must not emit -- it shows, it does not edit

    // Driven through the WIDGET, which is the path a person takes. A test-only
    // "emit this row" hook would have proved the signal works and left the
    // widget-to-signal wiring -- the half that was missing in the Expression
    // chooser -- uncovered.
    auto* field = panel.findChild<QLineEdit*>(QStringLiteral("material:shininess"));
    REQUIRE(field != nullptr);
    CHECK(field->text() == QStringLiteral("0.9"));
    field->setText(QStringLiteral("0.9"));
    emit field->editingFinished();

    REQUIRE(seen.size() == 1);
    const auto spec = seen.at(0).toStdString();
    CHECK(spec == "shininess=0.9");

    core::Material m = sample();
    REQUIRE(core::setMaterialProperty(m, spec, "/tex"));
    CHECK(m.shininess == 0.9F);
}

TEST_CASE("the panel ignores an unknown id rather than inventing a row", "[ui][material]") {
    const auto props = core::editableProperties(sample());
    ui::MaterialPanel panel(props);
    const int before = panel.rowCount();
    panel.setValue(QStringLiteral("notAProperty"), QStringLiteral("1"));
    CHECK(panel.rowCount() == before);
    CHECK(panel.value(QStringLiteral("notAProperty")).isEmpty());
}

TEST_CASE("a third task TABS rather than stacking", "[ui][material][layout]") {
    // The material editor made this matter. With two categories the window put
    // one dock left and one right and looked fine; the third landed in the
    // right area too and Qt stacked it VERTICALLY, so a 29-row panel got a
    // title bar and about one row. Every UI test passed throughout, because
    // `shippedTasks()` in test_ui.cpp registers exactly two.
    //
    // Seen in a screenshot first, which is the only way it could have been
    // seen: no assertion in the suite described the layout at all.
    mh::ui::TaskRegistry tasks;
    REQUIRE(tasks.add(QStringLiteral("Modelling")));
    REQUIRE(tasks.add(QStringLiteral("Materials")));
    REQUIRE(tasks.add(QStringLiteral("Material")));

    mh::ui::MainWindow window(MH_SHADER_DIR, tasks);
    auto* assets   = window.findChild<QDockWidget*>(QStringLiteral("dock.materials"));
    auto* material = window.findChild<QDockWidget*>(QStringLiteral("dock.material"));
    REQUIRE(assets != nullptr);
    REQUIRE(material != nullptr);

    // Both in the right-hand area, and TABBED with each other rather than
    // sharing its height.
    CHECK(window.dockWidgetArea(assets) == Qt::RightDockWidgetArea);
    CHECK(window.dockWidgetArea(material) == Qt::RightDockWidgetArea);
    CHECK(window.tabifiedDockWidgets(assets).contains(material));

    // And the FIRST one registered in the area is the CURRENT tab, so the window
    // opens on the panel it always did rather than on whichever task happened to
    // be registered last -- tabifyDockWidget leaves the last one current.
    //
    // Read off the tab bar rather than visibility: nothing is visible in a
    // window that was never shown, and an isVisible() check here passes on any
    // arrangement at all.
    auto* bar = window.findChild<QTabBar*>();
    REQUIRE(bar != nullptr);
    REQUIRE(bar->count() == 2);
    CHECK(bar->tabText(bar->currentIndex()) == assets->windowTitle());
}

TEST_CASE("a workspace saved before the Material dock is IGNORED", "[ui][material][layout]") {
    // Tabbing fixed the layout for a fresh profile and changed nothing for
    // anyone who had run the app before: `restoreWorkspace` puts the saved
    // arrangement back, and a state saved by a build with two docks says
    // nothing about the third, which Qt then leaves wedged where it was added.
    // Every existing user would have seen the sliver.
    //
    // `saveState(version)` / `restoreState(state, version)` is Qt's answer:
    // a version mismatch makes restoreState return false and the shipped layout
    // stands. This asserts the old state really is refused -- without the
    // version parameter it restores cleanly and the dock comes back Right.
    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, dir.path());

    {
        mh::ui::MainWindow w(MH_SHADER_DIR, shippedTasksForLayout());
        auto* dock = w.findChild<QDockWidget*>(QStringLiteral("dock.modelling"));
        REQUIRE(dock != nullptr);
        w.addDockWidget(Qt::RightDockWidgetArea, dock);

        // Written the way a build BEFORE the layout version did: saveState()
        // with no version argument.
        QSettings stale(QSettings::IniFormat, QSettings::UserScope, QStringLiteral("MakeHuman"),
                        QStringLiteral("MakeHumanCpp"));
        stale.setValue(QStringLiteral("workspace/state"), w.saveState());
        stale.sync();
    }

    mh::ui::MainWindow fresh(MH_SHADER_DIR, shippedTasksForLayout());
    fresh.restoreWorkspace();
    auto* dock = fresh.findChild<QDockWidget*>(QStringLiteral("dock.modelling"));
    REQUIRE(dock != nullptr);
    CHECK(fresh.dockWidgetArea(dock) == Qt::LeftDockWidgetArea);
}
