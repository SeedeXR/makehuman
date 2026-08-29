// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "makehuman/foundation/Geometry.h"
#include "makehuman/render/SceneResources.h"

#include <QRhiWidget>

#include <filesystem>
#include <memory>

namespace mh::ui {

/// The interactive 3D view.
///
/// It draws through the same `SceneResources` the offscreen renderer uses, so
/// the window and the testable image cannot drift apart. QRhiWidget supplies
/// its own QRhi and render target, which is exactly why those resources are
/// constructible against a device passed in from outside.
///
/// The mesh is taken as a non-owning `RenderView`: the widget does not own
/// geometry, and the caller must keep it alive. That also keeps this module
/// Apache-2.0 -- it never touches the AGPL core.
class ViewportWidget : public QRhiWidget {
    Q_OBJECT

signals:
    /// Empty when all is well, otherwise the renderer's own message. Emitted so
    /// a failure reaches the user instead of being a black rectangle.
    void statusChanged(const QString& error);

public:
    explicit ViewportWidget(std::filesystem::path shaderDir, QWidget* parent = nullptr);
    ~ViewportWidget() override;

    /// Replaces the geometry. Safe before or after the RHI is initialised;
    /// the upload is deferred to the next frame either way.
    void setMesh(const foundation::RenderView& mesh);
    void setLitsphere(std::filesystem::path path);

    [[nodiscard]] render::Camera camera() const;
    void setCamera(const render::Camera& c);

    /// Empty until a frame has been drawn; holds the reason if setup failed.
    [[nodiscard]] QString lastError() const;

protected:
    void initialize(QRhiCommandBuffer* cb) override;
    void render(QRhiCommandBuffer* cb) override;
    void releaseResources() override;

    // Orbit with the left button, dolly with the wheel. The MODEL rotates and
    // the camera stays put, which is the reference's convention and what makes
    // the litsphere's fixed eye-space lighting read correctly.
    void mousePressEvent(QMouseEvent* e) override;
    void mouseMoveEvent(QMouseEvent* e) override;
    void wheelEvent(QWheelEvent* e) override;

private:
    struct Impl;
    std::unique_ptr<Impl> d_;
};

}  // namespace mh::ui
