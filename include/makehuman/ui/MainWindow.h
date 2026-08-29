// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "makehuman/foundation/Geometry.h"

#include <QMainWindow>
#include <QStringList>

#include <filesystem>
#include <memory>

namespace mh::ui {

class ViewportWidget;

/// The application shell: a viewport with dockable panels around it.
///
/// Takes geometry as a non-owning view, so this module never touches the AGPL
/// core and stays Apache-2.0. The caller owns the mesh and keeps it alive.
class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(std::filesystem::path shaderDir, QWidget* parent = nullptr);
    ~MainWindow() override;

    void setMesh(const foundation::RenderView& mesh);
    void setLitsphere(std::filesystem::path path);

    /// Puts @p widget in the Modelling dock, replacing the placeholder.
    /// Ownership passes to this window in every case, including failure.
    void setModellingWidget(QWidget* widget);

    /// The same, for the Materials dock.
    void setMaterialsWidget(QWidget* widget);

    [[nodiscard]] ViewportWidget* viewport() const;

    /// Shown in the title bar after a load or save.
    void setDocumentPath(const QString& path);

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

private:
    struct Impl;
    std::unique_ptr<Impl> d_;
};

}  // namespace mh::ui
