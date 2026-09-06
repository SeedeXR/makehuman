// SPDX-License-Identifier: Apache-2.0
#include "makehuman/ui/ViewportWidget.h"

#include "makehuman/render/Picking.h"
#include "makehuman/ui/Theme.h"

#include <rhi/qrhi.h>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QWheelEvent>

#include <algorithm>
#include <cmath>
#include <limits>
#include <optional>

namespace mh::ui {

struct ViewportWidget::Impl {
    std::filesystem::path shaderDir;
    std::filesystem::path litsphere;  ///< the default, used by setMesh
    std::vector<render::MeshInstance> meshes;

    std::unique_ptr<render::SceneResources> scene;
    render::Camera camera;
    /// Held here rather than only in `scene`, which is destroyed and rebuilt
    /// whenever the device or render pass changes.
    render::ShadingModel shading{render::ShadingModel::Litsphere};
    QString error;

    /// What `scene` was built against. initialize() runs on every resize, but
    /// the device and render pass usually have not changed -- rebuilding anyway
    /// costs two shader reads, a PNG decode, a pipeline compile and a 600 KB
    /// buffer upload per resize event, so the pair is remembered and compared.
    QRhi* builtAgainstRhi{};
    QRhiRenderPassDescriptor* builtAgainstPass{};

    /// Set whenever the geometry changes; consumed by the next render().
    bool needsUpload{false};
    QPoint lastMouse;
};

ViewportWidget::ViewportWidget(std::filesystem::path shaderDir, QWidget* parent)
    : QRhiWidget(parent), d_(std::make_unique<Impl>()) {
    d_->shaderDir = std::move(shaderDir);
    setApi(QRhiWidget::Api::Metal);
    // 4x MSAA: the body's silhouette is the most visible edge in the app, and
    // this is the cheapest place to fix it.
    setSampleCount(render::kSampleCount);
}

ViewportWidget::~ViewportWidget() = default;

void ViewportWidget::setMeshes(std::vector<render::MeshInstance> meshes) {
    d_->meshes      = std::move(meshes);
    d_->needsUpload = true;
    update();
}

void ViewportWidget::setMesh(const foundation::RenderView& mesh) {
    d_->meshes      = {render::MeshInstance{mesh, d_->litsphere}};
    d_->needsUpload = true;
    update();
}

void ViewportWidget::setLitsphere(std::filesystem::path path) {
    d_->litsphere = std::move(path);
    // Only the single-mesh case follows this; a list set by setMeshes carries
    // a litsphere per entry and must not be overwritten wholesale.
    if (d_->meshes.size() == 1) d_->meshes[0].litsphere = d_->litsphere;
    d_->needsUpload = true;
    update();
}

void ViewportWidget::setShadingModel(render::ShadingModel model) {
    d_->shading = model;
    // The scene may not exist yet: the widget is constructed long before it is
    // first shown, and initialize() applies the remembered value.
    if (d_->scene) d_->scene->setShadingModel(model);
    update();
}

render::ShadingModel ViewportWidget::shadingModel() const {
    return d_->shading;
}

render::Camera ViewportWidget::camera() const {
    return d_->camera;
}

void ViewportWidget::setCamera(const render::Camera& c) {
    d_->camera = c;
    update();
}

QString ViewportWidget::lastError() const {
    return d_->error;
}

void ViewportWidget::initialize(QRhiCommandBuffer* cb) {
    Q_UNUSED(cb);
    QRhiRenderPassDescriptor* pass = renderTarget()->renderPassDescriptor();
    if (d_->scene && rhi() == d_->builtAgainstRhi && pass == d_->builtAgainstPass) {
        return;  // a plain resize: nothing the resources bind to has changed
    }
    d_->scene.reset();

    // renderTarget()->sampleCount() is what the pipeline must match.
    // QRhiWidget::sampleCount() is only what was *requested*, and the backend
    // may clamp it -- asking for 64 yields a target with 4.
    auto scene =
        render::SceneResources::create(rhi(), pass, d_->shaderDir, renderTarget()->sampleCount());
    if (!scene) {
        d_->error = QString::fromStdString(scene.error().message());
        emit statusChanged(d_->error);
        return;
    }
    d_->scene = std::move(*scene);
    // A fresh SceneResources defaults to the litsphere, so without this a
    // rebuild would quietly throw away the user's shading choice.
    //
    // NOT covered by a test: reaching this line needs a real device and a live
    // render target, which the offscreen UI tests do not have. The widget-side
    // memory below it IS covered; this line is reviewed, not proven.
    d_->scene->setShadingModel(d_->shading);
    d_->builtAgainstRhi  = rhi();
    d_->builtAgainstPass = pass;
    d_->needsUpload      = true;
    d_->error.clear();
    emit statusChanged(QString{});
}

void ViewportWidget::render(QRhiCommandBuffer* cb) {
    if (!d_->scene) return;

    QRhiResourceUpdateBatch* u = rhi()->nextResourceUpdateBatch();

    if (d_->needsUpload && !d_->meshes.empty()) {
        if (const auto ok = d_->scene->upload(u, d_->meshes); !ok) {
            // Left set, so a transient failure is retried on the next frame
            // rather than leaving the viewport empty for the whole session.
            d_->error = QString::fromStdString(ok.error().message());
            emit statusChanged(d_->error);
        } else {
            d_->needsUpload = false;
        }
    }

    const QSize pix    = renderTarget()->pixelSize();
    const float aspect = pix.height() > 0
                             ? static_cast<float>(pix.width()) / static_cast<float>(pix.height())
                             : 1.0F;
    d_->scene->updateCamera(u, d_->camera, aspect);

    cb->beginPass(renderTarget(), theme::palette().bgViewport, {1.0F, 0}, u);
    d_->scene->draw(cb, pix);
    cb->endPass();
}

void ViewportWidget::releaseResources() {
    // Qt does not always call this -- on macOS/Metal it was never observed to
    // fire on hide, reparent or delete. What actually guarantees the resources
    // die before their device is that `d_` is a member of this class, so it is
    // destroyed before the QRhiWidget base subobject that owns the QRhi.
    d_->scene.reset();
    d_->builtAgainstRhi  = nullptr;
    d_->builtAgainstPass = nullptr;
}

void ViewportWidget::mousePressEvent(QMouseEvent* e) {
    d_->lastMouse = e->pos();
}

void ViewportWidget::mouseDoubleClickEvent(QMouseEvent* e) {
    if (e->button() != Qt::LeftButton) return;

    // A CPU ray cast, not the reference's colour-ID pass plus depth readback
    // (`glmodule.py:641`, `camera.py:774`). Once per double-click over 36,972
    // triangles, so there is nothing to save by going near the GPU -- and this
    // works before the RHI exists, which is what makes it testable.
    const render::Ray ray =
        render::rayThroughPixel(d_->camera, e->pos().x(), e->pos().y(), width(), height());

    std::optional<foundation::Vec3> nearest;
    float nearestDistance = std::numeric_limits<float>::max();
    for (const auto& m : d_->meshes) {
        const auto hit = render::intersect(m.mesh, ray);
        if (!hit) continue;
        // Every worn proxy is its own mesh, so "nearest across all of them" is
        // the question -- clicking a sleeve must focus the sleeve, not the arm
        // behind it.
        const foundation::Vec3 delta = *hit - ray.origin;
        const float distance         = foundation::dot(delta, delta);
        if (distance >= nearestDistance) continue;
        nearestDistance = distance;
        nearest         = hit;
    }
    // A miss leaves the camera alone. Focusing on wherever the ray happened to
    // be would throw the model off screen from any corner of the window.
    if (!nearest) return;

    render::focusOn(d_->camera, *nearest);
    update();
}

void ViewportWidget::mouseMoveEvent(QMouseEvent* e) {
    const Qt::MouseButtons held = e->buttons();
    if ((held & (Qt::LeftButton | Qt::MiddleButton)) == 0) return;

    const QPoint delta = e->pos() - d_->lastMouse;
    d_->lastMouse      = e->pos();

    // MIDDLE drag pans, LEFT drags orbits. The reference binds pan to the arrow
    // keys (`core/mhmain.py:178-181`), but those already orbit here -- taking
    // them back would remove a working control to match a convention. Middle
    // drag was free, and is what every DCC uses.
    if ((held & Qt::MiddleButton) != 0) {
        // Scaled by distance so a drag moves the model the same fraction of the
        // screen at every zoom: pan is a world-space offset seen through a
        // perspective projection, so a fixed step crawls when far and leaps
        // when close -- the same reason the wheel is multiplicative.
        const float perPixel = d_->camera.distance * kPanPerPixel;
        d_->camera.panX += static_cast<float>(delta.x()) * perPixel;
        // Screen y grows downward; the camera's does not.
        d_->camera.panY -= static_cast<float>(delta.y()) * perPixel;
        update();
        return;
    }

    d_->camera.yawDegrees += static_cast<float>(delta.x()) * 0.5F;
    d_->camera.pitchDegrees += static_cast<float>(delta.y()) * 0.5F;
    // Clamped so the model cannot roll past vertical, which is disorienting
    // and has no use for a standing figure.
    d_->camera.pitchDegrees =
        std::clamp(d_->camera.pitchDegrees, -kMaxPitchDegrees, kMaxPitchDegrees);
    update();
}

void ViewportWidget::wheelEvent(QWheelEvent* e) {
    const float steps = static_cast<float>(e->angleDelta().y()) / 120.0F;
    // Multiplicative, so a step feels the same at every distance -- linear
    // dolly crawls when far and overshoots when close.
    d_->camera.distance *= std::pow(0.9F, steps);
    d_->camera.distance = std::clamp(d_->camera.distance, kMinDistance, kMaxDistance);
    update();
}

void ViewportWidget::keyPressEvent(QKeyEvent* e) {
    // One arrow press is worth a few degrees -- enough to make progress, small
    // enough to aim. Shift multiplies for coarse movement, matching the
    // convention every DCC uses.
    const float step = (e->modifiers() & Qt::ShiftModifier) != 0 ? 15.0F : 3.0F;
    render::Camera c = d_->camera;

    switch (e->key()) {
        case Qt::Key_Left: c.yawDegrees -= step; break;
        case Qt::Key_Right: c.yawDegrees += step; break;
        case Qt::Key_Up: c.pitchDegrees -= step; break;
        case Qt::Key_Down: c.pitchDegrees += step; break;
        case Qt::Key_Plus:
        case Qt::Key_Equal: c.distance *= 0.9F; break;
        case Qt::Key_Minus: c.distance /= 0.9F; break;
        case Qt::Key_Home: c = render::Camera{}; break;
        default: QRhiWidget::keyPressEvent(e); return;
    }

    // The same clamps the mouse obeys, in one place.
    c.pitchDegrees = std::clamp(c.pitchDegrees, -kMaxPitchDegrees, kMaxPitchDegrees);
    c.distance     = std::clamp(c.distance, kMinDistance, kMaxDistance);
    d_->camera     = c;
    update();
    e->accept();
}

}  // namespace mh::ui
