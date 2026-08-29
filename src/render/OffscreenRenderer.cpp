// SPDX-License-Identifier: Apache-2.0
#include "makehuman/render/OffscreenRenderer.h"

#include <rhi/qrhi.h>
#include <QFile>
#include <QMatrix4x4>

#include <cmath>
#include <vector>

namespace mh::render {
namespace {

using foundation::Vec3;

std::unique_ptr<QRhiShaderStage> loadStage(const std::filesystem::path& p,
                                           QRhiShaderStage::Type type, std::string& err) {
    QFile f(QString::fromStdString(p.string()));
    if (!f.open(QIODevice::ReadOnly)) {
        err = "cannot open " + p.string();
        return nullptr;
    }
    const QShader s = QShader::fromSerialized(f.readAll());
    if (!s.isValid()) {
        err = "not a valid .qsb: " + p.string();
        return nullptr;
    }
    return std::make_unique<QRhiShaderStage>(type, s);
}

}  // namespace

std::string RenderError::message() const {
    const char* k = "unknown error";
    switch (kind) {
        case RenderErrorKind::NoDevice: k = "no RHI device"; break;
        case RenderErrorKind::ShaderMissing: k = "shader missing"; break;
        case RenderErrorKind::TextureMissing: k = "texture missing"; break;
        case RenderErrorKind::EmptyMesh: k = "mesh has no geometry"; break;
        case RenderErrorKind::Failed: k = "render failed"; break;
    }
    std::string m = k;
    if (!detail.empty()) m += " (" + detail + ")";
    return m;
}

struct OffscreenRenderer::Impl {
    std::unique_ptr<QRhi> rhi;
    QShader vert;
    QShader frag;
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

    std::string err;
    const auto vs = loadStage(shaderDir / "litsphere.vert.qsb", QRhiShaderStage::Vertex, err);
    if (!vs) return std::unexpected(RenderError{RenderErrorKind::ShaderMissing, err});
    const auto fs = loadStage(shaderDir / "litsphere.frag.qsb", QRhiShaderStage::Fragment, err);
    if (!fs) return std::unexpected(RenderError{RenderErrorKind::ShaderMissing, err});

    r->d_->vert = vs->shader();
    r->d_->frag = fs->shader();
    return r;
}

std::expected<QImage, RenderError> OffscreenRenderer::render(const foundation::RenderView& mesh,
                                                             const RenderSettings& s) {
    if (mesh.vertexCount() == 0 || mesh.indexCount() == 0) {
        return std::unexpected(RenderError{RenderErrorKind::EmptyMesh, {}});
    }
    QRhi* rhi = d_->rhi.get();

    QImage lit(QString::fromStdString(s.litsphere.string()));
    if (lit.isNull()) {
        return std::unexpected(RenderError{RenderErrorKind::TextureMissing, s.litsphere.string()});
    }
    lit = lit.convertToFormat(QImage::Format_RGBA8888);

    const QSize size(s.width, s.height);

    // ---- resources --------------------------------------------------------
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

    // Interleaved position/normal/uv, because that is what the vertex layout
    // below declares and a single buffer is one binding instead of three.
    std::vector<float> verts;
    verts.reserve(mesh.vertexCount() * 8);
    const bool hasN = mesh.vnorm.size() == mesh.vertexCount();
    const bool hasT = mesh.texco.size() == mesh.vertexCount();
    for (size_t i = 0; i < mesh.vertexCount(); ++i) {
        verts.push_back(mesh.coord[i].x);
        verts.push_back(mesh.coord[i].y);
        verts.push_back(mesh.coord[i].z);
        verts.push_back(hasN ? mesh.vnorm[i].x : 0.0F);
        verts.push_back(hasN ? mesh.vnorm[i].y : 0.0F);
        verts.push_back(hasN ? mesh.vnorm[i].z : 1.0F);
        verts.push_back(hasT ? mesh.texco[i].x : 0.0F);
        verts.push_back(hasT ? mesh.texco[i].y : 0.0F);
    }

    std::unique_ptr<QRhiBuffer> vbuf(
        rhi->newBuffer(QRhiBuffer::Immutable, QRhiBuffer::VertexBuffer,
                       static_cast<quint32>(verts.size() * sizeof(float))));
    if (!vbuf->create()) return std::unexpected(RenderError{RenderErrorKind::Failed, "vbuf"});

    std::unique_ptr<QRhiBuffer> ibuf(
        rhi->newBuffer(QRhiBuffer::Immutable, QRhiBuffer::IndexBuffer,
                       static_cast<quint32>(mesh.indexCount() * sizeof(uint32_t))));
    if (!ibuf->create()) return std::unexpected(RenderError{RenderErrorKind::Failed, "ibuf"});

    // Three mat4 plus a vec4, matching the `Buf` block in litsphere.vert.
    constexpr quint32 kUboSize = 64 * 3 + 16;
    std::unique_ptr<QRhiBuffer> ubuf(
        rhi->newBuffer(QRhiBuffer::Dynamic, QRhiBuffer::UniformBuffer, kUboSize));
    if (!ubuf->create()) return std::unexpected(RenderError{RenderErrorKind::Failed, "ubuf"});

    std::unique_ptr<QRhiTexture> litTex(rhi->newTexture(QRhiTexture::RGBA8, lit.size()));
    if (!litTex->create()) return std::unexpected(RenderError{RenderErrorKind::Failed, "litTex"});

    // A 1x1 white stand-in for the diffuse map, so the shader's
    // `shading * diffuse` term is a no-op when no diffuse texture is supplied.
    //
    // Binding the litsphere to BOTH slots -- which is what this did first --
    // makes "diffuse" the matcap sampled by the MESH's UVs, which is
    // meaningless and paints arbitrary dark patches wherever those UVs happen
    // to land on a dark part of the sphere. It looked like a normals bug.
    QImage whitePixel(1, 1, QImage::Format_RGBA8888);
    whitePixel.fill(Qt::white);
    std::unique_ptr<QRhiTexture> diffuseTex(rhi->newTexture(QRhiTexture::RGBA8, QSize(1, 1)));
    if (!diffuseTex->create()) {
        return std::unexpected(RenderError{RenderErrorKind::Failed, "diffuseTex"});
    }

    // The litsphere is sampled by the view-space normal, which reaches the
    // edges of the sphere, so clamping matters -- repeat would wrap the rim.
    std::unique_ptr<QRhiSampler> sampler(
        rhi->newSampler(QRhiSampler::Linear, QRhiSampler::Linear, QRhiSampler::None,
                        QRhiSampler::ClampToEdge, QRhiSampler::ClampToEdge));
    if (!sampler->create()) return std::unexpected(RenderError{RenderErrorKind::Failed, "sampler"});

    std::unique_ptr<QRhiShaderResourceBindings> srb(rhi->newShaderResourceBindings());
    srb->setBindings({
        QRhiShaderResourceBinding::uniformBuffer(
            0, QRhiShaderResourceBinding::VertexStage | QRhiShaderResourceBinding::FragmentStage,
            ubuf.get()),
        QRhiShaderResourceBinding::sampledTexture(1, QRhiShaderResourceBinding::FragmentStage,
                                                  litTex.get(), sampler.get()),
        QRhiShaderResourceBinding::sampledTexture(2, QRhiShaderResourceBinding::FragmentStage,
                                                  diffuseTex.get(), sampler.get()),
    });
    if (!srb->create()) return std::unexpected(RenderError{RenderErrorKind::Failed, "srb"});

    std::unique_ptr<QRhiGraphicsPipeline> ps(rhi->newGraphicsPipeline());
    ps->setShaderStages(
        {{QRhiShaderStage::Vertex, d_->vert}, {QRhiShaderStage::Fragment, d_->frag}});
    QRhiVertexInputLayout layout;
    layout.setBindings({{8 * sizeof(float)}});
    layout.setAttributes({
        {0, 0, QRhiVertexInputAttribute::Float3, 0},
        {0, 1, QRhiVertexInputAttribute::Float3, 3 * sizeof(float)},
        {0, 2, QRhiVertexInputAttribute::Float2, 6 * sizeof(float)},
    });
    ps->setVertexInputLayout(layout);
    ps->setShaderResourceBindings(srb.get());
    ps->setRenderPassDescriptor(rp.get());
    ps->setDepthTest(true);
    ps->setDepthWrite(true);
    // Winding IS consistent after fan triangulation -- verified by rendering
    // with culling on and off and getting byte-identical pixel statistics
    // (32,432 covered, luminance 7..116, mean 64.5). Culling stays on because
    // it halves the fragment work for free.
    ps->setCullMode(QRhiGraphicsPipeline::Back);
    if (!ps->create()) return std::unexpected(RenderError{RenderErrorKind::Failed, "pipeline"});

    // ---- matrices ---------------------------------------------------------
    QMatrix4x4 proj = rhi->clipSpaceCorrMatrix();
    proj.perspective(s.fovY, static_cast<float>(s.width) / static_cast<float>(s.height), 0.1F,
                     1000.0F);

    QMatrix4x4 view;
    view.translate(0.0F, 0.0F, -s.distance);

    // The MODEL rotates and the camera stays put, matching the reference.
    QMatrix4x4 model;
    model.rotate(s.pitchDegrees, 1.0F, 0.0F, 0.0F);
    model.rotate(s.yawDegrees, 0.0F, 1.0F, 0.0F);

    const QMatrix4x4 modelView = view * model;
    const QMatrix4x4 mvp       = proj * modelView;
    // Inverse-transpose, because a normal is a covector: it does not transform
    // like a position under a non-uniform transform.
    const QMatrix4x4 normalMat = modelView.inverted().transposed();

    // ---- record -----------------------------------------------------------
    QRhiCommandBuffer* cb = nullptr;
    if (rhi->beginOffscreenFrame(&cb) != QRhi::FrameOpSuccess) {
        return std::unexpected(RenderError{RenderErrorKind::Failed, "beginOffscreenFrame"});
    }

    QRhiResourceUpdateBatch* u = rhi->nextResourceUpdateBatch();
    u->uploadStaticBuffer(vbuf.get(), verts.data());
    u->uploadStaticBuffer(ibuf.get(), mesh.index.data());
    u->uploadTexture(litTex.get(), lit);
    u->uploadTexture(diffuseTex.get(), whitePixel);
    u->updateDynamicBuffer(ubuf.get(), 0, 64, mvp.constData());
    u->updateDynamicBuffer(ubuf.get(), 64, 64, modelView.constData());
    u->updateDynamicBuffer(ubuf.get(), 128, 64, normalMat.constData());
    const float params[4] = {0.0F, 1.0F, 0.0F, 0.0F};  // AdditiveShading, normalmapIntensity
    u->updateDynamicBuffer(ubuf.get(), 192, 16, params);

    cb->beginPass(rt.get(), QColor::fromRgbF(s.background.x, s.background.y, s.background.z, 1.0F),
                  {1.0F, 0}, u);
    cb->setGraphicsPipeline(ps.get());
    cb->setViewport({0, 0, static_cast<float>(s.width), static_cast<float>(s.height)});
    cb->setShaderResources();
    const QRhiCommandBuffer::VertexInput vin(vbuf.get(), 0);
    cb->setVertexInput(0, 1, &vin, ibuf.get(), 0, QRhiCommandBuffer::IndexUInt32);
    cb->drawIndexed(static_cast<quint32>(mesh.indexCount()));
    cb->endPass();

    // Read back in the SAME frame; a separate frame would need the texture to
    // survive, and offscreen frames do not present.
    QRhiReadbackResult readback;
    QRhiResourceUpdateBatch* rb = rhi->nextResourceUpdateBatch();
    rb->readBackTexture({colour.get()}, &readback);
    cb->resourceUpdate(rb);

    rhi->endOffscreenFrame();

    if (readback.data.isEmpty()) {
        return std::unexpected(RenderError{RenderErrorKind::Failed, "empty readback"});
    }
    QImage out(reinterpret_cast<const uchar*>(readback.data.constData()),
               readback.pixelSize.width(), readback.pixelSize.height(), QImage::Format_RGBA8888);
    return out.copy();  // detach from the readback buffer before it dies
}

}  // namespace mh::render
