// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "makehuman/foundation/Geometry.h"
#include "makehuman/render/SceneResources.h"
#include "makehuman/ui/MacroStatus.h"
#include "makehuman/ui/TaskRegistry.h"

#include <QMainWindow>
#include <QStringList>

#include <filesystem>
#include <memory>
#include <vector>

class QUndoStack;

namespace mh::ui {

class ViewportWidget;

/// The application shell: a viewport with dockable panels around it.
///
/// Takes geometry as a non-owning view, so this module never touches the AGPL
/// core and stays Apache-2.0. The caller owns the mesh and keeps it alive.
class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    /// @param tasks which docks to create, in registration order. Passing an
    ///        empty registry gives a window with a viewport and no panels;
    ///        required rather than defaulted, so a dockless window is always a
    ///        choice someone typed.
    explicit MainWindow(std::filesystem::path shaderDir, TaskRegistry tasks,
                        QWidget* parent = nullptr);
    ~MainWindow() override;

    void setMesh(const foundation::RenderView& mesh);

    /// Switches the interface language and the layout direction.
    ///
    /// An empty @p name removes any translation and returns to the source
    /// strings. A file that fails to load leaves the current language in place
    /// and returns false, rather than dropping the user into raw English.
    [[nodiscard]] bool setLanguage(const std::filesystem::path& dataDir, const QString& name);

    /// The language set by setLanguage, or empty for the untranslated source.
    [[nodiscard]] QString language() const;

    /// Fills the Language menu, which stays hidden until this is called.
    ///
    /// `mh_ui` has no data path of its own and must not invent one, so the
    /// directory and the list both come from the application. The menu always
    /// offers "English" first -- the untranslated source every shipped file is
    /// a translation of, and the way back from a language you cannot read.
    void setLanguageChoices(const std::filesystem::path& dataDir, const QStringList& names);

    /// Re-applies every registered label from the installed translator. Called
    /// for you on `QEvent::LanguageChange`; public so a test can force it.
    void retranslateUi();

    /// The body plus everything worn, each with its own material. See
    /// ViewportWidget::setMeshes for the lifetime rule: the views are
    /// non-owning and the caller keeps the geometry alive.
    void setMeshes(std::vector<render::MeshInstance> meshes);
    void setLitsphere(std::filesystem::path path);

    /// Puts @p widget in the dock for @p category, replacing the placeholder.
    ///
    /// @return false if no dock has that category, in which case ownership does
    ///         **not** pass and @p widget is untouched. Checked rather than
    ///         void because the category is a free-form string: a typo used to
    ///         delete the panel and leave the caller holding a dangling pointer.
    [[nodiscard]] bool setPanel(const QString& category, QWidget* widget);

    /// Object name of the dock for @p category, e.g. "dock.modelling".
    [[nodiscard]] static QString dockObjectName(const QString& category);

    [[nodiscard]] ViewportWidget* viewport() const;

    /// The docking flags this window uses, given the reduce-motion setting.
    ///
    /// A free choice rather than a member so both branches are testable: the
    /// system setting cannot be changed from a test, and asserting the window
    /// agrees with `theme::reduceMotion()` is vacuous when both sides call the
    /// same function.
    [[nodiscard]] static QMainWindow::DockOptions dockOptionsFor(bool reduceMotion);

    /// The window's undo stack, already wired to Edit > Undo/Redo.
    ///
    /// A command's apply callback must not push further commands: it runs
    /// during undo(), and pushing from there would record the undo itself.
    [[nodiscard]] QUndoStack* undoStack() const;

    /// Shown in the title bar after a load or save.
    void setDocumentPath(const QString& path);

    /// The reference's persistent macro line, on the right of the status bar.
    ///
    /// A permanent widget rather than `showMessage`, which is transient: every
    /// "Saved workspace" or viewport error would wipe the stats and they would
    /// never come back. The reference calls its equivalent `statusPersist` for
    /// the same reason.
    void setMacroStatus(const QString& line);
    [[nodiscard]] QString macroStatus() const;

    /// The user's length units, restored from QSettings at construction so the
    /// choice survives a restart. Metric until they say otherwise.
    [[nodiscard]] Units units() const;

    /// Whether the status line shows a mass or the slider percentage.
    [[nodiscard]] Weight weightMode() const;

    /// Restores docks and geometry from QSettings, or lays out the defaults.
    void restoreWorkspace();
    void saveWorkspace() const;
    /// Back to the shipped layout, discarding the saved one.
    void resetWorkspace();

    /// Applies a shipped preset by name (`design.md` §6.4).
    /// @return false if no preset has that name.
    bool applyWorkspacePreset(const QString& name);

    /// Writes the current layout to the user's workspace directory.
    [[nodiscard]] bool saveWorkspaceAs(const QString& name) const;

    /// Restores a named workspace written by saveWorkspaceAs.
    /// @return false if it is missing, unreadable, or a schema this build does
    ///         not understand.
    bool loadNamedWorkspace(const QString& name);

    /// Names of the workspaces on disk, sorted.
    [[nodiscard]] QStringList namedWorkspaces() const;

    /// Where named workspaces live. Created on demand.
    [[nodiscard]] static QString workspaceDirectory();

signals:
    /// The File menu, as intent rather than action: this module must not decide
    /// what a `.mhm` is, so it says what the user asked for and the app -- which
    /// owns the character -- does it.
    void openRequested();
    void saveRequested();
    void saveAsRequested();
    /// File > Export. The writers have existed since M7 and nothing in the
    /// window reached them; this is what makes them reachable. Intent only --
    /// `mh_ui` is Apache-2.0 and knows nothing about glTF, USD or FBX.
    void exportRequested();

    /// Settings > Units changed. Carries the new value so the app can reformat
    /// the status line without asking back.
    void unitsChanged(Units units);

    /// Edit > Randomise. Belongs with Undo rather than in File: it edits the
    /// character rather than producing a file, and it is the one command in the
    /// application that changes hundreds of values at once.
    void randomiseRequested();

    /// File > Render. Like Export, this is the capability that existed with no
    /// way to reach it: `OffscreenRenderer` has been in the tree since M6 and
    /// `--render` has worked for as long, and the window reached neither.
    void renderRequested();

    /// The toolbar's "grab screen". Same reasoning as the three above: this
    /// module can render a frame but must not decide where a PNG goes.
    void screenshotRequested();

protected:
    void changeEvent(QEvent* e) override;

private:
    /// Registers @p source against @p target and applies it now. `target` may
    /// be a QAction, a QMenu or any QWidget; see applyText.
    void buildLanguageMenu();
    void registerText(QObject* target, const char* source);
    static void applyText(QObject* target, const QString& text);

    struct Impl;
    std::unique_ptr<Impl> d_;
};

}  // namespace mh::ui
