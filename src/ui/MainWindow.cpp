// SPDX-License-Identifier: Apache-2.0
#include "makehuman/ui/MainWindow.h"

#include "makehuman/ui/UndoCommands.h"

#include "makehuman/ui/Language.h"
#include "makehuman/ui/MouseBindings.h"
#include "makehuman/ui/PanelTitleBar.h"
#include "makehuman/ui/Shortcuts.h"
#include "makehuman/ui/ShortcutsDialog.h"
#include "makehuman/ui/Theme.h"
#include "makehuman/ui/ViewportWidget.h"
#include "makehuman/ui/Workspace.h"

#include <QAction>
#include <QActionGroup>
#include <QApplication>
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
#include <QPointer>
#include <QSaveFile>
#include <QSettings>
#include <QTabWidget>

#include <QStandardPaths>
#include <QUndoStack>
#include <cstdio>

#include <QStatusBar>
#include <QToolBar>
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

    dock->setAccessibleName(title);

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

/// One retranslatable label: what to set, and the ENGLISH source to look up.
///
/// The alternative is `findChild` by object name at retranslate time, which
/// only reaches things that were given a name and silently skips the rest.
struct Translatable {
    QPointer<QObject> target;
    QByteArray source;
};

}  // namespace

struct MainWindow::Impl {
    ViewportWidget* viewport{};
    std::vector<Translatable> texts;
    std::unique_ptr<JsonTranslator> translator;
    QString language;
    QMenu* languageMenu{};
    std::filesystem::path languageDir;
    /// Categories, so a preset can resolve "everything" at apply time rather
    /// than naming docks literally. Only categories() was ever read from the
    /// registry, so the list is what is kept.
    QStringList categories;
    QMenu* savedMenu{};
    QUndoStack* undo{};
    Units units{Units::Metric};
    Weight weight{Weight::Percent};
    // Aligned with the shipped default so the two cannot contradict each other,
    // but the constructor's stored-value read is the authority: it assigns this
    // unconditionally before anything can observe it.
    Skinning skinning{Skinning::DualQuaternion};
    bool smooth{false};
    bool wireframe{false};
    /// True from the start, exactly as the reference's `_posed` is: what makes
    /// an unposed character unposed is having no pose, not this flag.
    bool poseEnabled{true};
    bool grid{false};
    /// The reference keeps this on the human rather than in its settings, and
    /// so do we: a session mode, not a stored preference.
    bool symmetryMode{false};
    /// The reference's persistent macro line. A permanent status-bar widget,
    /// because showMessage is transient and every other message would wipe it.
    QLabel* macroStatus{};
    /// The shipped layout, captured before any saved one is restored. Resetting
    /// is then just restoring it, which also covers docks added later -- the
    /// manual re-dock it replaces did not.
    QByteArray defaultState;
};

QString MainWindow::dockObjectName(const QString& category) {
    // Plain lower-cased concatenation. An earlier version folded punctuation
    // to '-' "so the name stays usable in a QSS ID selector" -- measured, that
    // is false: the `dock.` prefix's own period already forces `#dock\.name`,
    // and once escaping, `#dock\.arms\ and\ legs` matches fine. No QSS in the
    // repo selects these at all. The folding bought nothing and let two
    // categories collide on one object name, which saveState keys on.
    return QStringLiteral("dock.") + category.toLower();
}

MainWindow::MainWindow(std::filesystem::path shaderDir, TaskRegistry tasks, QWidget* parent)
    : QMainWindow(parent), d_(std::make_unique<Impl>()) {
    d_->categories = tasks.categories();
    setObjectName(QStringLiteral("MainWindow"));
    setWindowTitle(QStringLiteral("MakeHuman"));

    d_->viewport = new ViewportWidget(std::move(shaderDir), this);
    // The 3D view is the central control and takes keyboard focus for orbiting,
    // so it needs a name and a description of what the keys do.
    d_->viewport->setAccessibleName(tr("3D viewport"));
    d_->viewport->setAccessibleDescription(tr("The character. Drag to orbit, scroll to zoom."));
    d_->viewport->setFocusPolicy(Qt::StrongFocus);
    setCentralWidget(d_->viewport);

    // One dock per registered category, in registration order -- not from a
    // hardcoded pair, and not from what a file happens to be called.
    // The first goes left and the rest right, which is the shipped layout the
    // presets restore.
    bool first = true;
    for (const QString& category : tasks.categories()) {
        const Qt::DockWidgetArea area = first ? Qt::LeftDockWidgetArea : Qt::RightDockWidgetArea;
        // The category name is a DATA string -- it comes from the task
        // registry, not from tr() -- and the shipped dictionaries do carry
        // these: Materials, Modelling, Skeleton, Files, Settings and Pose all
        // have entries. Registered so it re-translates on a live switch.
        // The title is passed in as well as registered: makeDock derives the
        // dock's accessibleName and its placeholder body from it, and neither
        // follows a later setWindowTitle. registerText then keeps the visible
        // title live across a language switch.
        // TITLE for what is shown, category ID for the object name. The id is
        // a persisted key -- `saveState` records it -- so it never changes;
        // the title is free to.
        const QByteArray shown = tasks.title(category).toUtf8();
        QDockWidget* dock      = makeDock(QCoreApplication::translate("", shown.constData()),
                                          dockObjectName(category), area, this);
        registerText(dock, shown.constData());
        addDockWidget(area, dock);
        first = false;
    }

    // A renderer failure is otherwise a black rectangle and a status bar saying
    // "Ready" -- the message exists, it just never reached anyone.
    connect(d_->viewport, &ViewportWidget::statusChanged, this, [this](const QString& error) {
        statusBar()->showMessage(error.isEmpty() ? QStringLiteral("Ready") : error);
    });

    // Nested and tabbed docking, with Qt's own drop indicators while dragging.
    // Without AllowNestedDocks a dock can only sit in one of the four areas, so
    // "snapping" has nothing to snap into.
    setDockNestingEnabled(true);
    setDockOptions(dockOptionsFor(theme::reduceMotion()));
    // Panel tabs at the TOP. Qt's default for dock areas is `South`, and a
    // screenshot of the Tabbed workspace is what showed why that is wrong here:
    // the "Modelling | Materials" bar sat under 900 pixels of sliders, far from
    // the panel title it belongs to, and read as a status strip rather than a
    // switcher. Every DCC this port is measured against puts them on top.
    setTabPosition(Qt::AllDockWidgetAreas, QTabWidget::North);

    QMenu* file = menuBar()->addMenu(tr("&File"));
    registerText(file, QT_TR_NOOP("&File"));
    const auto addFileAction = [&](const QString& objectName, const char* source,
                                   QKeySequence::StandardKey key, const char* iconName,
                                   void (MainWindow::*signal)()) {
        QAction* a = file->addAction(QCoreApplication::translate("", source));
        registerText(a, source);
        a->setObjectName(objectName);
        a->setShortcut(key);
        a->setIcon(theme::icon(iconName, theme::palette().textSecondary, 16));
        connect(a, &QAction::triggered, this, signal);
    };
    addFileAction(QStringLiteral("file.open"), QT_TR_NOOP("Open…"), QKeySequence::Open,
                  "folder-open", &MainWindow::openRequested);
    addFileAction(QStringLiteral("file.save"), QT_TR_NOOP("Save"), QKeySequence::Save, "save",
                  &MainWindow::saveRequested);
    addFileAction(QStringLiteral("file.saveAs"), QT_TR_NOOP("Save As…"), QKeySequence::SaveAs,
                  "copy", &MainWindow::saveAsRequested);
    file->addSeparator();
    // Separated from the three above because it writes a DIFFERENT kind of
    // file: Open/Save/Save As round-trip a `.mhm`, and this hands the character
    // to another tool and cannot be read back the same way.
    addFileAction(QStringLiteral("file.export"), QT_TR_NOOP("Export…"), QKeySequence::UnknownKey,
                  "upload", &MainWindow::exportRequested);
    // A production render is a third kind of output: not a `.mhm` to reopen and
    // not a model for another tool, but a picture. `image` rather than `camera`
    // -- the toolbar's camera is Grab Screen, which captures the WINDOW, and
    // one glyph for two different things would be worse than two.
    addFileAction(QStringLiteral("file.render"), QT_TR_NOOP("Render…"), QKeySequence::UnknownKey,
                  "image", &MainWindow::renderRequested);

    // Qt builds the actions, so the text follows the command ("Undo Change
    // head/head-oval") and they enable and disable themselves with the stack.
    d_->undo    = new QUndoStack(this);
    QMenu* edit = menuBar()->addMenu(tr("&Edit"));
    registerText(edit, QT_TR_NOOP("&Edit"));
    QAction* undoAction = d_->undo->createUndoAction(this, tr("Undo"));
    undoAction->setObjectName(QStringLiteral("edit.undo"));
    undoAction->setShortcut(QKeySequence::Undo);
    // Qt builds these two, so they are the pair most easily forgotten: they
    // shipped with no icon at all until the icon audit walked every action and
    // named them. `theme::icon` returns a null QIcon for a missing file, so
    // there was nothing to see and nothing to log.
    undoAction->setIcon(theme::icon("undo-2", theme::palette().textSecondary, 16));
    QAction* redoAction = d_->undo->createRedoAction(this, tr("Redo"));
    redoAction->setObjectName(QStringLiteral("edit.redo"));
    redoAction->setShortcut(QKeySequence::Redo);
    redoAction->setIcon(theme::icon("redo-2", theme::palette().textSecondary, 16));
    edit->addAction(undoAction);
    edit->addAction(redoAction);
    edit->addSeparator();
    QAction* randomise =
        edit->addAction(theme::icon("refresh-cw", theme::palette().textSecondary, 16),
                        QCoreApplication::translate("", QT_TR_NOOP("Randomise")));
    registerText(randomise, QT_TR_NOOP("Randomise"));
    randomise->setObjectName(QStringLiteral("edit.randomise"));
    randomise->setShortcut(QKeySequence(QStringLiteral("Ctrl+R")));
    connect(randomise, &QAction::triggered, this, &MainWindow::randomiseRequested);

    // Symmetry, the reference's third toolbar group (`core/mhmain.py:1516`).
    // Here rather than on the toolbar: the two commands differ only in
    // DIRECTION, lucide ships one mirror glyph and no left/right pair, and two
    // identical icon-only buttons would be a coin toss. Words say it; the empty
    // `resources/icons/custom/` is where a proper pair would go.
    const auto addSymmetry = [&](const char* label, const QString& objectName, char targetSide) {
        QAction* a =
            edit->addAction(theme::icon("flip-horizontal-2", theme::palette().textSecondary, 16),
                            QCoreApplication::translate("", label));
        registerText(a, label);
        a->setObjectName(objectName);
        connect(a, &QAction::triggered, this,
                [this, targetSide] { emit symmetryRequested(targetSide); });
    };
    addSymmetry(QT_TR_NOOP("Symmetry Left \u2192 Right"), QStringLiteral("edit.symmetryLtoR"), 'r');
    addSymmetry(QT_TR_NOOP("Symmetry Right \u2192 Left"), QStringLiteral("edit.symmetryRtoL"), 'l');

    // ...and the same idea as a MODE (`core/mhmain.py:1524`): with it on,
    // dragging one side's slider drags the other. Checkable, and the only one
    // of the three that is not a one-shot command.
    QAction* symmetryMode =
        edit->addAction(theme::icon("flip-horizontal-2", theme::palette().textSecondary, 16),
                        QCoreApplication::translate("", QT_TR_NOOP("Symmetry While Editing")));
    registerText(symmetryMode, QT_TR_NOOP("Symmetry While Editing"));
    symmetryMode->setObjectName(QStringLiteral("edit.symmetryMode"));
    symmetryMode->setCheckable(true);
    connect(symmetryMode, &QAction::toggled, this, [this](bool on) { d_->symmetryMode = on; });

    QMenu* workspace = menuBar()->addMenu(tr("&Workspace"));
    registerText(workspace, QT_TR_NOOP("&Workspace"));
    int index = 0;
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
    registerText(d_->savedMenu, QT_TR_NOOP("Saved Layouts"));
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
    registerText(saveAsAction, QT_TR_NOOP("Save Workspace As…"));
    saveAsAction->setObjectName(QStringLiteral("workspace.saveAs"));
    saveAsAction->setIcon(theme::icon("save", theme::palette().textSecondary, 16));
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
    registerText(resetAction, QT_TR_NOOP("Reset Workspace"));
    resetAction->setObjectName(QStringLiteral("workspace.reset"));
    // Without this the toolbar fell back to the action's TEXT and shipped a
    // "Reset Workspace" word button between the icons -- visible in a
    // screenshot, invisible to the audit, because the audit exempted the whole
    // `workspace.` prefix rather than just the named layouts.
    resetAction->setIcon(theme::icon("rotate-ccw", theme::palette().textSecondary, 16));
    connect(resetAction, &QAction::triggered, this, &MainWindow::resetWorkspace);

    // Settings. The reference has a whole Settings tab; this holds the one
    // setting that currently changes anything on screen, and grows as more
    // earn their place. A menu of controls that do nothing would be worse than
    // a short menu.
    QMenu* settings = menuBar()->addMenu(tr("&Settings"));
    registerText(settings, QT_TR_NOOP("&Settings"));
    // Shortcuts first: it is the one entry here that answers a question a user
    // arrives with ("how do I drive this"), and it covers the mouse too.
    QAction* rebind = settings->addAction(tr("Shortcuts…"));
    registerText(rebind, QT_TR_NOOP("Shortcuts…"));
    rebind->setObjectName(QStringLiteral("settings.shortcuts"));
    rebind->setIcon(theme::icon("sliders-horizontal", theme::palette().textSecondary, 16));
    connect(rebind, &QAction::triggered, this, [this] {
        QSettings stored = workspaceSettings();
        ShortcutsDialog dialog(*this, d_->viewport->mouseBindings(), stored, this);
        (void)dialog.exec();
    });
    settings->addSeparator();

    QMenu* unitsMenu = settings->addMenu(tr("Units"));
    registerText(unitsMenu, QT_TR_NOOP("Units"));
    unitsMenu->menuAction()->setObjectName(QStringLiteral("settings.units"));
    unitsMenu->menuAction()->setIcon(theme::icon("scale-3d", theme::palette().textSecondary, 16));

    // Restored before the actions are built, so the right one starts checked.
    d_->units = workspaceSettings().value(QStringLiteral("units"), 0).toInt() == 1 ? Units::Imperial
                                                                                   : Units::Metric;

    auto* unitGroup = new QActionGroup(this);
    unitGroup->setExclusive(true);
    const auto addUnit = [&](const char* label, const QString& objectName, Units value) {
        QAction* a = unitsMenu->addAction(QCoreApplication::translate("", label));
        registerText(a, label);
        a->setObjectName(objectName);
        a->setCheckable(true);
        a->setChecked(d_->units == value);
        unitGroup->addAction(a);
        connect(a, &QAction::triggered, this, [this, value] {
            if (d_->units == value) return;
            d_->units = value;
            // Persisted immediately rather than at shutdown: a crash should not
            // lose a preference the user has already seen take effect.
            workspaceSettings().setValue(QStringLiteral("units"), value == Units::Imperial ? 1 : 0);
            emit unitsChanged(value);
        });
    };
    addUnit(QT_TR_NOOP("Metric (cm)"), QStringLiteral("settings.units.metric"), Units::Metric);
    addUnit(QT_TR_NOOP("Imperial (in.)"), QStringLiteral("settings.units.imperial"),
            Units::Imperial);

    // Real weight, beside Units because it is the same kind of preference: how
    // a measurement is written, not what the model is. The reference keys it
    // off a `real_weight` setting (`guimodifier.py:168`); the percentage stays
    // the default, as it is there.
    QAction* realWeight = settings->addAction(tr("Real weight (kg)"));
    registerText(realWeight, QT_TR_NOOP("Real weight (kg)"));
    realWeight->setObjectName(QStringLiteral("settings.realweight"));
    // Every menu action carries an icon -- a rule the icon test enforces, and
    // it caught this one. `person-standing` rather than the Units menu's
    // `scale-3d`: this is the figure's mass, not the unit it is written in.
    realWeight->setIcon(theme::icon("person-standing", theme::palette().textSecondary, 16));
    realWeight->setCheckable(true);
    d_->weight = workspaceSettings().value(QStringLiteral("realWeight"), false).toBool()
                     ? Weight::Real
                     : Weight::Percent;
    realWeight->setChecked(d_->weight == Weight::Real);
    connect(realWeight, &QAction::toggled, this, [this](bool on) {
        d_->weight = on ? Weight::Real : Weight::Percent;
        workspaceSettings().setValue(QStringLiteral("realWeight"), on);
        // Reuses unitsChanged: the one consumer rebuilds the whole status line
        // from `units()` and `weightMode()`, so a second signal would be a
        // second thing to forget to connect.
        emit unitsChanged(d_->units);
    });

    // Skinning, under a separator: Units and Real weight change how a number is
    // WRITTEN, this changes the geometry on screen. It is the one setting here
    // that costs a re-pose, which is why the app is told rather than polling.
    //
    // Radio entries rather than one "Dual quaternion" checkbox: unchecked would
    // have to be read as "linear blend" without ever saying so, and the choice
    // is between two named methods, not between a feature and its absence.
    settings->addSeparator();
    QMenu* skinningMenu = settings->addMenu(tr("Skinning"));
    registerText(skinningMenu, QT_TR_NOOP("Skinning"));
    skinningMenu->menuAction()->setObjectName(QStringLiteral("settings.skinning"));
    skinningMenu->menuAction()->setIcon(theme::icon("bone", theme::palette().textSecondary, 16));

    // Restored before the actions are built, so the right one starts checked.
    // Stored as the word `--skinning` takes rather than an int: someone reading
    // the ini file sees the same spelling they would type. Anything else --
    // including a fresh install with no stored value -- reads as dual
    // quaternion, which is the shipped default; only the literal word `linear`
    // opts out.
    d_->skinning =
        workspaceSettings().value(QStringLiteral("skinning")).toString() == QLatin1String("linear")
            ? Skinning::Linear
            : Skinning::DualQuaternion;

    auto* skinningGroup = new QActionGroup(this);
    skinningGroup->setExclusive(true);
    const auto addSkinning = [&](const char* label, const QString& objectName, Skinning value) {
        QAction* a = skinningMenu->addAction(QCoreApplication::translate("", label));
        registerText(a, label);
        a->setObjectName(objectName);
        a->setCheckable(true);
        a->setChecked(d_->skinning == value);
        skinningGroup->addAction(a);
        connect(a, &QAction::triggered, this, [this, value] {
            if (d_->skinning == value) return;
            d_->skinning = value;
            workspaceSettings().setValue(QStringLiteral("skinning"),
                                         value == Skinning::DualQuaternion
                                             ? QStringLiteral("dqs")
                                             : QStringLiteral("linear"));
            emit skinningChanged(value);
        });
    };
    addSkinning(QT_TR_NOOP("Linear blend"), QStringLiteral("settings.skinning.linear"),
                Skinning::Linear);
    addSkinning(QT_TR_NOOP("Dual quaternion"), QStringLiteral("settings.skinning.dqs"),
                Skinning::DualQuaternion);

    // View: the reference's Camera toolbar (`core/mhmain.py:1750-1756`) on its
    // own menu, with its own numpad bindings (`:184-190`).
    //
    // A menu rather than six toolbar buttons: lucide has no front/back/left/
    // right glyphs, and six icons only distinguishable by hovering are worse
    // than seven words -- the same call Symmetry made. Keypad shortcuts, not
    // the number row: Qt matches shortcuts before the focus widget sees the
    // key, so a bare "1" would be stolen from every spin box in the window.
    QMenu* viewMenu = menuBar()->addMenu(tr("&View"));
    registerText(viewMenu, QT_TR_NOOP("&View"));

    const auto addAxisView = [&](const char* label, const QString& objectName, const char* glyph,
                                 const QKeySequence& key, float yaw) {
        QAction* a = viewMenu->addAction(theme::icon(glyph, theme::palette().textSecondary, 16),
                                         QCoreApplication::translate("", label));
        registerText(a, label);
        a->setObjectName(objectName);
        a->setShortcut(key);
        connect(a, &QAction::triggered, this, [this, yaw] {
            // Rotation only. Distance and pan are kept, because an axis view
            // answers "show me this side", not "start again" -- that is Reset,
            // below. The four of these are level by definition, so the pitch is
            // written here rather than passed in as a 0 four times.
            render::Camera c = d_->viewport->camera();
            c.yawDegrees     = yaw;
            c.pitchDegrees   = 0.0F;
            d_->viewport->setCamera(c);
        });
    };
    // The reference's own angles: front [0,0,0], right [0,90,0], top [90,0,0]
    // and their opposites (`mhmain.py:1612-1628`). Top and bottom are clamped
    // to the limit the MOUSE obeys, so the first drag after one does not jump.
    addAxisView(QT_TR_NOOP("Front"), QStringLiteral("view.camera.front"), "user",
                QKeySequence(Qt::KeypadModifier | Qt::Key_1), 0.0F);
    addAxisView(QT_TR_NOOP("Back"), QStringLiteral("view.camera.back"), "rotate-3d",
                QKeySequence(Qt::ControlModifier | Qt::KeypadModifier | Qt::Key_1), 180.0F);
    addAxisView(QT_TR_NOOP("Right"), QStringLiteral("view.camera.right"), "chevron-right",
                QKeySequence(Qt::KeypadModifier | Qt::Key_3), 90.0F);
    addAxisView(QT_TR_NOOP("Left"), QStringLiteral("view.camera.left"), "chevron-left",
                QKeySequence(Qt::ControlModifier | Qt::KeypadModifier | Qt::Key_3), -90.0F);

    // Top and bottom keep the current heading -- only the elevation changes --
    // so looking down on the model does not also spin it back to front-on.
    const auto addPitchView = [&](const char* label, const QString& objectName, const char* glyph,
                                  const QKeySequence& key, float pitch) {
        QAction* a = viewMenu->addAction(theme::icon(glyph, theme::palette().textSecondary, 16),
                                         QCoreApplication::translate("", label));
        registerText(a, label);
        a->setObjectName(objectName);
        a->setShortcut(key);
        connect(a, &QAction::triggered, this, [this, pitch] {
            render::Camera c = d_->viewport->camera();
            c.pitchDegrees   = pitch;
            d_->viewport->setCamera(c);
        });
    };
    addPitchView(QT_TR_NOOP("Top"), QStringLiteral("view.camera.top"), "chevron-up",
                 QKeySequence(Qt::KeypadModifier | Qt::Key_7), ViewportWidget::kMaxPitchDegrees);
    addPitchView(QT_TR_NOOP("Bottom"), QStringLiteral("view.camera.bottom"), "chevron-down",
                 QKeySequence(Qt::ControlModifier | Qt::KeypadModifier | Qt::Key_7),
                 -ViewportWidget::kMaxPitchDegrees);

    viewMenu->addSeparator();
    QAction* resetCam = viewMenu->addAction(
        theme::icon("focus", theme::palette().textSecondary, 16), tr("Reset Camera"));
    registerText(resetCam, QT_TR_NOOP("Reset Camera"));
    resetCam->setObjectName(QStringLiteral("view.camera.reset"));
    resetCam->setShortcut(QKeySequence(QStringLiteral(".")));
    connect(resetCam, &QAction::triggered, this, [this] {
        // Rotation, pan AND zoom, which is what makes this different from
        // Front (`resetView`, `mhmain.py:1631-1636`).
        d_->viewport->setCamera(render::Camera{});
    });

    buildLanguageMenu();

    // The top toolbar (owner directive 8; the reference has one and we had
    // none). It re-uses the QActions the menus already own rather than building
    // parallel ones -- two actions for one command is how a button drifts out
    // of step with its menu item, each keeping its own enabled state, shortcut
    // and translation registration. Undo and redo make that concrete: they
    // grey out with the stack only because there is exactly one of each.
    //
    // The symmetry and body-part-camera groups in the reference screenshot are
    // NOT here, and neither is wireframe or grid: nothing can mirror, draw
    // edges or lay down a floor yet, so every one of those buttons would be a
    // painted no-op. They arrive with the behaviour they need. Smooth is the
    // one of that group that HAS its behaviour -- `--subdivide` and the
    // `.mhm`'s subdivide line have both shipped for milestones -- so it is
    // here.
    auto* bar = new QToolBar(tr("Main"), this);
    registerText(bar, QT_TR_NOOP("Main"));
    bar->setObjectName(QStringLiteral("toolbar.main"));
    bar->setMovable(false);
    bar->setIconSize(QSize(18, 18));
    addToolBar(Qt::TopToolBarArea, bar);

    for (const QString& name : {QStringLiteral("file.open"), QStringLiteral("file.save"),
                                QStringLiteral("file.saveAs")}) {
        // findChild rather than a captured pointer: the actions are built above
        // by a lambda that does not hand them back, and looking them up by the
        // objectName the tests also use keeps one source of truth for the name.
        if (QAction* a = findChild<QAction*>(name)) bar->addAction(a);
    }
    bar->addSeparator();
    bar->addAction(undoAction);
    bar->addAction(redoAction);
    bar->addAction(resetAction);
    bar->addSeparator();

    // Smooth: the reference's own View-toolbar toggle and its own shortcut
    // (`core/mhmain.py:1733` and `:170`). Checkable and NOT in an action group,
    // so the icon audit rightly demands a glyph; `spline` is the smoothed curve
    // and was vendored and unused.
    QAction* smooth =
        bar->addAction(theme::icon("spline", theme::palette().textSecondary, 16), tr("Smooth"));
    registerText(smooth, QT_TR_NOOP("Smooth"));
    smooth->setObjectName(QStringLiteral("view.smooth"));
    smooth->setCheckable(true);
    smooth->setShortcut(QKeySequence(QStringLiteral("Alt+S")));
    connect(smooth, &QAction::toggled, this, [this](bool on) {
        if (d_->smooth == on) return;
        d_->smooth = on;
        // No QSettings: this belongs to the character, not to the application.
        emit smoothChanged(on);
    });

    // Wireframe, beside Smooth and with the reference's own Ctrl+F
    // (`core/mhmain.py:1498`). `box` is what `design.md` 5 gives it; `grid-3x3`
    // is the Grid toggle's and was taken by mistake here.
    QAction* wire =
        bar->addAction(theme::icon("box", theme::palette().textSecondary, 16), tr("Wireframe"));
    registerText(wire, QT_TR_NOOP("Wireframe"));
    wire->setObjectName(QStringLiteral("view.wireframe"));
    wire->setCheckable(true);
    wire->setShortcut(QKeySequence(QStringLiteral("Ctrl+F")));
    connect(wire, &QAction::toggled, this, [this](bool on) {
        if (d_->wireframe == on) return;
        d_->wireframe = on;
        emit wireframeChanged(on);
    });

    // The pose toggle (`core/mhmain.py:1735`). Disabled until a pose is loaded:
    // the reference's `isPosed()` is `_posed AND isPoseable()`, and a button
    // that cannot change the picture is worse than no button.
    QAction* poseToggle = bar->addAction(
        theme::icon("person-standing", theme::palette().textSecondary, 16), tr("Pose"));
    registerText(poseToggle, QT_TR_NOOP("Pose"));
    poseToggle->setObjectName(QStringLiteral("view.pose"));
    poseToggle->setCheckable(true);
    poseToggle->setChecked(true);
    poseToggle->setEnabled(false);
    connect(poseToggle, &QAction::toggled, this, [this](bool on) {
        if (d_->poseEnabled == on) return;
        d_->poseEnabled = on;
        emit poseEnabledChanged(on);
    });

    // The grid, last of the four view modes and in the order `design.md` 6.1
    // draws them. `grid-3x3` is its glyph in the icon map -- Wireframe borrowed
    // it for two chunks, which is what led to that map being enforced.
    QAction* gridToggle =
        bar->addAction(theme::icon("grid-3x3", theme::palette().textSecondary, 16), tr("Grid"));
    registerText(gridToggle, QT_TR_NOOP("Grid"));
    gridToggle->setObjectName(QStringLiteral("view.grid"));
    gridToggle->setCheckable(true);
    connect(gridToggle, &QAction::toggled, this, [this](bool on) {
        if (d_->grid == on) return;
        d_->grid = on;
        emit gridChanged(on);
    });
    bar->addSeparator();

    QAction* shot = bar->addAction(theme::icon("camera", theme::palette().textSecondary, 16),
                                   tr("Grab Screen"));
    registerText(shot, QT_TR_NOOP("Grab Screen"));
    shot->setObjectName(QStringLiteral("view.screenshot"));
    connect(shot, &QAction::triggered, this, &MainWindow::screenshotRequested);

    // Permanent, so a transient showMessage cannot wipe it. addPermanentWidget
    // right-aligns it, which is where the reference puts the same numbers.
    d_->macroStatus = new QLabel(this);
    d_->macroStatus->setObjectName(QStringLiteral("status.macro"));
    statusBar()->addPermanentWidget(d_->macroStatus);

    statusBar()->showMessage(QStringLiteral("Ready"));
    resize(1280, 800);
    // Captured AFTER every dock exists, so a preset restoring it gets them all.
    d_->defaultState = saveState();

    // Shortcut overrides, LAST: every action has to exist before it can be
    // rebound, and `apply` records each shipped sequence as it goes.
    //
    // Reported on stderr rather than swallowed. A shortcut that silently did
    // not take is the kind of thing a user blames on their keyboard, and the
    // message names the entry and what is wrong with it.
    QSettings stored = workspaceSettings();
    for (const QString& problem : shortcuts::apply(*this, stored)) {
        std::fprintf(stderr, "shortcuts: %s\n", problem.toUtf8().constData());
    }
    // Same treatment for the camera gestures: applied here, reported rather
    // than swallowed. A drag that silently does nothing is blamed on the mouse.
    for (const QString& problem : d_->viewport->mouseBindings().apply(stored)) {
        std::fprintf(stderr, "mouse: %s\n", problem.toUtf8().constData());
    }
}

MainWindow::~MainWindow() = default;

void MainWindow::setMacroStatus(const QString& line) {
    d_->macroStatus->setText(line);
}

QString MainWindow::macroStatus() const {
    return d_->macroStatus->text();
}

Units MainWindow::units() const {
    return d_->units;
}

Weight MainWindow::weightMode() const {
    return d_->weight;
}

Skinning MainWindow::skinning() const {
    return d_->skinning;
}

bool MainWindow::smooth() const {
    return d_->smooth;
}

bool MainWindow::wireframe() const {
    return d_->wireframe;
}

void MainWindow::setWireframe(bool on) {
    // Assign first, then setChecked: see setSmooth.
    d_->wireframe = on;
    if (QAction* a = findChild<QAction*>(QStringLiteral("view.wireframe"))) a->setChecked(on);
}

bool MainWindow::symmetryMode() const {
    return d_->symmetryMode;
}

bool MainWindow::grid() const {
    return d_->grid;
}

void MainWindow::setGrid(bool on) {
    // Assign first, then setChecked: see setSmooth.
    d_->grid = on;
    if (QAction* a = findChild<QAction*>(QStringLiteral("view.grid"))) a->setChecked(on);
}

bool MainWindow::poseEnabled() const {
    return d_->poseEnabled;
}

void MainWindow::setPoseEnabled(bool on) {
    // Assign first, then setChecked: see setSmooth.
    d_->poseEnabled = on;
    if (QAction* a = findChild<QAction*>(QStringLiteral("view.pose"))) a->setChecked(on);
}

void MainWindow::setPoseAvailable(bool available) {
    // Only the enabled state. The flag is deliberately untouched: a user who
    // switched posing off and then changes pose expects it to stay off.
    if (QAction* a = findChild<QAction*>(QStringLiteral("view.pose"))) a->setEnabled(available);
}

void MainWindow::setSmooth(bool on) {
    // The toggled lambda above early-outs when the value already matches, so
    // setChecked moves the tick without emitting smoothChanged -- and would
    // still not emit if that early-out went, because this assigns first.
    d_->smooth = on;
    if (QAction* a = findChild<QAction*>(QStringLiteral("view.smooth"))) a->setChecked(on);
}

void MainWindow::setSkinning(Skinning method) {
    d_->skinning = method;
    // setChecked, not trigger: the menu entries are connected to `triggered`,
    // which setChecked does not raise, so this moves the tick without
    // persisting the choice or asking the app to re-pose. The exclusive
    // QActionGroup unchecks the other one for us.
    if (QAction* a = findChild<QAction*>(method == Skinning::DualQuaternion
                                             ? QStringLiteral("settings.skinning.dqs")
                                             : QStringLiteral("settings.skinning.linear"))) {
        a->setChecked(true);
    }
}

ViewportWidget* MainWindow::viewport() const {
    return d_->viewport;
}

void MainWindow::setMesh(const foundation::RenderView& mesh) {
    d_->viewport->setMesh(mesh);
}

void MainWindow::setMeshes(std::vector<render::MeshInstance> meshes) {
    d_->viewport->setMeshes(std::move(meshes));
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

bool MainWindow::setPanel(const QString& category, QWidget* widget) {
    auto* dock = findChild<QDockWidget*>(dockObjectName(category));
    if (dock == nullptr) {
        // Deliberately does NOT take ownership here. The previous version
        // deleted the widget, so a mistyped category -- "Modeling", the
        // reference's own spelling -- silently freed the panel while the caller
        // went on connecting signals to it. A leak is a far better failure than
        // a use-after-free, and [[nodiscard]] means the caller cannot ignore it.
        return false;
    }
    installInDock(dock, widget);
    return true;
}

QMainWindow::DockOptions MainWindow::dockOptionsFor(bool reduceMotion) {
    // AnimatedDocks is the only animation in the window, so honouring reduce
    // motion (design.md 9) means dropping exactly that flag. The docking
    // behaviour is unchanged; it simply stops sliding.
    QMainWindow::DockOptions options = QMainWindow::AllowNestedDocks |
                                       QMainWindow::AllowTabbedDocks | QMainWindow::GroupedDragging;
    if (!reduceMotion) options |= QMainWindow::AnimatedDocks;
    return options;
}

void MainWindow::buildLanguageMenu() {
    // The data directory is not known to this module -- `mh_ui` has no data
    // path of its own and must not invent one -- so the menu is built from
    // whatever setLanguageChoices() was given. Empty until then, and hidden
    // rather than shown empty.
    d_->languageMenu = menuBar()->addMenu(tr("&Language"));
    registerText(d_->languageMenu, QT_TR_NOOP("&Language"));
    d_->languageMenu->menuAction()->setVisible(false);
}

void MainWindow::setLanguageChoices(const std::filesystem::path& dataDir,
                                    const QStringList& names) {
    d_->languageMenu->clear();
    d_->languageDir = dataDir;

    auto* group = new QActionGroup(d_->languageMenu);
    group->setExclusive(true);

    // "English" is the untranslated source, not a file: every shipped .json is
    // a translation OF it, so it is the way back rather than another choice.
    const auto add = [&](const QString& label, const QString& file) {
        QAction* a = d_->languageMenu->addAction(label);
        a->setCheckable(true);
        a->setChecked(file == d_->language);
        a->setObjectName(QStringLiteral("language.") + (file.isEmpty() ? "english" : file));
        group->addAction(a);
        connect(a, &QAction::triggered, this, [this, file] {
            if (!setLanguage(d_->languageDir, file)) {
                statusBar()->showMessage(tr("Could not load that language"), 3000);
            }
        });
    };
    add(QStringLiteral("English"), QString{});
    for (const QString& n : names)
        add(n, n);

    d_->languageMenu->menuAction()->setVisible(!names.isEmpty());
}

void MainWindow::applyText(QObject* target, const QString& text) {
    // QAction, QMenu and QWidget each spell "the label" differently; there is
    // no common setter to call.
    if (auto* a = qobject_cast<QAction*>(target)) {
        a->setText(text);
    } else if (auto* m = qobject_cast<QMenu*>(target)) {
        m->setTitle(text);
    } else if (auto* w = qobject_cast<QWidget*>(target)) {
        w->setWindowTitle(text);
    }
}

void MainWindow::registerText(QObject* target, const char* source) {
    d_->texts.push_back(Translatable{target, QByteArray(source)});
    applyText(target, QCoreApplication::translate("", source));
}

void MainWindow::retranslateUi() {
    for (const Translatable& t : d_->texts) {
        // QPointer, so a dock closed and destroyed since registration is
        // skipped rather than followed into freed memory.
        if (t.target) applyText(t.target, QCoreApplication::translate("", t.source.constData()));
    }
}

void MainWindow::changeEvent(QEvent* e) {
    // Qt posts this to every top-level widget when a translator is installed or
    // removed -- including by someone other than setLanguage(), which is why
    // the work hangs off the event rather than off the setter.
    if (e != nullptr && e->type() == QEvent::LanguageChange) retranslateUi();
    QMainWindow::changeEvent(e);
}

bool MainWindow::setLanguage(const std::filesystem::path& dataDir, const QString& name) {
    auto next = std::make_unique<JsonTranslator>();
    // Loaded BEFORE the old one is removed: a failed load must leave the
    // running language alone rather than drop the user into raw source strings.
    if (!name.isEmpty() && !next->load(dataDir, name)) return false;

    if (d_->translator) QCoreApplication::removeTranslator(d_->translator.get());
    d_->translator = std::move(next);
    d_->language   = name;

    if (!name.isEmpty()) QCoreApplication::installTranslator(d_->translator.get());
    // Direction is application-wide in Qt and the file is the only thing that
    // knows: `__options__.rtl`, set on Arabic alone of the twenty shipped.
    QApplication::setLayoutDirection(d_->translator->isRightToLeft() ? Qt::RightToLeft
                                                                     : Qt::LeftToRight);
    // installTranslator posts LanguageChange, but removeTranslator on the way
    // to NO language posts it too -- and if neither ran (same empty name twice)
    // nothing would repaint. Cheap, and idempotent.
    retranslateUi();
    return true;
}

QString MainWindow::language() const {
    return d_->language;
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

QStringList MainWindow::setWorkspaceNames(const foundation::NameTable& table,
                                          foundation::NamingProfile profile) {
    QStringList shownNames;
    for (const WorkspacePreset& preset : workspacePresets()) {
        QAction* action = findChild<QAction*>(QStringLiteral("workspace.") + preset.name);
        if (action == nullptr) continue;
        // The preset list is keyed by LEGACY names, so the legacy column is
        // what it is looked up in. Resolving in the ACTIVE profile would
        // happen to work -- `resolve` falls back to the other column, so
        // "Materials" is found under the modern profile too -- and a mutation
        // confirmed it changes nothing. The column is named anyway: relying on
        // the fallback here would make a deliberate lookup read as an
        // accident, and the fallback is there for what a USER typed.
        const auto hit = foundation::resolve(table, foundation::NamingProfile::Legacy,
                                             preset.name.toStdString());
        if (!hit) continue;
        const auto shown = foundation::displayName(table, profile, hit->canonical);
        if (!shown) continue;
        const QString label = QString::fromStdString(std::string(*shown));
        action->setText(label);
        shownNames.push_back(label);
    }
    return shownNames;
}

bool MainWindow::applyWorkspacePreset(const QString& name) {
    const auto& presets = workspacePresets();
    const auto found =
        std::find_if(presets.begin(), presets.end(), [&](const auto& p) { return p.name == name; });
    if (found == presets.end()) return false;

    const QStringList shown = found->categories.value_or(d_->categories);
    QStringList visible;
    for (const QString& category : shown) {
        if (findChild<QDockWidget*>(dockObjectName(category)) != nullptr) {
            visible << dockObjectName(category);
        }
    }
    // A preset that names categories but resolves to no live dock would hide
    // everything and report success -- indistinguishable from Export, and the
    // layout is then saved on quit. Rename a registered category and Cmd+3
    // used to give a blank window with "Workspace: Materials" in the status
    // bar and no way back except Reset Workspace.
    if (!shown.isEmpty() && visible.isEmpty()) return false;

    // Captured before anything moves, so undo has somewhere to go back to.
    // Everything above this line is a REFUSAL path: a preset that resolves to
    // nothing must not leave an undo entry that does nothing, the same rule the
    // pose commands follow.
    const QByteArray before = saveState();

    // From the shipped layout each time, so switching preset to preset does not
    // accumulate whatever the previous one moved.
    restoreState(d_->defaultState);
    for (QDockWidget* dock : findChildren<QDockWidget*>()) {
        dock->setVisible(visible.contains(dock->objectName()));
    }
    // Tabs, when the preset asks for them. After restoreState above, so it
    // stacks the shipped layout rather than whatever the last preset left --
    // and before resizeDocks, because tabifying moves docks between areas and
    // a width set first would apply to the wrong one.
    if (found->tabbed) {
        QDockWidget* anchor = nullptr;
        for (const QString& objectName : visible) {
            auto* dock = findChild<QDockWidget*>(objectName);
            if (dock == nullptr) continue;
            if (anchor == nullptr) {
                anchor = dock;
                continue;
            }
            tabifyDockWidget(anchor, dock);
        }
        // The first category named is the one the preset is about, so it is the
        // tab the user is looking at rather than whichever Qt raised last.
        if (anchor != nullptr) anchor->raise();
    }

    // The first category named is the one the preset is about, so it gets room.
    if (!visible.isEmpty()) {
        if (auto* dock = findChild<QDockWidget*>(visible.front())) {
            resizeDocks({dock}, {380}, Qt::Horizontal);
        }
    }
    // One undo step for the whole layout change. Without this, Cmd+1 followed by
    // Cmd+Z left the new layout in place and undid whatever slider the user had
    // touched before it -- the wrong thing, silently.
    if (const QByteArray after = saveState(); after != before) {
        d_->undo->push(new LayoutChangeCommand(name, before, after,
                                               [this](const QByteArray& st) { restoreState(st); }));
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
