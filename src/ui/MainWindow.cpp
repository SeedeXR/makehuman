// SPDX-License-Identifier: Apache-2.0
#include "makehuman/ui/MainWindow.h"

#include "makehuman/ui/PanelTitleBar.h"
#include "makehuman/ui/Theme.h"
#include "makehuman/ui/ViewportWidget.h"
#include "makehuman/ui/Workspace.h"

#include <QAction>
#include <QDir>
#include <QDockWidget>
#include <QFile>
#include <QFileInfo>
#include <QInputDialog>
#include <QJsonDocument>
#include <QKeySequence>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMenuBar>
#include <QSaveFile>
#include <QSettings>
#include <QStandardPaths>
#include <QUndoStack>

#include <QStatusBar>
#include <algorithm>

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
    QMenu* savedMenu{};
    QUndoStack* undo{};
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

    // Nested and tabbed docking, with Qt's own drop indicators while dragging.
    // Without AllowNestedDocks a dock can only sit in one of the four areas, so
    // "snapping" has nothing to snap into.
    setDockNestingEnabled(true);
    setDockOptions(QMainWindow::AnimatedDocks | QMainWindow::AllowNestedDocks |
                   QMainWindow::AllowTabbedDocks | QMainWindow::GroupedDragging);

    QMenu* file              = menuBar()->addMenu(tr("&File"));
    const auto addFileAction = [&](const QString& objectName, const QString& text,
                                   QKeySequence::StandardKey key, const char* iconName,
                                   void (MainWindow::*signal)()) {
        QAction* a = file->addAction(text);
        a->setObjectName(objectName);
        a->setShortcut(key);
        a->setIcon(theme::icon(iconName, theme::palette().textSecondary, 16));
        connect(a, &QAction::triggered, this, signal);
    };
    addFileAction(QStringLiteral("file.open"), tr("Open…"), QKeySequence::Open, "folder-open",
                  &MainWindow::openRequested);
    addFileAction(QStringLiteral("file.save"), tr("Save"), QKeySequence::Save, "save",
                  &MainWindow::saveRequested);
    addFileAction(QStringLiteral("file.saveAs"), tr("Save As…"), QKeySequence::SaveAs, "upload",
                  &MainWindow::saveAsRequested);

    // Qt builds the actions, so the text follows the command ("Undo Change
    // head/head-oval") and they enable and disable themselves with the stack.
    d_->undo            = new QUndoStack(this);
    QMenu* edit         = menuBar()->addMenu(tr("&Edit"));
    QAction* undoAction = d_->undo->createUndoAction(this, tr("Undo"));
    undoAction->setObjectName(QStringLiteral("edit.undo"));
    undoAction->setShortcut(QKeySequence::Undo);
    QAction* redoAction = d_->undo->createRedoAction(this, tr("Redo"));
    redoAction->setObjectName(QStringLiteral("edit.redo"));
    redoAction->setShortcut(QKeySequence::Redo);
    edit->addAction(undoAction);
    edit->addAction(redoAction);

    QMenu* workspace = menuBar()->addMenu(tr("&Workspace"));
    int index        = 0;
    for (const WorkspacePreset& preset : workspacePresets()) {
        const QString name = preset.name;
        QAction* a         = workspace->addAction(name);
        a->setObjectName(QStringLiteral("workspace.") + name);
        a->setShortcut(QKeySequence(QStringLiteral("Ctrl+%1").arg(++index)));
        connect(a, &QAction::triggered, this, [this, name] { applyWorkspacePreset(name); });
    }

    // The user's own layouts, rebuilt whenever the menu opens: the directory can
    // gain files between openings, including from another window.
    workspace->addSeparator();
    d_->savedMenu = workspace->addMenu(tr("Saved Layouts"));
    d_->savedMenu->menuAction()->setObjectName(QStringLiteral("workspace.saved"));
    connect(d_->savedMenu, &QMenu::aboutToShow, this, [this] {
        d_->savedMenu->clear();
        for (const QString& name : namedWorkspaces()) {
            QAction* a = d_->savedMenu->addAction(name);
            a->setObjectName(QStringLiteral("workspace.saved.") + name);
            connect(a, &QAction::triggered, this, [this, name] { loadNamedWorkspace(name); });
        }
    });

    QAction* saveAsAction = workspace->addAction(tr("Save Workspace As…"));
    saveAsAction->setObjectName(QStringLiteral("workspace.saveAs"));
    connect(saveAsAction, &QAction::triggered, this, [this] {
        bool accepted      = false;
        const QString name = QInputDialog::getText(this, tr("Save Workspace"), tr("Name:"),
                                                   QLineEdit::Normal, QString{}, &accepted);
        if (!accepted) return;
        if (!saveWorkspaceAs(name)) {
            statusBar()->showMessage(tr("Could not save workspace \u201C%1\u201D").arg(name), 4000);
            return;
        }
        statusBar()->showMessage(tr("Saved workspace \u201C%1\u201D").arg(name), 3000);
    });

    workspace->addSeparator();
    QAction* resetAction = workspace->addAction(tr("Reset Workspace"));
    resetAction->setObjectName(QStringLiteral("workspace.reset"));
    connect(resetAction, &QAction::triggered, this, &MainWindow::resetWorkspace);

    statusBar()->showMessage(QStringLiteral("Ready"));
    resize(1280, 800);
    // Captured AFTER every dock exists, so a preset restoring it gets them all.
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

QUndoStack* MainWindow::undoStack() const {
    return d_->undo;
}

QString MainWindow::workspaceDirectory() {
    // AppDataLocation is ~/Library/Application Support/<app> on macOS, which is
    // what design.md 6.4 specifies. Deliberately does NOT create the directory:
    // merely listing workspaces should not leave one behind for a user who has
    // never saved any.
    return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) +
           QStringLiteral("/workspaces");
}

bool MainWindow::applyWorkspacePreset(const QString& name) {
    const auto& presets = workspacePresets();
    const auto found =
        std::find_if(presets.begin(), presets.end(), [&](const auto& p) { return p.name == name; });
    if (found == presets.end()) return false;

    // From the shipped layout each time, so switching preset to preset does not
    // accumulate whatever the previous one moved.
    restoreState(d_->defaultState);
    for (QDockWidget* dock : findChildren<QDockWidget*>()) {
        dock->setVisible(found->visibleDocks.contains(dock->objectName()));
    }
    // The first dock named is the one the preset is about, so it gets the room.
    if (!found->visibleDocks.isEmpty()) {
        if (auto* dock = findChild<QDockWidget*>(found->visibleDocks.front())) {
            resizeDocks({dock}, {380}, Qt::Horizontal);
        }
    }
    statusBar()->showMessage(tr("Workspace: %1").arg(name), 3000);
    return true;
}

bool MainWindow::saveWorkspaceAs(const QString& name) const {
    // The name reaches this from a free-text field, and it is about to become a
    // path.
    if (!isValidWorkspaceName(name)) return false;

    WorkspaceFile out;
    out.schemaVersion = kWorkspaceSchemaVersion;
    out.name          = name;
    out.state         = saveState();
    out.geometry      = saveGeometry();

    const QString dir = workspaceDirectory();
    if (!QDir().mkpath(dir)) return false;

    // QSaveFile, not QFile: a plain write() returns the byte count before
    // anything reaches the disk, so a full or read-only volume reported success
    // and left an empty file that the next load could not parse.
    QSaveFile f(dir + QLatin1Char('/') + name + QStringLiteral(".json"));
    if (!f.open(QIODevice::WriteOnly)) return false;
    const QByteArray json = QJsonDocument(toJson(out)).toJson(QJsonDocument::Indented);
    if (f.write(json) != json.size()) return false;
    return f.commit();
}

bool MainWindow::loadNamedWorkspace(const QString& name) {
    if (!isValidWorkspaceName(name)) return false;

    QFile f(workspaceDirectory() + QLatin1Char('/') + name + QStringLiteral(".json"));
    if (!f.open(QIODevice::ReadOnly)) return false;

    QJsonParseError error{};
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll(), &error);
    if (error.error != QJsonParseError::NoError || !doc.isObject()) return false;

    const auto parsed = workspaceFromJson(doc.object());
    if (!parsed) return false;

    // Geometry before state: restoreState places docks relative to the window
    // size, so doing it the other way sizes them against the old one.
    if (!parsed->geometry.isEmpty()) restoreGeometry(parsed->geometry);
    // restoreState's own result, not `true` for an empty blob -- reporting
    // success while nothing moved is the failure this whole path guards.
    return restoreState(parsed->state);
}

QStringList MainWindow::namedWorkspaces() const {
    QStringList names;
    for (const QFileInfo& info :
         QDir(workspaceDirectory())
             .entryInfoList({QStringLiteral("*.json")}, QDir::Files, QDir::Name)) {
        names << info.completeBaseName();
    }
    return names;
}

void MainWindow::setDocumentPath(const QString& path) {
    // The file name, not the path: a title bar full of directories tells the
    // user nothing they were looking for.
    setWindowTitle(path.isEmpty()
                       ? QStringLiteral("MakeHuman")
                       : QStringLiteral("MakeHuman — %1").arg(QFileInfo(path).fileName()));
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
