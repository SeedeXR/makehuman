// SPDX-License-Identifier: Apache-2.0
#include "makehuman/ui/ViewportWidget.h"

#include "makehuman/ui/Theme.h"

#include <rhi/qrhi.h>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QWheelEvent>

#include <algorithm>
#include <cmath>

namespace mh::ui {

struct ViewportWidget::Impl {
    std::filesystem::path shaderDir;
    std::filesystem::path litsphere;
    foundation::RenderView mesh;

    std::unique_ptr<render::SceneResources> scene;
    render::Camera camera;
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
    setSampleCount(4);
}

ViewportWidget::~ViewportWidget() = default;

void ViewportWidget::setMesh(const foundation::RenderView& mesh) {
    d_->mesh        = mesh;
    d_->needsUpload = true;
    update();
}

void ViewportWidget::setLitsphere(std::filesystem::path path) {
    d_->litsphere   = std::move(path);
    d_->needsUpload = true;
    update();
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
    d_->scene            = std::move(*scene);
    d_->builtAgainstRhi  = rhi();
    d_->builtAgainstPass = pass;
    d_->needsUpload      = true;
    d_->error.clear();
    emit statusChanged(QString{});
}

void ViewportWidget::render(QRhiCommandBuffer* cb) {
    if (!d_->scene) return;

    QRhiResourceUpdateBatch* u = rhi()->nextResourceUpdateBatch();

    if (d_->needsUpload && d_->mesh.vertexCount() > 0) {
        if (const auto ok = d_->scene->upload(u, d_->mesh, d_->litsphere); !ok) {
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

void ViewportWidget::mouseMoveEvent(QMouseEvent* e) {
    if ((e->buttons() & Qt::LeftButton) == 0) return;

    const QPoint delta = e->pos() - d_->lastMouse;
    d_->lastMouse      = e->pos();

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
