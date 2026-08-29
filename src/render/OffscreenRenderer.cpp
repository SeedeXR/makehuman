// SPDX-License-Identifier: Apache-2.0
#include "makehuman/render/OffscreenRenderer.h"

#include <rhi/qrhi.h>
#include <QFile>

namespace mh::render {

struct OffscreenRenderer::Impl {
    std::unique_ptr<QRhi> rhi;
    std::filesystem::path shaderDir;
};

OffscreenRenderer::OffscreenRenderer() : d_(std::make_unique<Impl>()) {}

OffscreenRenderer::~OffscreenRenderer() = default;

std::string OffscreenRenderer::backendName() const {
    return d_->rhi ? d_->rhi->backendName() : std::string{};
}

std::expected<std::unique_ptr<OffscreenRenderer>, RenderError> OffscreenRenderer::create(
    const std::filesystem::path& shaderDir) {
    auto r = std::unique_ptr<OffscreenRenderer>(new OffscreenRenderer());

    QRhiMetalInitParams params;
    r->d_->rhi.reset(QRhi::create(QRhi::Metal, &params));
    if (!r->d_->rhi) {
        return std::unexpected(RenderError{RenderErrorKind::NoDevice, "QRhi::create(Metal)"});
    }
    r->d_->shaderDir = shaderDir;

    // Fail here rather than at first render if the shaders are missing: a
    // renderer that constructs and then cannot draw is harder to diagnose.
    QFile probe(QString::fromStdString((shaderDir / "litsphere.vert.qsb").string()));
    if (!probe.exists()) {
        return std::unexpected(RenderError{RenderErrorKind::ShaderMissing,
                                           (shaderDir / "litsphere.vert.qsb").string()});
    }
    return r;
}

std::expected<QImage, RenderError> OffscreenRenderer::render(const foundation::RenderView& mesh,
                                                             const RenderSettings& s) {
    if (mesh.vertexCount() == 0 || mesh.indexCount() == 0) {
        return std::unexpected(RenderError{RenderErrorKind::EmptyMesh, {}});
    }
    QRhi* rhi = d_->rhi.get();
    const QSize size(s.width, s.height);

    std::unique_ptr<QRhiTexture> colour(
        rhi->newTexture(QRhiTexture::RGBA8, size, 1, QRhiTexture::RenderTarget));
    if (!colour->create()) {
        return std::unexpected(RenderError{RenderErrorKind::Failed, "colour target"});
    }
    std::unique_ptr<QRhiRenderBuffer> depth(
        rhi->newRenderBuffer(QRhiRenderBuffer::DepthStencil, size, 1));
    if (!depth->create()) {
        return std::unexpected(RenderError{RenderErrorKind::Failed, "depth buffer"});
    }

    QRhiTextureRenderTargetDescription rtDesc;
    rtDesc.setColorAttachments({QRhiColorAttachment(colour.get())});
    rtDesc.setDepthStencilBuffer(depth.get());
    std::unique_ptr<QRhiTextureRenderTarget> rt(rhi->newTextureRenderTarget(rtDesc));
    std::unique_ptr<QRhiRenderPassDescriptor> rp(rt->newCompatibleRenderPassDescriptor());
    rt->setRenderPassDescriptor(rp.get());
    if (!rt->create()) {
        return std::unexpected(RenderError{RenderErrorKind::Failed, "render target"});
    }

    auto scene = SceneResources::create(rhi, rp.get(), d_->shaderDir, 1);
    if (!scene) return std::unexpected(scene.error());

    QRhiCommandBuffer* cb = nullptr;
    if (rhi->beginOffscreenFrame(&cb) != QRhi::FrameOpSuccess) {
        return std::unexpected(RenderError{RenderErrorKind::Failed, "beginOffscreenFrame"});
    }

    QRhiResourceUpdateBatch* u = rhi->nextResourceUpdateBatch();
    if (const auto ok = (*scene)->upload(u, mesh, s.litsphere); !ok) {
        rhi->endOffscreenFrame();
        return std::unexpected(ok.error());
    }
    (*scene)->updateCamera(u, s.camera, static_cast<float>(s.width) / static_cast<float>(s.height));

    cb->beginPass(rt.get(), QColor::fromRgbF(s.background.x, s.background.y, s.background.z, 1.0F),
                  {1.0F, 0}, u);
    (*scene)->draw(cb, size);
    cb->endPass();

    // Read back in the SAME frame; offscreen frames do not present, so a later
    // frame would need the texture to survive on its own.
    QRhiReadbackResult readback;
    QRhiResourceUpdateBatch* rb = rhi->nextResourceUpdateBatch();
    rb->readBackTexture({colour.get()}, &readback);
    cb->resourceUpdate(rb);

    rhi->endOffscreenFrame();

    if (readback.data.isEmpty()) {
        return std::unexpected(RenderError{RenderErrorKind::Failed, "empty readback"});
    }
    const QImage out(reinterpret_cast<const uchar*>(readback.data.constData()),
                     readback.pixelSize.width(), readback.pixelSize.height(),
                     QImage::Format_RGBA8888);
    return out.copy();  // detach before the readback buffer dies
}

}  // namespace mh::render
