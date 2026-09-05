// SPDX-License-Identifier: Apache-2.0
#include "makehuman/render/OffscreenRenderer.h"

#include <rhi/qrhi.h>
#include <QFile>

#include <array>

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
    const std::array<MeshInstance, 1> one{MeshInstance{mesh, s.litsphere}};
    return render(one, s);
}

std::expected<QImage, RenderError> OffscreenRenderer::render(std::span<const MeshInstance> meshes,
                                                             const RenderSettings& s) {
    QRhi* rhi = d_->rhi.get();
    const QSize size(s.width, s.height);

    // Asked for, not assumed: a backend that does not offer 4x samples would
    // otherwise fail to create the render buffer and the whole render would
    // fail rather than fall back to the aliased image it used to produce.
    const int samples = rhi->supportedSampleCounts().contains(kSampleCount) ? kSampleCount : 1;

    // The texture that is read back is always single-sample -- readBackTexture
    // cannot resolve, and a multisample texture is not readable. With MSAA on,
    // drawing goes to a multisample colour buffer and the render pass resolves
    // into this one at endPass.
    std::unique_ptr<QRhiTexture> colour(
        rhi->newTexture(QRhiTexture::RGBA8, size, 1, QRhiTexture::RenderTarget));
    if (!colour->create()) {
        return std::unexpected(RenderError{RenderErrorKind::Failed, "colour target"});
    }
    std::unique_ptr<QRhiRenderBuffer> msaa;
    QRhiColorAttachment attachment;
    if (samples > 1) {
        msaa.reset(rhi->newRenderBuffer(QRhiRenderBuffer::Color, size, samples));
        if (!msaa->create()) {
            return std::unexpected(RenderError{RenderErrorKind::Failed, "msaa colour buffer"});
        }
        attachment.setRenderBuffer(msaa.get());
        attachment.setResolveTexture(colour.get());
    } else {
        attachment.setTexture(colour.get());
    }

    // Depth must carry the SAME sample count as colour or the target is
    // incomplete.
    std::unique_ptr<QRhiRenderBuffer> depth(
        rhi->newRenderBuffer(QRhiRenderBuffer::DepthStencil, size, samples));
    if (!depth->create()) {
        return std::unexpected(RenderError{RenderErrorKind::Failed, "depth buffer"});
    }

    QRhiTextureRenderTargetDescription rtDesc;
    rtDesc.setColorAttachments({attachment});
    rtDesc.setDepthStencilBuffer(depth.get());
    std::unique_ptr<QRhiTextureRenderTarget> rt(rhi->newTextureRenderTarget(rtDesc));
    std::unique_ptr<QRhiRenderPassDescriptor> rp(rt->newCompatibleRenderPassDescriptor());
    rt->setRenderPassDescriptor(rp.get());
    if (!rt->create()) {
        return std::unexpected(RenderError{RenderErrorKind::Failed, "render target"});
    }

    auto scene = SceneResources::create(rhi, rp.get(), d_->shaderDir, samples);
    if (!scene) return std::unexpected(scene.error());
    (*scene)->setShadingModel(s.shading);

    QRhiCommandBuffer* cb = nullptr;
    if (rhi->beginOffscreenFrame(&cb) != QRhi::FrameOpSuccess) {
        return std::unexpected(RenderError{RenderErrorKind::Failed, "beginOffscreenFrame"});
    }

    QRhiResourceUpdateBatch* u = rhi->nextResourceUpdateBatch();
    if (const auto ok = (*scene)->upload(u, meshes); !ok) {
        // An unsubmitted batch stays checked out of a pool of 64; leaking them
        // eventually makes nextResourceUpdateBatch() return null.
        u->release();
        rhi->endOffscreenFrame();
        return std::unexpected(ok.error());
    }
    (*scene)->updateCamera(u, s.camera, static_cast<float>(s.width) / static_cast<float>(s.height));

    cb->beginPass(rt.get(),
                  QColor::fromRgbF(s.background.x, s.background.y, s.background.z,
                                   s.transparentBackground ? 0.0F : 1.0F),
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
