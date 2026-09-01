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

#include <catch2/catch_session.hpp>
#include <catch2/catch_test_macros.hpp>

#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <filesystem>
#include <vector>

#include <QAccessible>
#include <QApplication>
#include <QDockWidget>
#include <QLabel>
#include <QMouseEvent>
#include <QSettings>
#include <QSlider>
#include <QTemporaryDir>
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
