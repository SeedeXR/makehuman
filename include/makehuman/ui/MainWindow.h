// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "makehuman/foundation/Geometry.h"

#include <QMainWindow>

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

    [[nodiscard]] ViewportWidget* viewport() const;

    /// Restores docks and geometry from QSettings, or lays out the defaults.
    void restoreWorkspace();
    void saveWorkspace() const;
    /// Back to the shipped layout, discarding the saved one.
    void resetWorkspace();

private:
    struct Impl;
    std::unique_ptr<Impl> d_;
};

}  // namespace mh::ui
