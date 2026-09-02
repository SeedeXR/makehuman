// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "makehuman/foundation/Geometry.h"
#include "makehuman/render/SceneResources.h"

#include <QRhiWidget>

#include <filesystem>
#include <memory>
#include <vector>

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
    /// Navigation limits, public because a camera restored from a file has to
    /// respect the same ones the mouse does. MakeHuman's `maxZoomFactor` of 15
    /// maps to a distance of 3, which is inside the head.
    static constexpr float kMinDistance = 5.0F;
    static constexpr float kMaxDistance = 300.0F;
    /// Beyond this the model rolls past vertical, which is disorienting and has
    /// no use for a standing figure.
    static constexpr float kMaxPitchDegrees = 89.0F;
    /// Pan per pixel, per unit of camera distance. Scaled by distance so a drag
    /// covers the same fraction of the screen at every zoom; the constant is
    /// roughly `tan(fovY/2) / viewportHeight` for the default 30 degree field.
    static constexpr float kPanPerPixel = 0.002F;

    explicit ViewportWidget(std::filesystem::path shaderDir, QWidget* parent = nullptr);
    ~ViewportWidget() override;

    /// Replaces the geometry with a single mesh shaded by the current
    /// litsphere. Safe before or after the RHI is initialised; the upload is
    /// deferred to the next frame either way.
    void setMesh(const foundation::RenderView& mesh);

    /// Replaces the geometry with several meshes, each with its own litsphere.
    /// This is what a dressed character needs: body plus every worn proxy.
    ///
    /// Taken by value, but each `MeshInstance` holds a non-owning `RenderView`:
    /// the caller still owns the geometry and must keep it alive for as long as
    /// the widget is drawing, not merely for the duration of this call.
    void setMeshes(std::vector<render::MeshInstance> meshes);

    /// The litsphere for `setMesh`, and the default a later `setMesh` adopts.
    ///
    /// It rewrites the entry of a **one-mesh** list, whether that list came from
    /// `setMesh` or from `setMeshes`; against a longer list it does nothing but
    /// change the default, because there is no way to know which mesh it meant.
    /// A multi-mesh caller sets materials through `setMeshes`.
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
    /// Arrows orbit, +/- dolly, Home resets. Without this the viewport takes
    /// focus and does nothing with it, and orbiting is mouse-only -- which
    /// `design.md` §9 ("every action reachable without a mouse") forbids.
    void keyPressEvent(QKeyEvent* e) override;

private:
    struct Impl;
    std::unique_ptr<Impl> d_;
};

}  // namespace mh::ui
