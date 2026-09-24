// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "makehuman/foundation/Geometry.h"
#include "makehuman/render/SceneResources.h"

#include "makehuman/ui/Backdrop.h"

#include <QImage>

#include <QRhiWidget>

#include <filesystem>
#include <memory>
#include <vector>

namespace mh::ui {

class MouseBindings;

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

    /// The backdrop was dragged. Carries the new placement.
    ///
    /// The widget does not own the undo stack; the application does. So this
    /// reports WHAT changed and `backdropGestureFinished` reports WHEN the
    /// gesture ended, and the application merges the flood of mouse events
    /// into one undo entry -- the same division `ModifierPanel` already uses
    /// for slider drags.
    void backdropTransformChanged(BackdropTransform transform);

    /// The right button came up, so the drag is one completed act.
    void backdropGestureFinished();

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

    /// Which mouse gesture orbits and which pans, so the application can apply
    /// the user's stored overrides. Mutable on purpose: `MouseBindings::apply`
    /// is how those overrides arrive.
    [[nodiscard]] MouseBindings& mouseBindings();

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

    /// Switches the viewport between the reference matcap and metallic-
    /// roughness PBR.
    ///
    /// Cheap and safe at any time: both pipelines are built when the scene is
    /// created, so this changes which one the next frame binds and nothing
    /// else. The choice SURVIVES a device or render-pass rebuild -- resizing
    /// the window re-creates SceneResources, and a toggle that silently reset
    /// itself on resize is the defect this remembers past.
    void setShadingModel(render::ShadingModel model);
    [[nodiscard]] render::ShadingModel shadingModel() const;

    /// Replaces the PBR lighting rig.
    ///
    /// Remembered on the widget, because the scene is rebuilt on resize and a
    /// fresh `SceneResources` starts from the built-in rig -- the same reason
    /// `setShadingModel` has to remember its value.
    ///
    /// Does nothing visible under `ShadingModel::Litsphere`: a matcap carries
    /// its lighting in the texture. Callers that offer the user a scene chooser
    /// have to say so rather than let a selection appear to do nothing.
    void setLighting(const render::Lighting& lighting);

    /// Draws edges instead of filled faces. Remembered until the scene exists,
    /// like the shading model.
    void setWireframe(bool on);
    [[nodiscard]] bool wireframe() const;

    /// Whether the device can draw them. **False until the first frame**: the
    /// answer comes from a pipeline that is built with the scene, and the scene
    /// is built on first paint. Ask after showing the window.
    [[nodiscard]] bool wireframeSupported() const;

    /// A reference photograph behind the model, bound to an axis view.
    ///
    /// Bound to a SIDE because that is what the capability is for: you model to
    /// an orthographic reference photo, and a front photo hanging behind a
    /// three-quarter view is worse than no photo at all. The reference does the
    /// same (`plugins/0_modeling_background.py`), and this port already has the
    /// same six views on the View menu.
    ///
    /// A null @p image removes it.
    /// @param transform where the image sits within the frame. Travels WITH the
    ///        backdrop rather than being set on its own, because the two are
    ///        meaningless apart -- a pan belongs to the image the user dragged,
    ///        and rebinding a side without its framing would show the new photo
    ///        through the old window. Defaults to identity, which is what keeps
    ///        an undragged backdrop rendering exactly as it always has.
    ///
    ///        The widget holds ONE backdrop, so it holds ONE transform. The six
    ///        REMEMBERED placements live with the document, which is what the
    ///        `.mhm`'s six `background` lines are; a six-slot table here would
    ///        be five-sixths unused.
    void setBackdrop(const QImage& image, BackdropSide side, float opacity,
                     BackdropTransform transform = {});

    [[nodiscard]] BackdropTransform backdropTransform() const;

    /// Moves the backdrop without re-handing the image.
    ///
    /// Undo's entry point, and the reason it takes no `QImage`: re-uploading
    /// the photograph to the scene in order to change where it sits would make
    /// every undo step a texture upload.
    void setBackdropTransform(BackdropTransform transform);

    /// Draws the ground grid and the backplane behind the figure.
    void setGrid(bool on);
    [[nodiscard]] bool grid() const;

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
    void mouseReleaseEvent(QMouseEvent* e) override;
    /// Double-click focuses: the view recentres on the point under the cursor,
    /// without zooming. Matches what the reference does with a pick
    /// (`camera.py:774 mousePickHumanCenter`); a miss is a no-op.
    void mouseDoubleClickEvent(QMouseEvent* e) override;
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
