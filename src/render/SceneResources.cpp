// SPDX-License-Identifier: Apache-2.0
#include "makehuman/render/SceneResources.h"

#include <rhi/qrhi.h>
#include <QFile>

#include <vector>

namespace mh::render {
namespace {

/// The binding list, in one place. It has to be identical where the pipeline is
/// built and where the litsphere texture is replaced, or the two disagree about
/// which texture is at which slot.
void bindAll(QRhiShaderResourceBindings* srb, QRhiBuffer* ubuf, QRhiTexture* lit,
             QRhiTexture* diffuse, QRhiSampler* sampler) {
    srb->setBindings({
        QRhiShaderResourceBinding::uniformBuffer(
            0, QRhiShaderResourceBinding::VertexStage | QRhiShaderResourceBinding::FragmentStage,
            ubuf),
        QRhiShaderResourceBinding::sampledTexture(1, QRhiShaderResourceBinding::FragmentStage, lit,
                                                  sampler),
        QRhiShaderResourceBinding::sampledTexture(2, QRhiShaderResourceBinding::FragmentStage,
                                                  diffuse, sampler),
    });
}

/// Three mat4 plus a vec4, matching the `Buf` block in litsphere.vert.
constexpr quint32 kUboSize = 64 * 3 + 16;
/// Interleaved position (3) + normal (3) + uv (2).
constexpr quint32 kStride = 8 * sizeof(float);

std::expected<QShader, RenderError> loadShader(const std::filesystem::path& p) {
    QFile f(QString::fromStdString(p.string()));
    if (!f.open(QIODevice::ReadOnly)) {
        return std::unexpected(RenderError{RenderErrorKind::ShaderMissing, p.string()});
    }
    const QShader s = QShader::fromSerialized(f.readAll());
    if (!s.isValid()) {
        return std::unexpected(
            RenderError{RenderErrorKind::ShaderMissing, "not a valid .qsb: " + p.string()});
    }
    return s;
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

struct SceneResources::Impl {
    QRhi* rhi{};
    std::unique_ptr<QRhiBuffer> vbuf;
    std::unique_ptr<QRhiBuffer> ibuf;
    std::unique_ptr<QRhiBuffer> ubuf;
    std::unique_ptr<QRhiTexture> litTex;
    std::unique_ptr<QRhiTexture> diffuseTex;
    std::unique_ptr<QRhiSampler> sampler;
    std::unique_ptr<QRhiShaderResourceBindings> srb;
    std::unique_ptr<QRhiGraphicsPipeline> pipeline;
    quint32 indexCount{};
};

SceneResources::SceneResources() : d_(std::make_unique<Impl>()) {}

SceneResources::~SceneResources() = default;

std::expected<std::unique_ptr<SceneResources>, RenderError> SceneResources::create(
    QRhi* rhi, QRhiRenderPassDescriptor* rp, const std::filesystem::path& shaderDir,
    int sampleCount) {
    if (rhi == nullptr || rp == nullptr) {
        return std::unexpected(RenderError{RenderErrorKind::NoDevice, "null rhi or render pass"});
    }

    auto vs = loadShader(shaderDir / "litsphere.vert.qsb");
    if (!vs) return std::unexpected(vs.error());
    auto fs = loadShader(shaderDir / "litsphere.frag.qsb");
    if (!fs) return std::unexpected(fs.error());

    auto r     = std::unique_ptr<SceneResources>(new SceneResources());
    r->d_->rhi = rhi;

    r->d_->ubuf.reset(rhi->newBuffer(QRhiBuffer::Dynamic, QRhiBuffer::UniformBuffer, kUboSize));
    if (!r->d_->ubuf->create()) {
        return std::unexpected(RenderError{RenderErrorKind::Failed, "uniform buffer"});
    }

    // Sized at upload time; created here so the bindings can reference them.
    r->d_->litTex.reset(rhi->newTexture(QRhiTexture::RGBA8, QSize(1, 1)));
    r->d_->diffuseTex.reset(rhi->newTexture(QRhiTexture::RGBA8, QSize(1, 1)));
    if (!r->d_->litTex->create() || !r->d_->diffuseTex->create()) {
        return std::unexpected(RenderError{RenderErrorKind::Failed, "textures"});
    }

    // The litsphere is sampled by the view-space normal, which reaches the
    // sphere's rim, so clamping matters -- repeat would wrap it.
    r->d_->sampler.reset(rhi->newSampler(QRhiSampler::Linear, QRhiSampler::Linear,
                                         QRhiSampler::None, QRhiSampler::ClampToEdge,
                                         QRhiSampler::ClampToEdge));
    if (!r->d_->sampler->create()) {
        return std::unexpected(RenderError{RenderErrorKind::Failed, "sampler"});
    }

    r->d_->srb.reset(rhi->newShaderResourceBindings());
    bindAll(r->d_->srb.get(), r->d_->ubuf.get(), r->d_->litTex.get(), r->d_->diffuseTex.get(),
            r->d_->sampler.get());
    if (!r->d_->srb->create()) {
        return std::unexpected(RenderError{RenderErrorKind::Failed, "shader resource bindings"});
    }

    r->d_->pipeline.reset(rhi->newGraphicsPipeline());
    r->d_->pipeline->setShaderStages(
        {{QRhiShaderStage::Vertex, *vs}, {QRhiShaderStage::Fragment, *fs}});

    QRhiVertexInputLayout layout;
    layout.setBindings({{kStride}});
    layout.setAttributes({
        {0, 0, QRhiVertexInputAttribute::Float3, 0},
        {0, 1, QRhiVertexInputAttribute::Float3, 3 * sizeof(float)},
        {0, 2, QRhiVertexInputAttribute::Float2, 6 * sizeof(float)},
    });
    r->d_->pipeline->setVertexInputLayout(layout);
    r->d_->pipeline->setShaderResourceBindings(r->d_->srb.get());
    r->d_->pipeline->setRenderPassDescriptor(rp);
    r->d_->pipeline->setDepthTest(true);
    r->d_->pipeline->setDepthWrite(true);
    // Winding IS consistent after fan triangulation -- verified by rendering
    // with culling on and off and getting byte-identical pixel statistics.
    r->d_->pipeline->setCullMode(QRhiGraphicsPipeline::Back);
    // Must match the target, or pipeline creation fails.
    r->d_->pipeline->setSampleCount(sampleCount);
    if (!r->d_->pipeline->create()) {
        return std::unexpected(RenderError{RenderErrorKind::Failed, "graphics pipeline"});
    }

    return r;
}

std::expected<void, RenderError> SceneResources::upload(QRhiResourceUpdateBatch* batch,
                                                        const foundation::RenderView& mesh,
                                                        const std::filesystem::path& litsphere) {
    if (mesh.vertexCount() == 0 || mesh.indexCount() == 0) {
        return std::unexpected(RenderError{RenderErrorKind::EmptyMesh, {}});
    }

    QImage lit(QString::fromStdString(litsphere.string()));
    if (lit.isNull()) {
        return std::unexpected(RenderError{RenderErrorKind::TextureMissing, litsphere.string()});
    }
    lit = lit.convertToFormat(QImage::Format_RGBA8888);

    // Interleaved, because that is what the vertex layout declares and one
    // buffer is one binding instead of three.
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

    QRhi* rhi = d_->rhi;
    d_->vbuf.reset(rhi->newBuffer(QRhiBuffer::Immutable, QRhiBuffer::VertexBuffer,
                                  static_cast<quint32>(verts.size() * sizeof(float))));
    d_->ibuf.reset(rhi->newBuffer(QRhiBuffer::Immutable, QRhiBuffer::IndexBuffer,
                                  static_cast<quint32>(mesh.indexCount() * sizeof(uint32_t))));
    if (!d_->vbuf->create() || !d_->ibuf->create()) {
        return std::unexpected(RenderError{RenderErrorKind::Failed, "geometry buffers"});
    }

    // Recreate the litsphere texture at the image's size.
    d_->litTex.reset(rhi->newTexture(QRhiTexture::RGBA8, lit.size()));
    if (!d_->litTex->create()) {
        return std::unexpected(RenderError{RenderErrorKind::Failed, "litsphere texture"});
    }
    // Rebinding is required because the texture object changed identity.
    bindAll(d_->srb.get(), d_->ubuf.get(), d_->litTex.get(), d_->diffuseTex.get(),
            d_->sampler.get());
    if (!d_->srb->create()) {
        return std::unexpected(RenderError{RenderErrorKind::Failed, "rebinding"});
    }

    // A 1x1 white stand-in for the diffuse map, so `shading * diffuse` is a
    // no-op. Binding the litsphere here instead makes "diffuse" the matcap
    // sampled by the MESH's UVs, which paints arbitrary dark patches.
    QImage white(1, 1, QImage::Format_RGBA8888);
    white.fill(Qt::white);

    batch->uploadStaticBuffer(d_->vbuf.get(), verts.data());
    batch->uploadStaticBuffer(d_->ibuf.get(), mesh.index.data());
    batch->uploadTexture(d_->litTex.get(), lit);
    batch->uploadTexture(d_->diffuseTex.get(), white);

    d_->indexCount = static_cast<quint32>(mesh.indexCount());
    return {};
}

void SceneResources::updateCamera(QRhiResourceUpdateBatch* batch, const Camera& camera,
                                  float aspect) {
    QMatrix4x4 proj = d_->rhi->clipSpaceCorrMatrix();
    proj.perspective(camera.fovY, aspect, 0.1F, 1000.0F);

    // The MODEL rotates and the camera stays put (`glmodule.py`), which is what
    // keeps the litsphere's fixed eye-space lighting looking right.
    QMatrix4x4 model;
    model.rotate(camera.pitchDegrees, 1.0F, 0.0F, 0.0F);
    model.rotate(camera.yawDegrees, 0.0F, 1.0F, 0.0F);
    QMatrix4x4 view;
    view.translate(0.0F, 0.0F, -camera.distance);

    const QMatrix4x4 modelView = view * model;
    const QMatrix4x4 mvp       = proj * modelView;
    // Inverse-transpose: a normal is a covector and does not transform like a
    // position under a non-uniform transform.
    const QMatrix4x4 normalMat = modelView.inverted().transposed();

    batch->updateDynamicBuffer(d_->ubuf.get(), 0, 64, mvp.constData());
    batch->updateDynamicBuffer(d_->ubuf.get(), 64, 64, modelView.constData());
    batch->updateDynamicBuffer(d_->ubuf.get(), 128, 64, normalMat.constData());
    const float params[4] = {0.0F, 1.0F, 0.0F, 0.0F};  // AdditiveShading, normalmapIntensity
    batch->updateDynamicBuffer(d_->ubuf.get(), 192, 16, params);
}

void SceneResources::draw(QRhiCommandBuffer* cb, const QSize& pixelSize) {
    if (d_->indexCount == 0) return;

    cb->setGraphicsPipeline(d_->pipeline.get());
    cb->setViewport(
        {0, 0, static_cast<float>(pixelSize.width()), static_cast<float>(pixelSize.height())});
    cb->setShaderResources();
    const QRhiCommandBuffer::VertexInput vin(d_->vbuf.get(), 0);
    cb->setVertexInput(0, 1, &vin, d_->ibuf.get(), 0, QRhiCommandBuffer::IndexUInt32);
    cb->drawIndexed(d_->indexCount);
}

}  // namespace mh::render
