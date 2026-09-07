// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Runs on the offscreen platform: no window is opened and nothing is rendered.
// These exercise the parts of the UI that are logic rather than pixels -- the
// navigation bindings and the workspace round-trip. What the viewport actually
// draws is checked by the render tests and by `makehuman --screenshot`, which
// needs a real device and so cannot run on a build box.
#include "makehuman/core/SliderLayout.h"
#include "makehuman/ui/MainWindow.h"
#include "makehuman/ui/ModifierPanel.h"
#include "makehuman/ui/TaskRegistry.h"
#include "makehuman/ui/ViewportWidget.h"
#include "makehuman/ui/Workspace.h"

#include <catch2/catch_session.hpp>
#include <catch2/catch_test_macros.hpp>

#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <filesystem>
#include <ranges>
#include <vector>

#include <QAccessible>
#include <QApplication>
#include <QDockWidget>
#include <QLabel>
#include <QMouseEvent>
#include <QSettings>
#include <QSlider>
#include <QTabWidget>
#include <QTemporaryDir>
#include <QUndoStack>
#include <QWheelEvent>

using Catch::Matchers::WithinAbs;

namespace {

/// The shipped modifier task views, or empty when this machine has no data
/// directory. The panel is built from real specs so the row structure under
/// test is the one the app ships.
std::vector<mh::foundation::TaskViewSpec> shippedModifierViews() {
    // `loadStandardLayout` takes the MODIFIERS directory, not the data root.
    const auto layout =
        mh::core::loadStandardLayout(std::filesystem::path(MH_DATA_DIR) / "modifiers");
    if (!layout) return {};
    return layout->views;
}

/// The two panels the app registers.
mh::ui::TaskRegistry shippedTasks() {
    mh::ui::TaskRegistry tasks;
    (void)tasks.add(QStringLiteral("Modelling"));
    (void)tasks.add(QStringLiteral("Materials"));
    return tasks;
}

/// The camera is float; Catch2's float matchers take double. Widening at the
/// call site keeps -Wdouble-promotion quiet without weakening it project-wide.
constexpr double d(float v) {
    return static_cast<double>(v);
}

void drag(mh::ui::ViewportWidget& w, QPoint from, QPoint to) {
    QMouseEvent press(QEvent::MouseButtonPress, QPointF(from), QPointF(from), Qt::LeftButton,
                      Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(&w, &press);
    QMouseEvent move(QEvent::MouseMove, QPointF(to), QPointF(to), Qt::NoButton, Qt::LeftButton,
                     Qt::NoModifier);
    QApplication::sendEvent(&w, &move);
}

void doubleClick(mh::ui::ViewportWidget& w, QPoint at) {
    QMouseEvent e(QEvent::MouseButtonDblClick, QPointF(at), QPointF(at), Qt::LeftButton,
                  Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(&w, &e);
}

void wheel(mh::ui::ViewportWidget& w, int notches) {
    QWheelEvent e(QPointF(0, 0), QPointF(0, 0), QPoint(), QPoint(0, notches * 120), Qt::NoButton,
                  Qt::NoModifier, Qt::NoScrollPhase, false);
    QApplication::sendEvent(&w, &e);
}

}  // namespace

// The header promises the geometry setters are safe before the RHI exists --
// the upload is deferred to the first frame. A widget that is never shown never
// initialises RHI, so this also pins that setMeshes touches no device state.
TEST_CASE("the geometry setters are safe before RHI initialisation", "[ui]") {
    mh::ui::ViewportWidget w(MH_SHADER_DIR);

    const std::vector<mh::foundation::Vec3> coord{{0, 0, 0}, {1, 0, 0}, {0, 1, 0}};
    const std::vector<uint32_t> index{0, 1, 2};
    const mh::foundation::RenderView view{coord, {}, {}, {}, index};

    // Either order must work: setLitsphere before setMesh stores the default
    // that setMesh then picks up, and after it updates the single entry.
    w.setLitsphere("first.png");
    w.setMesh(view);
    w.setLitsphere("second.png");
    w.setMeshes({{view, "a.png"}, {view, "b.png"}});
    // A multi-mesh list carries its own litspheres, so this must not flatten
    // them; against a one-mesh list it does apply, whatever set that list.
    w.setLitsphere("third.png");
    w.setMeshes({{view, "only.png"}});
    w.setLitsphere("fourth.png");

    CHECK(w.lastError().isEmpty());
}

// Double-click to focus. The reference does this with two GL readbacks; here
// it is a CPU ray cast, so it works in a widget that has never had a device --
// which is also what makes it testable at all.
TEST_CASE("double-clicking the model recentres the view on it", "[ui][pick]") {
    mh::ui::ViewportWidget w(MH_SHADER_DIR);
    w.resize(800, 600);

    // A quad filling the middle of the frame at the origin, big enough that the
    // centre pixel lands on it and the far corner does not.
    const std::vector<mh::foundation::Vec3> coord{{-3, -3, 0}, {3, -3, 0}, {3, 3, 0}, {-3, 3, 0}};
    const std::vector<uint32_t> index{0, 1, 2, 0, 2, 3};
    w.setMesh(mh::foundation::RenderView{coord, {}, {}, {}, index});

    const auto before = w.camera();
    REQUIRE(before.panX == 0.0F);

    // Off centre, but still on the quad.
    doubleClick(w, {460, 250});
    const auto after = w.camera();
    // Right of centre and above it: the model must move LEFT and DOWN to bring
    // that point to the middle.
    CHECK(after.panX < 0.0F);
    CHECK(after.panY < 0.0F);
    // Focusing recentres; it must not zoom.
    CHECK_THAT(d(after.distance), WithinAbs(d(before.distance), 1e-6));
}

// A dressed character is body plus a mesh per worn proxy, so "nearest" has to
// be decided ACROSS the list: clicking a sleeve must focus the sleeve, not the
// arm behind it.
TEST_CASE("double-clicking picks the nearest of several meshes", "[ui][pick]") {
    mh::ui::ViewportWidget w(MH_SHADER_DIR);
    w.resize(800, 600);

    // Two parallel planes, at z = 0 and z = 3, with the eye 45 away. An
    // off-centre ray diverges, so it meets them at DIFFERENT x -- which is what
    // makes the two outcomes tell apart at all.
    const std::vector<mh::foundation::Vec3> far{{-4, -4, 0}, {4, -4, 0}, {4, 4, 0}, {-4, 4, 0}};
    const std::vector<mh::foundation::Vec3> near{{-4, -4, 3}, {4, -4, 3}, {4, 4, 3}, {-4, 4, 3}};
    const std::vector<uint32_t> index{0, 1, 2, 0, 2, 3};
    // FAR first, so "keep whatever was found last" and "keep the nearest"
    // disagree.
    w.setMeshes({{mh::foundation::RenderView{far, {}, {}, {}, index}, "a.png"},
                 {mh::foundation::RenderView{near, {}, {}, {}, index}, "b.png"}});

    doubleClick(w, {460, 250});

    // ndcX = 0.15, aspect 4/3, tan(15 deg) = 0.267949 -> dx = 0.053590 per unit
    // of depth. The near plane is 42 away, the far one 45.
    CHECK_THAT(d(w.camera().panX), WithinAbs(-42.0 * 0.053590, 1e-3));
    CHECK_THAT(d(w.camera().panY), WithinAbs(-42.0 * 0.044658, 1e-3));
}

TEST_CASE("double-clicking empty space leaves the camera alone", "[ui][pick]") {
    mh::ui::ViewportWidget w(MH_SHADER_DIR);
    w.resize(800, 600);
    const std::vector<mh::foundation::Vec3> coord{{-1, -1, 0}, {1, -1, 0}, {1, 1, 0}};
    const std::vector<uint32_t> index{0, 1, 2};
    w.setMesh(mh::foundation::RenderView{coord, {}, {}, {}, index});

    // A miss must be a no-op, not a jump to wherever the ray happened to be.
    // Without that the corner of the window throws the model off screen.
    doubleClick(w, {5, 5});
    CHECK_THAT(d(w.camera().panX), WithinAbs(0.0, 1e-6));
    CHECK_THAT(d(w.camera().panY), WithinAbs(0.0, 1e-6));
}

TEST_CASE("dragging orbits by half a degree per pixel", "[ui]") {
    mh::ui::ViewportWidget w(MH_SHADER_DIR);
    const auto before = w.camera();

    drag(w, {100, 100}, {140, 120});

    CHECK_THAT(d(w.camera().yawDegrees), WithinAbs(d(before.yawDegrees) + 20.0, 1e-4));
    CHECK_THAT(d(w.camera().pitchDegrees), WithinAbs(d(before.pitchDegrees) + 10.0, 1e-4));
    // Orbiting must not change how far away the camera sits.
    CHECK_THAT(d(w.camera().distance), WithinAbs(d(before.distance), 1e-6));
}

TEST_CASE("a move with no button held does not orbit", "[ui]") {
    mh::ui::ViewportWidget w(MH_SHADER_DIR);
    const auto before = w.camera();

    QMouseEvent move(QEvent::MouseMove, QPointF(400, 400), QPointF(400, 400), Qt::NoButton,
                     Qt::NoButton, Qt::NoModifier);
    QApplication::sendEvent(&w, &move);

    CHECK_THAT(d(w.camera().yawDegrees), WithinAbs(d(before.yawDegrees), 1e-6));
    CHECK_THAT(d(w.camera().pitchDegrees), WithinAbs(d(before.pitchDegrees), 1e-6));
}

TEST_CASE("pitch stops at the poles", "[ui]") {
    mh::ui::ViewportWidget w(MH_SHADER_DIR);

    drag(w, {0, 0}, {0, 100000});
    CHECK_THAT(d(w.camera().pitchDegrees), WithinAbs(89.0, 1e-4));

    drag(w, {0, 0}, {0, -100000});
    CHECK_THAT(d(w.camera().pitchDegrees), WithinAbs(-89.0, 1e-4));
}

TEST_CASE("yaw is not clamped, so the model can be turned right around", "[ui]") {
    mh::ui::ViewportWidget w(MH_SHADER_DIR);
    drag(w, {0, 0}, {2000, 0});
    CHECK_THAT(d(w.camera().yawDegrees), WithinAbs(1000.0, 1e-2));
}

TEST_CASE("the wheel dollies multiplicatively and stays in range", "[ui]") {
    mh::ui::ViewportWidget w(MH_SHADER_DIR);

    mh::render::Camera c = w.camera();
    c.distance           = 40.0F;
    w.setCamera(c);

    wheel(w, 1);
    CHECK_THAT(d(w.camera().distance), WithinAbs(36.0, 1e-3));  // 40 * 0.9
    wheel(w, -1);
    CHECK_THAT(d(w.camera().distance), WithinAbs(40.0, 1e-3));

    // A step is the same proportion at every distance -- that is the point of
    // multiplicative dolly, and what a linear one gets wrong.
    c.distance = 200.0F;
    w.setCamera(c);
    wheel(w, 1);
    CHECK_THAT(d(w.camera().distance) / 200.0, WithinAbs(0.9, 1e-4));

    wheel(w, 100);
    CHECK_THAT(d(w.camera().distance), WithinAbs(5.0, 1e-4));
    wheel(w, -100);
    CHECK_THAT(d(w.camera().distance), WithinAbs(300.0, 1e-4));
}

TEST_CASE("a workspace survives a save and restore", "[ui]") {
    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, dir.path());

    {
        mh::ui::MainWindow w(MH_SHADER_DIR, shippedTasks());
        auto* dock = w.findChild<QDockWidget*>(QStringLiteral("dock.modelling"));
        REQUIRE(dock != nullptr);
        REQUIRE(w.dockWidgetArea(dock) == Qt::LeftDockWidgetArea);

        w.addDockWidget(Qt::RightDockWidgetArea, dock);
        w.saveWorkspace();
    }

    mh::ui::MainWindow fresh(MH_SHADER_DIR, shippedTasks());
    fresh.restoreWorkspace();
    auto* dock = fresh.findChild<QDockWidget*>(QStringLiteral("dock.modelling"));
    REQUIRE(dock != nullptr);
    // Without an objectName on the dock, restoreState silently does nothing and
    // this would come back Left -- which is exactly the bug the name prevents.
    CHECK(fresh.dockWidgetArea(dock) == Qt::RightDockWidgetArea);
}

TEST_CASE("resetting the workspace discards the saved one", "[ui]") {
    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, dir.path());

    {
        mh::ui::MainWindow w(MH_SHADER_DIR, shippedTasks());
        auto* dock = w.findChild<QDockWidget*>(QStringLiteral("dock.modelling"));
        REQUIRE(dock != nullptr);
        w.addDockWidget(Qt::RightDockWidgetArea, dock);
        w.saveWorkspace();
        w.resetWorkspace();
    }

    mh::ui::MainWindow fresh(MH_SHADER_DIR, shippedTasks());
    fresh.restoreWorkspace();
    auto* dock = fresh.findChild<QDockWidget*>(QStringLiteral("dock.modelling"));
    REQUIRE(dock != nullptr);
    CHECK(fresh.dockWidgetArea(dock) == Qt::LeftDockWidgetArea);

    // Left is also what a window that restored nothing shows, so on its own the
    // check above cannot tell "reset worked" from "restore silently failed".
    // The round-trip test above is what rules the second one out.
    CHECK(fresh.findChild<QDockWidget*>(QStringLiteral("dock.materials")) != nullptr);
}

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    return Catch::Session().run(argc, argv);
}

// --- What a screen reader is told a slider's value is -----------------------
//
// Measured before the fix, on the first shipped slider:
//
//     slider name : "Neck circum, Neck"
//     slider value: "500"          <- the raw TICK
//     readout text: "0.00"
//
// Qt's default for a QSlider is `QString::number(value())`, and these run
// 0..1000 ticks whatever the modifier's own range is. So a screen-reader user
// heard "500" for a modifier sitting at 0.00 -- a number with no meaning, and
// one that disagrees with the label right beside it.
//
// `text(QAccessible::Value)` is not a proxy for what VoiceOver reads: it is the
// string Qt's Cocoa accessibility bridge hands over. What is NOT verified here
// is VoiceOver's own behaviour, which needs a real device.
TEST_CASE("a slider announces its value, not its tick", "[ui][a11y]") {
    const auto views = shippedModifierViews();
    if (views.empty()) return;  // no data dir on this machine

    mh::ui::ModifierPanel panel(views);
    QSlider* slider = panel.findChild<QSlider*>(QString(), Qt::FindChildrenRecursively);
    REQUIRE(slider != nullptr);

    auto* row = qobject_cast<QWidget*>(slider->parent());
    REQUIRE(row != nullptr);
    QLabel* readout = nullptr;
    for (QLabel* l : row->findChildren<QLabel*>(QString(), Qt::FindChildrenRecursively)) {
        if (l->objectName() == QStringLiteral("modifiers.readout")) readout = l;
    }
    REQUIRE(readout != nullptr);

    QAccessibleInterface* iface = QAccessible::queryAccessibleInterface(slider);
    REQUIRE(iface != nullptr);
    CHECK_FALSE(iface->text(QAccessible::Name).isEmpty());

    // Heard and seen must be the same number.
    INFO("announced " << iface->text(QAccessible::Value).toStdString() << ", shown "
                      << readout->text().toStdString());
    CHECK(iface->text(QAccessible::Value) == readout->text());

    // ...and it must follow the slider, not be a one-off at construction. The
    // tick and the value differ by three orders of magnitude here, so a stale
    // or raw reading cannot coincide with the right answer.
    slider->setValue(slider->maximum());
    QAccessibleInterface* after = QAccessible::queryAccessibleInterface(slider);
    REQUIRE(after != nullptr);
    INFO("at maximum: announced " << after->text(QAccessible::Value).toStdString() << ", shown "
                                  << readout->text().toStdString() << ", tick " << slider->value());
    CHECK(after->text(QAccessible::Value) == readout->text());
    CHECK(after->text(QAccessible::Value) != QString::number(slider->value()));
}

// --- Workspace changes and the undo stack ------------------------------------
//
// `applyWorkspacePreset` rewrote the layout and pushed nothing, so Cmd+1
// followed by Cmd+Z left the new layout in place and undid whatever slider the
// user had touched before it -- the wrong thing, silently.
//
// `saveWorkspaceAs` is deliberately NOT undoable and is not covered here: it
// writes a file and changes no window state. "Undo" for it would mean deleting
// a file the user asked to save, which is not what an undo stack is for.
TEST_CASE("saveState round-trips dock visibility", "[ui][workspace]") {
    // The whole undo design below rests on this: one QByteArray must carry
    // enough to restore a layout, visibility included. Asserted rather than
    // assumed, because if it only carried geometry the undo would silently
    // restore positions and leave docks hidden.
    mh::ui::MainWindow w(MH_SHADER_DIR, shippedTasks());
    const auto docks = w.findChildren<QDockWidget*>();
    REQUIRE(docks.size() >= 2);

    // isHidden(), not isVisible(): this window is never shown, so every child
    // reports isVisible() == false regardless of what was asked for. isHidden()
    // is the flag setVisible() actually writes and restoreState() restores.
    docks.front()->setVisible(false);
    const QByteArray hidden = w.saveState();
    docks.front()->setVisible(true);
    REQUIRE(docks.front()->isHidden() == false);

    REQUIRE(w.restoreState(hidden));
    CHECK(docks.front()->isHidden() == true);
}

TEST_CASE("applying a workspace preset is one undo step", "[ui][workspace][undo]") {
    mh::ui::MainWindow w(MH_SHADER_DIR, shippedTasks());
    auto* stack = w.undoStack();
    REQUIRE(stack != nullptr);
    REQUIRE(stack->count() == 0);

    const QByteArray before = w.saveState();
    const auto& presets     = mh::ui::workspacePresets();
    REQUIRE(presets.size() >= 2);

    REQUIRE(w.applyWorkspacePreset(presets[1].name));
    const QByteArray after = w.saveState();
    // The preset must actually change something, or nothing below is a test.
    REQUIRE(after != before);
    CHECK(stack->count() == 1);

    stack->undo();
    CHECK(w.saveState() == before);
    stack->redo();
    CHECK(w.saveState() == after);
}

// A preset that resolves to nothing is refused, and a refusal must not leave an
// undo entry that does nothing -- the same rule the pose commands already
// follow ("probed before the command is pushed").
TEST_CASE("a refused preset pushes no undo entry", "[ui][workspace][undo]") {
    mh::ui::MainWindow w(MH_SHADER_DIR, shippedTasks());
    auto* stack = w.undoStack();
    REQUIRE(stack != nullptr);
    CHECK_FALSE(w.applyWorkspacePreset(QStringLiteral("no such preset")));
    CHECK(stack->count() == 0);
}

// --- Camera pan ---------------------------------------------------------------
//
// The `.mhm` camera line holds a translation in slots 2..4 that this port wrote
// as zeros, and the viewport had no pan at all: a model could only be orbited
// and zoomed, never moved off centre.
//
// Middle drag pans, left drag still orbits. The reference binds pan to the
// arrow keys (`core/mhmain.py:178-181`), but those already orbit here -- taking
// them back would remove a working control to match a convention -- so pan went
// on the button every DCC uses and nothing was lost.
TEST_CASE("middle drag pans, left drag orbits", "[ui][viewport][pan]") {
    mh::ui::ViewportWidget v(MH_SHADER_DIR);
    v.resize(400, 300);
    const mh::render::Camera start = v.camera();

    const auto drag = [&v](Qt::MouseButton button, QPoint from, QPoint to) {
        QMouseEvent press(QEvent::MouseButtonPress, QPointF(from), QPointF(from), QPointF(from),
                          button, button, Qt::NoModifier);
        QApplication::sendEvent(&v, &press);
        QMouseEvent move(QEvent::MouseMove, QPointF(to), QPointF(to), QPointF(to), Qt::NoButton,
                         button, Qt::NoModifier);
        QApplication::sendEvent(&v, &move);
    };

    SECTION("middle drag moves the camera and leaves the angles alone") {
        drag(Qt::MiddleButton, {100, 100}, {160, 140});
        const mh::render::Camera c = v.camera();
        CHECK(c.panX > start.panX);
        // Screen y grows downward and the camera's does not, so dragging DOWN
        // must decrease panY. Getting this backwards makes the model run away
        // from the cursor.
        CHECK(c.panY < start.panY);
        CHECK(c.yawDegrees == start.yawDegrees);
        CHECK(c.pitchDegrees == start.pitchDegrees);
    }

    SECTION("left drag still orbits and does not pan") {
        drag(Qt::LeftButton, {100, 100}, {160, 140});
        const mh::render::Camera c = v.camera();
        CHECK(c.yawDegrees != start.yawDegrees);
        CHECK(c.pitchDegrees != start.pitchDegrees);
        CHECK(c.panX == start.panX);
        CHECK(c.panY == start.panY);
    }
}

TEST_CASE("panning scales with distance", "[ui][viewport][pan]") {
    // Pan is a world-space offset seen through a perspective projection, so a
    // fixed step per pixel crawls when far away and leaps when close. The same
    // drag must cover the same fraction of the screen at any zoom.
    const auto panFor = [](float distance) {
        mh::ui::ViewportWidget v(MH_SHADER_DIR);
        v.resize(400, 300);
        mh::render::Camera c = v.camera();
        c.distance           = distance;
        v.setCamera(c);

        const QPoint from(100, 100);
        const QPoint to(200, 100);
        QMouseEvent press(QEvent::MouseButtonPress, QPointF(from), QPointF(from), QPointF(from),
                          Qt::MiddleButton, Qt::MiddleButton, Qt::NoModifier);
        QApplication::sendEvent(&v, &press);
        QMouseEvent move(QEvent::MouseMove, QPointF(to), QPointF(to), QPointF(to), Qt::NoButton,
                         Qt::MiddleButton, Qt::NoModifier);
        QApplication::sendEvent(&v, &move);
        return v.camera().panX;
    };

    const float near = panFor(10.0F);
    const float far  = panFor(100.0F);
    REQUIRE(near > 0.0F);
    INFO("near " << near << ", far " << far);
    CHECK(far > near * 5.0F);  // 10x the distance, so nearly 10x the pan
}

// ---------------------------------------------------------------- tabs -----
//
// Owner decision, 2026-09-07: "for docks vs tabs, we can design for both, just
// ensure it's intuitive and allows someone to configure their workspace and
// save or decided to reset to the default ui."
//
// `AllowTabbedDocks` and `GroupedDragging` were already set, so dragging one
// panel onto another has always tabbed them -- but nothing verified that the
// arrangement SURVIVES a save, and nothing offered tabs except by discovering
// the drag. These cover both halves.

TEST_CASE("a tabbed arrangement survives save and restore", "[ui][workspace][tabs]") {
    // The claim `saveState` round-trips tabbing is Qt's, not ours, and this
    // window is the thing that has to keep working -- so it is checked rather
    // than assumed. If Qt ever stopped, a user's tabbed layout would silently
    // come back as columns.
    mh::ui::MainWindow window(std::filesystem::path{}, shippedTasks());
    auto* modelling = window.findChild<QDockWidget*>(
        mh::ui::MainWindow::dockObjectName(QStringLiteral("Modelling")));
    auto* materials = window.findChild<QDockWidget*>(
        mh::ui::MainWindow::dockObjectName(QStringLiteral("Materials")));
    REQUIRE(modelling != nullptr);
    REQUIRE(materials != nullptr);
    REQUIRE(window.tabifiedDockWidgets(modelling).isEmpty());

    window.tabifyDockWidget(modelling, materials);
    REQUIRE(window.tabifiedDockWidgets(modelling).contains(materials));

    const QByteArray tabbed = window.saveState();
    // Pulled apart again, so restoring has something real to undo.
    window.addDockWidget(Qt::RightDockWidgetArea, materials);
    REQUIRE(window.tabifiedDockWidgets(modelling).isEmpty());

    REQUIRE(window.restoreState(tabbed));
    CHECK(window.tabifiedDockWidgets(modelling).contains(materials));
}

TEST_CASE("panel tabs sit at the top, not Qt's default bottom", "[ui][workspace][tabs]") {
    // Pinned because it is a deliberate departure from the Qt default, and the
    // reason is only visible in a screenshot: at `South` the
    // "Modelling | Materials" bar sat below 900 pixels of sliders, nowhere near
    // the panel title it switches, and read as a status strip.
    mh::ui::MainWindow window(std::filesystem::path{}, shippedTasks());
    for (const Qt::DockWidgetArea area : {Qt::LeftDockWidgetArea, Qt::RightDockWidgetArea,
                                          Qt::TopDockWidgetArea, Qt::BottomDockWidgetArea}) {
        CHECK(window.tabPosition(area) == QTabWidget::North);
    }
}

TEST_CASE("the Tabbed preset really tabs the panels", "[ui][workspace][tabs]") {
    // Tabs as something a user can CHOOSE, not only discover by dragging. The
    // preset list is what the Workspace menu and Cmd+1..n are built from, so
    // adding it there is what makes the mode reachable.
    const auto& presets = mh::ui::workspacePresets();
    const auto tabbed   = std::ranges::find_if(
        presets, [](const auto& p) { return p.name == QStringLiteral("Tabbed"); });
    REQUIRE(tabbed != presets.end());
    CHECK(tabbed->tabbed);

    mh::ui::MainWindow window(std::filesystem::path{}, shippedTasks());
    REQUIRE(window.applyWorkspacePreset(QStringLiteral("Tabbed")));

    auto* modelling = window.findChild<QDockWidget*>(
        mh::ui::MainWindow::dockObjectName(QStringLiteral("Modelling")));
    auto* materials = window.findChild<QDockWidget*>(
        mh::ui::MainWindow::dockObjectName(QStringLiteral("Materials")));
    REQUIRE(modelling != nullptr);
    REQUIRE(materials != nullptr);
    // isHidden(), not isVisible(): this window is never shown, so every child
    // reports isVisible() == false whatever was asked for. Same reason as
    // "saveState round-trips dock visibility" above.
    CHECK_FALSE(modelling->isHidden());
    CHECK_FALSE(materials->isHidden());
    CHECK(window.tabifiedDockWidgets(modelling).contains(materials));
}

TEST_CASE("a side-by-side preset un-tabs what Tabbed did", "[ui][workspace][tabs]") {
    // "Both" has to mean both DIRECTIONS. Applying Modelling after Tabbed must
    // give columns back, or the tabbed mode is a one-way door.
    mh::ui::MainWindow window(std::filesystem::path{}, shippedTasks());
    REQUIRE(window.applyWorkspacePreset(QStringLiteral("Tabbed")));
    auto* modelling = window.findChild<QDockWidget*>(
        mh::ui::MainWindow::dockObjectName(QStringLiteral("Modelling")));
    REQUIRE(modelling != nullptr);
    REQUIRE_FALSE(window.tabifiedDockWidgets(modelling).isEmpty());

    REQUIRE(window.applyWorkspacePreset(QStringLiteral("Modelling")));
    CHECK(window.tabifiedDockWidgets(modelling).isEmpty());
}

TEST_CASE("reset returns to the docked default from a tabbed layout", "[ui][workspace][tabs]") {
    // The owner's "or decided to reset to the default ui", from the state most
    // likely to make someone want it.
    mh::ui::MainWindow window(std::filesystem::path{}, shippedTasks());
    auto* modelling = window.findChild<QDockWidget*>(
        mh::ui::MainWindow::dockObjectName(QStringLiteral("Modelling")));
    auto* materials = window.findChild<QDockWidget*>(
        mh::ui::MainWindow::dockObjectName(QStringLiteral("Materials")));
    REQUIRE(modelling != nullptr);
    REQUIRE(materials != nullptr);

    window.tabifyDockWidget(modelling, materials);
    REQUIRE_FALSE(window.tabifiedDockWidgets(modelling).isEmpty());

    window.resetWorkspace();
    CHECK(window.tabifiedDockWidgets(modelling).isEmpty());
    CHECK_FALSE(modelling->isHidden());
    CHECK_FALSE(materials->isHidden());
}

TEST_CASE("a named workspace keeps its tabs", "[ui][workspace][tabs]") {
    // The configure-and-save half of the request, end to end through the real
    // JSON file rather than through saveState alone.
    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, dir.path());

    mh::ui::MainWindow window(std::filesystem::path{}, shippedTasks());
    auto* modelling = window.findChild<QDockWidget*>(
        mh::ui::MainWindow::dockObjectName(QStringLiteral("Modelling")));
    auto* materials = window.findChild<QDockWidget*>(
        mh::ui::MainWindow::dockObjectName(QStringLiteral("Materials")));
    REQUIRE(modelling != nullptr);
    REQUIRE(materials != nullptr);

    window.tabifyDockWidget(modelling, materials);
    const QString name = QStringLiteral("tabbed-layout");
    REQUIRE(window.saveWorkspaceAs(name));

    window.resetWorkspace();
    REQUIRE(window.tabifiedDockWidgets(modelling).isEmpty());

    REQUIRE(window.loadNamedWorkspace(name));
    CHECK(window.tabifiedDockWidgets(modelling).contains(materials));
}

// ------------------------------------------------- category id vs title -----
//
// The second dock was called "Materials" and holds Skin, Pose, Eyes, Skin
// material and Skeleton -- three of which are not materials.
//
// There is no reference name for this set, and I checked rather than assumed:
// the `3_libraries_*` filenames are a plugin-ordering convention, not a tab.
// The reference SPLITS these across three of its own categories --
// `getCategory('Materials')` for the material chooser,
// `getCategory('Geometries')` for the eyes, `getCategory('Pose/Animate')` for
// pose and skeleton. Borrowing "Libraries" would have invented a user-facing
// name the reference does not have.
//
// So it takes the name our own code already gives it: the widget in that dock
// is `mh::ui::AssetPanel`, and skin, eyes, pose, skeleton and material are all
// chosen ASSETS. When the task-view work subdivides this panel it will follow
// the reference's three categories; until then one honest name beats three
// wrong ones.
//
// Renaming looked expensive because `dockObjectName` lower-cases the category
// and `QMainWindow::saveState` keys on that, so every saved workspace would
// stop matching. It is only expensive if the visible title and the persisted
// key are the same string. They are now separate: the ID stays "Materials"
// forever, the TITLE is what the user reads.

TEST_CASE("a category has a stable id and a separate display title", "[ui][registry]") {
    mh::ui::TaskRegistry tasks;
    REQUIRE(tasks.add(QStringLiteral("Modelling")));
    REQUIRE(tasks.add(QStringLiteral("Materials"), QStringLiteral("Assets")));

    // categories() is the ID list -- what dockObjectName and the workspace
    // presets address -- and it is unchanged by any renaming.
    CHECK(tasks.categories() ==
          QStringList{QStringLiteral("Modelling"), QStringLiteral("Materials")});
    CHECK(tasks.title(QStringLiteral("Materials")) == QStringLiteral("Assets"));

    // A category registered without one is its own title, so the common case
    // stays a single argument.
    CHECK(tasks.title(QStringLiteral("Modelling")) == QStringLiteral("Modelling"));
    // An id nobody registered answers with itself rather than an empty string:
    // a blank dock title is worse than a slightly wrong one.
    CHECK(tasks.title(QStringLiteral("Nope")) == QStringLiteral("Nope"));
}

TEST_CASE("renaming the title leaves the persisted key alone", "[ui][registry]") {
    // The property that makes the rename free. If this ever fails, every
    // workspace a user saved before the rename silently loses that panel's
    // position -- restoreState matches docks by objectName and quietly ignores
    // one it does not recognise.
    CHECK(mh::ui::MainWindow::dockObjectName(QStringLiteral("Materials")) ==
          QStringLiteral("dock.materials"));

    mh::ui::TaskRegistry tasks;
    REQUIRE(tasks.add(QStringLiteral("Modelling")));
    REQUIRE(tasks.add(QStringLiteral("Materials"), QStringLiteral("Assets")));
    mh::ui::MainWindow window(std::filesystem::path{}, tasks);

    auto* dock = window.findChild<QDockWidget*>(QStringLiteral("dock.materials"));
    REQUIRE(dock != nullptr);
    CHECK(dock->windowTitle() == QStringLiteral("Assets"));

    // Hidden, so restoring has something unmistakable to bring back.
    dock->setVisible(false);
    const QByteArray state = window.saveState();

    // The property itself, without reading Qt's blob format: a window whose
    // category carries a DIFFERENT title restores that same state correctly,
    // because only the id is persisted. That is what makes every workspace
    // saved before the rename keep working.
    //
    // Two earlier attempts asserted on the blob's bytes instead and both found
    // nothing in perfectly correct output -- `saveState` is a QDataStream, not
    // a run of UTF-16 at a predictable offset. Testing the behaviour is both
    // easier and the thing that actually matters.
    mh::ui::TaskRegistry oldNames;
    REQUIRE(oldNames.add(QStringLiteral("Modelling")));
    REQUIRE(oldNames.add(QStringLiteral("Materials")));  // the pre-rename title
    mh::ui::MainWindow other(std::filesystem::path{}, oldNames);

    auto* sameDock = other.findChild<QDockWidget*>(QStringLiteral("dock.materials"));
    REQUIRE(sameDock != nullptr);
    CHECK(sameDock->windowTitle() == QStringLiteral("Materials"));
    REQUIRE_FALSE(sameDock->isHidden());

    REQUIRE(other.restoreState(state));
    CHECK(sameDock->isHidden());
}

TEST_CASE("a duplicate id is still refused however it is titled", "[ui][registry]") {
    // The case rule exists because dockObjectName lower-cases: two categories
    // differing only in case would share one dock and one saved-state key.
    // Giving them different titles must not sneak them past it.
    mh::ui::TaskRegistry tasks;
    REQUIRE(tasks.add(QStringLiteral("Materials"), QStringLiteral("Assets")));
    CHECK_FALSE(tasks.add(QStringLiteral("materials"), QStringLiteral("Something Else")));
    CHECK(tasks.categories().size() == 1);
}
