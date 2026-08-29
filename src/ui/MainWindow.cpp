// SPDX-License-Identifier: Apache-2.0
#include "makehuman/ui/MainWindow.h"

#include "makehuman/ui/PanelTitleBar.h"
#include "makehuman/ui/ViewportWidget.h"

#include <QDockWidget>
#include <QLabel>
#include <QSettings>
#include <QStatusBar>

namespace mh::ui {
namespace {

/// A dock's object name is what QMainWindow::saveState keys on. Without one,
/// the saved layout silently fails to restore -- Qt warns, and the window comes
/// back with default docks as if nothing had been saved.
QDockWidget* makeDock(const QString& title, const QString& objectName, Qt::DockWidgetArea home,
                      QWidget* parent) {
    auto* dock = new QDockWidget(title, parent);
    dock->setObjectName(objectName);
    dock->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);
    // The six-dot menu (design.md 6.3). Qt has no hook for extra title-bar
    // buttons, so the whole bar is replaced.
    dock->setTitleBarWidget(new PanelTitleBar(dock, home));

    auto* body = new QLabel(QStringLiteral("%1\n\n(not yet implemented)").arg(title), dock);
    body->setAlignment(Qt::AlignCenter);
    body->setWordWrap(true);
    dock->setWidget(body);
    return dock;
}

constexpr auto kGeom  = "workspace/geometry";
constexpr auto kState = "workspace/state";

/// IniFormat rather than the platform default. On macOS the native backend is
/// CFPreferences, which ignores QSettings::setPath -- so a test could not
/// redirect it away from the real user preferences, and running the suite would
/// silently rewrite the developer's own window layout.
QSettings workspaceSettings() {
    return QSettings(QSettings::IniFormat, QSettings::UserScope, QStringLiteral("MakeHuman"),
                     QStringLiteral("MakeHumanCpp"));
}

}  // namespace

struct MainWindow::Impl {
    ViewportWidget* viewport{};
    /// The shipped layout, captured before any saved one is restored. Resetting
    /// is then just restoring it, which also covers docks added later -- the
    /// manual re-dock it replaces did not.
    QByteArray defaultState;
};

MainWindow::MainWindow(std::filesystem::path shaderDir, QWidget* parent)
    : QMainWindow(parent), d_(std::make_unique<Impl>()) {
    setObjectName(QStringLiteral("MainWindow"));
    setWindowTitle(QStringLiteral("MakeHuman"));

    d_->viewport = new ViewportWidget(std::move(shaderDir), this);
    setCentralWidget(d_->viewport);

    addDockWidget(Qt::LeftDockWidgetArea,
                  makeDock(QStringLiteral("Modelling"), QStringLiteral("dock.modelling"),
                           Qt::LeftDockWidgetArea, this));
    addDockWidget(Qt::RightDockWidgetArea,
                  makeDock(QStringLiteral("Materials"), QStringLiteral("dock.materials"),
                           Qt::RightDockWidgetArea, this));

    // A renderer failure is otherwise a black rectangle and a status bar saying
    // "Ready" -- the message exists, it just never reached anyone.
    connect(d_->viewport, &ViewportWidget::statusChanged, this, [this](const QString& error) {
        statusBar()->showMessage(error.isEmpty() ? QStringLiteral("Ready") : error);
    });

    statusBar()->showMessage(QStringLiteral("Ready"));
    resize(1280, 800);
    d_->defaultState = saveState();
}

MainWindow::~MainWindow() = default;

ViewportWidget* MainWindow::viewport() const {
    return d_->viewport;
}

void MainWindow::setMesh(const foundation::RenderView& mesh) {
    d_->viewport->setMesh(mesh);
}

void MainWindow::setLitsphere(std::filesystem::path path) {
    d_->viewport->setLitsphere(std::move(path));
}

namespace {

/// Installs @p widget into @p dock, taking ownership either way.
void installInDock(QDockWidget* dock, QWidget* widget) {
    if (widget == nullptr) return;
    if (dock == nullptr) {
        // The contract says the window takes ownership, so it has to hold even
        // when there is no dock -- otherwise the caller leaks a top-level
        // window it has no pointer left to.
        delete widget;
        return;
    }
    // Calling this twice with the same widget must not free it and then
    // reparent freed memory. ASan caught exactly that.
    if (dock->widget() == widget) return;
    // setWidget does not delete the old one, and the placeholder would keep
    // living as an invisible child for the life of the window.
    delete dock->widget();
    dock->setWidget(widget);
}

}  // namespace

void MainWindow::setModellingWidget(QWidget* widget) {
    installInDock(findChild<QDockWidget*>(QStringLiteral("dock.modelling")), widget);
}

void MainWindow::setMaterialsWidget(QWidget* widget) {
    installInDock(findChild<QDockWidget*>(QStringLiteral("dock.materials")), widget);
}

void MainWindow::saveWorkspace() const {
    QSettings s = workspaceSettings();
    s.setValue(kGeom, saveGeometry());
    s.setValue(kState, saveState());
}

void MainWindow::restoreWorkspace() {
    QSettings s = workspaceSettings();
    // A saved layout from an older build may not restore cleanly. restoreState
    // returns false rather than throwing, so falling back to the defaults is
    // just not acting on it.
    const QByteArray geom  = s.value(kGeom).toByteArray();
    const QByteArray state = s.value(kState).toByteArray();
    if (!geom.isEmpty()) restoreGeometry(geom);
    if (!state.isEmpty()) restoreState(state);
}

void MainWindow::resetWorkspace() {
    QSettings s = workspaceSettings();
    s.remove(kGeom);
    s.remove(kState);

    restoreState(d_->defaultState);
    resize(1280, 800);
}

}  // namespace mh::ui
