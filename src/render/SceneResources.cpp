// SPDX-License-Identifier: Apache-2.0
#include "makehuman/render/SceneResources.h"

#include <rhi/qrhi.h>
#include <QFile>

#include <cstring>
#include <vector>

namespace mh::render {
namespace {

/// The binding list, in one place. It has to be identical where the pipeline is
/// built and where the litsphere texture is replaced, or the two disagree about
/// which texture is at which slot.
void bindAll(QRhiShaderResourceBindings* srb, QRhiBuffer* ubuf, QRhiTexture* lit,
             QRhiTexture* diffuse, QRhiTexture* normalMap, QRhiSampler* sampler,
             QRhiBuffer* meshBuf) {
    srb->setBindings({
        QRhiShaderResourceBinding::uniformBuffer(
            0, QRhiShaderResourceBinding::VertexStage | QRhiShaderResourceBinding::FragmentStage,
            ubuf),
        QRhiShaderResourceBinding::sampledTexture(1, QRhiShaderResourceBinding::FragmentStage, lit,
                                                  sampler),
        QRhiShaderResourceBinding::sampledTexture(2, QRhiShaderResourceBinding::FragmentStage,
                                                  diffuse, sampler),
        // Always bound, even with no map: a slot the shader declares must have
        // a live texture or the bindings are incomplete. The shader branches on
        // params.z rather than sampling it.
        QRhiShaderResourceBinding::sampledTexture(3, QRhiShaderResourceBinding::FragmentStage,
                                                  normalMap, sampler),
        QRhiShaderResourceBinding::uniformBuffer(4, QRhiShaderResourceBinding::FragmentStage,
                                                 meshBuf),
    });
}

/// Three mat4 plus a vec4, matching the `Buf` block in litsphere.vert.
constexpr quint32 kUboSize = 64 * 3 + 16;
/// Interleaved position (3) + normal (3) + uv (2) + tangent (4).
///
/// The tangent is always present rather than switching layouts per mesh: 4
/// floats a vertex is ~1.3 MB on the subdivided mesh, against a second pipeline
/// and the state to pick between them.
constexpr quint32 kStride = 12 * sizeof(float);

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

/// Everything that belongs to one mesh rather than to the frame.
struct Drawable {
    std::unique_ptr<QRhiBuffer> vbuf;
    std::unique_ptr<QRhiBuffer> ibuf;
    std::unique_ptr<QRhiTexture> litTex;
    /// Null when the instance named no diffuse map; the shared white stand-in
    /// is bound instead.
    std::unique_ptr<QRhiTexture> diffuseTex;
    /// Null when the instance named no normal map. The shader then takes the
    /// no-map branch and never samples whatever is bound in that slot.
    std::unique_ptr<QRhiTexture> normalTex;
    /// Per-mesh material parameters; `Buf` is per frame and cannot hold them.
    std::unique_ptr<QRhiBuffer> meshBuf;
    float normalMapIntensity{1.0F};
    std::unique_ptr<QRhiShaderResourceBindings> srb;
    quint32 indexCount{};
};

struct SceneResources::Impl {
    QRhi* rhi{};
    // Shared: one camera for the frame, one sampler, one white diffuse
    // stand-in, one pipeline. Only the bindings and geometry vary per mesh.
    std::unique_ptr<QRhiBuffer> ubuf;
    std::unique_ptr<QRhiTexture> diffuseTex;
    /// Bound only so the pipeline has a layout to compile against; never read,
    /// because each drawable brings its own.
    std::unique_ptr<QRhiBuffer> layoutMeshBuf;
    std::unique_ptr<QRhiSampler> sampler;
    std::unique_ptr<QRhiShaderResourceBindings> layoutSrb;
    std::unique_ptr<QRhiGraphicsPipeline> pipeline;
    std::vector<Drawable> drawables;
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
    r->d_->diffuseTex.reset(rhi->newTexture(QRhiTexture::RGBA8, QSize(1, 1)));
    if (!r->d_->diffuseTex->create()) {
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

    // A pipeline needs an SRB only for its LAYOUT; the one bound at draw time
    // may be a different object as long as the layout matches. This one is
    // never drawn with -- each mesh builds its own with its own litsphere.
    // It binds diffuseTex in the litsphere slot purely because the layout only
    // cares that the slot is a sampled texture; a throwaway texture created
    // here would be destroyed while the SRB still pointed at it.
    r->d_->layoutSrb.reset(rhi->newShaderResourceBindings());
    r->d_->layoutMeshBuf.reset(rhi->newBuffer(QRhiBuffer::Dynamic, QRhiBuffer::UniformBuffer, 16));
    if (!r->d_->layoutMeshBuf->create()) {
        return std::unexpected(RenderError{RenderErrorKind::Failed, "material uniform buffer"});
    }
    bindAll(r->d_->layoutSrb.get(), r->d_->ubuf.get(), r->d_->diffuseTex.get(),
            r->d_->diffuseTex.get(), r->d_->diffuseTex.get(), r->d_->sampler.get(),
            r->d_->layoutMeshBuf.get());
    if (!r->d_->layoutSrb->create()) {
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
        {0, 3, QRhiVertexInputAttribute::Float4, 8 * sizeof(float)},
    });
    r->d_->pipeline->setVertexInputLayout(layout);
    r->d_->pipeline->setShaderResourceBindings(r->d_->layoutSrb.get());
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
                                                        std::span<const MeshInstance> meshes) {
    if (meshes.empty()) {
        return std::unexpected(RenderError{RenderErrorKind::EmptyMesh, "no meshes"});
    }

    QRhi* rhi = d_->rhi;

    // NOTHING is queued onto `batch` until every mesh has been built. A batch
    // holds raw pointers and does not learn that a resource died, so queueing
    // inside the loop and then failing on a later mesh would leave the caller
    // holding a batch that references freed buffers -- and ViewportWidget
    // submits its batch even when upload fails, which crashed in the Metal
    // backend. Build first, queue second.
    struct Pending {
        Drawable drawable;
        std::vector<float> verts;
        QImage lit;
        QImage diffuse;    ///< null when the instance named no diffuse map
        QImage normalMap;  ///< null when the instance named no normal map
    };

    std::vector<Pending> pending;
    pending.reserve(meshes.size());

    for (const MeshInstance& instance : meshes) {
        const foundation::RenderView& mesh = instance.mesh;
        if (mesh.vertexCount() == 0 || mesh.indexCount() == 0) {
            return std::unexpected(RenderError{RenderErrorKind::EmptyMesh, {}});
        }

        // An in-memory litsphere wins over a path: a blended skin tone has no
        // file behind it. Copied into the QImage rather than wrapped, because
        // the upload happens after this loop and a view over caller memory
        // would have to stay valid until then for no gain.
        QImage lit;
        if (!instance.litsphereRgba.empty()) {
            const size_t need = static_cast<size_t>(instance.litsphereWidth) *
                                static_cast<size_t>(instance.litsphereHeight) * 4U;
            if (instance.litsphereWidth <= 0 || instance.litsphereHeight <= 0 ||
                instance.litsphereRgba.size() != need) {
                return std::unexpected(
                    RenderError{RenderErrorKind::TextureMissing,
                                "in-memory litsphere does not match its declared size"});
            }
            lit =
                QImage(instance.litsphereWidth, instance.litsphereHeight, QImage::Format_RGBA8888);
            std::memcpy(lit.bits(), instance.litsphereRgba.data(), need);
        } else {
            lit = QImage(QString::fromStdString(instance.litsphere.string()));
            if (lit.isNull()) {
                return std::unexpected(
                    RenderError{RenderErrorKind::TextureMissing, instance.litsphere.string()});
            }
            lit = lit.convertToFormat(QImage::Format_RGBA8888);
        }

        // A named-but-unloadable diffuse is an ERROR, not a silent fall back to
        // white: a skin whose texture path is wrong would otherwise render as a
        // plausible untextured body and look like a shading bug.
        QImage diffuse;
        if (!instance.diffuse.empty()) {
            diffuse = QImage(QString::fromStdString(instance.diffuse.string()));
            if (diffuse.isNull()) {
                return std::unexpected(
                    RenderError{RenderErrorKind::TextureMissing, instance.diffuse.string()});
            }
            diffuse = diffuse.convertToFormat(QImage::Format_RGBA8888);
        }

        QImage normalMap;
        if (!instance.normalMap.empty()) {
            normalMap = QImage(QString::fromStdString(instance.normalMap.string()));
            if (normalMap.isNull()) {
                return std::unexpected(
                    RenderError{RenderErrorKind::TextureMissing, instance.normalMap.string()});
            }
            normalMap = normalMap.convertToFormat(QImage::Format_RGBA8888);
        }

        // Interleaved, because that is what the vertex layout declares and one
        // buffer is one binding instead of three.
        std::vector<float> verts;
        verts.reserve(mesh.vertexCount() * 8);
        const bool hasN  = mesh.vnorm.size() == mesh.vertexCount();
        const bool hasT  = mesh.texco.size() == mesh.vertexCount();
        const bool hasTg = mesh.vtang.size() == mesh.vertexCount();
        for (size_t i = 0; i < mesh.vertexCount(); ++i) {
            verts.push_back(mesh.coord[i].x);
            verts.push_back(mesh.coord[i].y);
            verts.push_back(mesh.coord[i].z);
            verts.push_back(hasN ? mesh.vnorm[i].x : 0.0F);
            verts.push_back(hasN ? mesh.vnorm[i].y : 0.0F);
            verts.push_back(hasN ? mesh.vnorm[i].z : 1.0F);
            verts.push_back(hasT ? mesh.texco[i].x : 0.0F);
            verts.push_back(hasT ? mesh.texco[i].y : 0.0F);
            // A mesh with no tangents still fills the slot: the attribute is
            // always in the layout, and +X with handedness +1 is a valid frame
            // for the branch that never samples it.
            verts.push_back(hasTg ? mesh.vtang[i].x : 1.0F);
            verts.push_back(hasTg ? mesh.vtang[i].y : 0.0F);
            verts.push_back(hasTg ? mesh.vtang[i].z : 0.0F);
            verts.push_back(hasTg ? mesh.vtang[i].w : 1.0F);
        }

        Drawable dr;
        dr.vbuf.reset(rhi->newBuffer(QRhiBuffer::Immutable, QRhiBuffer::VertexBuffer,
                                     static_cast<quint32>(verts.size() * sizeof(float))));
        dr.ibuf.reset(rhi->newBuffer(QRhiBuffer::Immutable, QRhiBuffer::IndexBuffer,
                                     static_cast<quint32>(mesh.indexCount() * sizeof(uint32_t))));
        if (!dr.vbuf->create() || !dr.ibuf->create()) {
            return std::unexpected(RenderError{RenderErrorKind::Failed, "geometry buffers"});
        }

        dr.litTex.reset(rhi->newTexture(QRhiTexture::RGBA8, lit.size()));
        if (!dr.litTex->create()) {
            return std::unexpected(RenderError{RenderErrorKind::Failed, "litsphere texture"});
        }

        // Its own bindings, so each mesh samples its own litsphere. Sharing one
        // SRB would shade every mesh with whichever texture uploaded last.
        dr.srb.reset(rhi->newShaderResourceBindings());
        if (!diffuse.isNull()) {
            dr.diffuseTex.reset(rhi->newTexture(QRhiTexture::RGBA8, diffuse.size()));
            if (!dr.diffuseTex->create()) {
                return std::unexpected(
                    RenderError{RenderErrorKind::TextureMissing, instance.diffuse.string()});
            }
        }
        if (!normalMap.isNull()) {
            dr.normalTex.reset(rhi->newTexture(QRhiTexture::RGBA8, normalMap.size()));
            if (!dr.normalTex->create()) {
                return std::unexpected(
                    RenderError{RenderErrorKind::TextureMissing, instance.normalMap.string()});
            }
        }
        dr.normalMapIntensity = instance.normalMapIntensity;
        dr.meshBuf.reset(rhi->newBuffer(QRhiBuffer::Dynamic, QRhiBuffer::UniformBuffer, 16));
        if (!dr.meshBuf->create()) {
            return std::unexpected(RenderError{RenderErrorKind::Failed, "material uniform buffer"});
        }
        bindAll(dr.srb.get(), d_->ubuf.get(), dr.litTex.get(),
                dr.diffuseTex ? dr.diffuseTex.get() : d_->diffuseTex.get(),
                dr.normalTex ? dr.normalTex.get() : d_->diffuseTex.get(), d_->sampler.get(),
                dr.meshBuf.get());
        if (!dr.srb->create()) {
            return std::unexpected(
                RenderError{RenderErrorKind::Failed, "shader resource bindings"});
        }

        dr.indexCount = static_cast<quint32>(mesh.indexCount());
        pending.push_back(Pending{std::move(dr), std::move(verts), std::move(lit),
                                  std::move(diffuse), std::move(normalMap)});
    }

    // Every mesh built, so it is now safe to queue: no early return remains.
    //
    // A 1x1 white stand-in for the diffuse map, so `shading * diffuse` is a
    // no-op. Binding the litsphere here instead makes "diffuse" the matcap
    // sampled by the MESH's UVs, which paints arbitrary dark patches.
    QImage white(1, 1, QImage::Format_RGBA8888);
    white.fill(Qt::white);
    batch->uploadTexture(d_->diffuseTex.get(), white);

    std::vector<Drawable> built;
    built.reserve(pending.size());
    for (Pending& p : pending) {
        batch->uploadStaticBuffer(p.drawable.vbuf.get(), p.verts.data());
        batch->uploadTexture(p.drawable.litTex.get(), p.lit);
        if (p.drawable.diffuseTex) batch->uploadTexture(p.drawable.diffuseTex.get(), p.diffuse);
        if (p.drawable.normalTex) batch->uploadTexture(p.drawable.normalTex.get(), p.normalMap);
        // x = intensity, y = 1 when a normal map is bound. Written per mesh
        // because whether one exists is a material property, not a frame one.
        const float material[4] = {p.drawable.normalMapIntensity,
                                   p.drawable.normalTex ? 1.0F : 0.0F, 0.0F, 0.0F};
        batch->updateDynamicBuffer(p.drawable.meshBuf.get(), 0, 16, material);
        built.push_back(std::move(p.drawable));
    }
    // Index data comes from the caller's mesh, which outlives this call.
    for (size_t i = 0; i < meshes.size(); ++i) {
        batch->uploadStaticBuffer(built[i].ibuf.get(), meshes[i].mesh.index.data());
    }

    // Swapped in only once every mesh succeeded, so a failure part-way leaves
    // the previous frame's meshes intact rather than half-replaced.
    d_->drawables = std::move(built);
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
    if (d_->drawables.empty()) return;

    // Pipeline and viewport are the same for every mesh, so they are set once.
    cb->setGraphicsPipeline(d_->pipeline.get());
    cb->setViewport(
        {0, 0, static_cast<float>(pixelSize.width()), static_cast<float>(pixelSize.height())});

    for (const Drawable& dr : d_->drawables) {
        cb->setShaderResources(dr.srb.get());
        const QRhiCommandBuffer::VertexInput vin(dr.vbuf.get(), 0);
        cb->setVertexInput(0, 1, &vin, dr.ibuf.get(), 0, QRhiCommandBuffer::IndexUInt32);
        cb->drawIndexed(dr.indexCount);
    }
}

}  // namespace mh::render
