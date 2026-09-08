// SPDX-License-Identifier: Apache-2.0
#include "makehuman/render/SceneResources.h"

#include <rhi/qrhi.h>
#include <QFile>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <limits>
#include <vector>

namespace mh::render {
namespace {

/// The binding list, in one place. It has to be identical where the pipeline is
/// built and where the litsphere texture is replaced, or the two disagree about
/// which texture is at which slot.
void bindAll(QRhiShaderResourceBindings* srb, QRhiBuffer* ubuf, QRhiTexture* lit,
             QRhiTexture* diffuse, QRhiTexture* normalMap, QRhiTexture* aoMap, QRhiSampler* sampler,
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
        QRhiShaderResourceBinding::sampledTexture(5, QRhiShaderResourceBinding::FragmentStage,
                                                  aoMap, sampler),
    });
}

/// UV-mapped textures are stored bottom-up, because OBJ's V origin is the
/// LOWER-left of the image while QRhi samples v=0 at the FIRST row. Without
/// this every diffuse, normal and AO map is read upside down.
///
/// The reference makes the same correction for the same reason -- GL's t=0 is
/// also the first row supplied, so `legacy/python/lib/texture.py:167` flips
/// before `glTexImage2D`.
///
/// The shipped eye is what makes it concrete: the high-poly proxy's front
/// geometry is a CORNEA UV'd to a small disc, whose texel is
/// (182,195,255, alpha 0) read correctly -- invisible, so the iris behind shows
/// through -- and (169,169,165, alpha 255) read upside down, an opaque pale
/// disc. That disc was the blank white oval the eyes rendered as.
///
/// NOT applied to the litsphere: a matcap is indexed by the view-space NORMAL
/// (`normal * 0.495 + 0.5`), not by a mesh UV, so it is not in this coordinate
/// system at all. Whether the reference's own flip leaves our matcap lookup
/// vertically mirrored against it is a separate parity question -- see
/// memory/todo.md.
QImage bottomUp(QImage img) {
    return img.flipped(Qt::Vertical);
}

/// Three mat4 plus a vec4, matching the `Buf` block in litsphere.vert.
constexpr quint32 kUboSize = 64 * 3 + 16;
/// Three vec4, matching the `MeshBuf` block: `material`, `pbr`, `base`. Both
/// shaders declare all three, so one size serves both pipelines and the SRB
/// layout that the two share stays identical.
constexpr quint32 kMeshUboSize = 48;
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
    /// Drawn in the second pass, blended. See MeshInstance::transparent.
    bool transparent{false};
    std::unique_ptr<QRhiBuffer> vbuf;
    std::unique_ptr<QRhiBuffer> ibuf;
    std::unique_ptr<QRhiTexture> litTex;
    /// Null when the instance named no diffuse map; the shared white stand-in
    /// is bound instead.
    std::unique_ptr<QRhiTexture> diffuseTex;
    /// Null when the instance named no normal map. The shader then takes the
    /// no-map branch and never samples whatever is bound in that slot.
    std::unique_ptr<QRhiTexture> normalTex;
    /// Null when the instance named no AO map; the shader then skips it.
    std::unique_ptr<QRhiTexture> aoTex;
    /// Per-mesh material parameters; `Buf` is per frame and cannot hold them.
    std::unique_ptr<QRhiBuffer> meshBuf;
    float normalMapIntensity{1.0F};
    /// Read only by the PBR shader; see MeshInstance.
    float metallic{0.0F};
    float roughness{0.6F};
    foundation::Vec3 baseColour{1.0F, 1.0F, 1.0F};
    float opacity{1.0F};
    std::unique_ptr<QRhiShaderResourceBindings> srb;
    quint32 indexCount{};
};

/// One opaque and one blended pipeline for a single shading model.
struct Pipelines {
    std::unique_ptr<QRhiGraphicsPipeline> opaque;
    /// The same pipeline with alpha blending on and depth WRITE off.
    std::unique_ptr<QRhiGraphicsPipeline> blend;
    /// The same pipeline again with `PolygonMode::Line`. Null where the device
    /// cannot do it, which is what `wireframeSupported()` reports.
    std::unique_ptr<QRhiGraphicsPipeline> wire;
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
    /// Indexed by `ShadingModel`. All four pipelines are built up front because
    /// a QRhi pipeline's state -- including its shader stages -- is baked at
    /// create(), so the viewport's shading toggle cannot mutate one in place.
    std::array<Pipelines, 2> pipelines;
    ShadingModel model{ShadingModel::Litsphere};
    bool wireframe{false};
    /// The ground grid: its own geometry, its own pipeline, and an SRB that
    /// binds only the camera block. Null when the shaders are missing, which is
    /// not fatal -- the scene still draws, without a floor.
    bool grid{false};
    /// Where the floor is. NaN until the first upload, so the first scene
    /// always writes the buffer.
    float gridFeetY{std::numeric_limits<float>::quiet_NaN()};
    std::unique_ptr<QRhiBuffer> gridBuf;
    std::unique_ptr<QRhiGraphicsPipeline> gridPipeline;
    std::unique_ptr<QRhiShaderResourceBindings> gridSrb;
    quint32 gridVertices{};
    std::vector<Drawable> drawables;

    [[nodiscard]] const Pipelines& active() const { return pipelines[static_cast<size_t>(model)]; }
};

/// Builds the opaque/blend pair for one shading model. Everything but the
/// shader stages is identical between the two models -- same vertex layout,
/// same bindings, same depth and cull state -- so the differences are the
/// arguments and the rest is written once.
[[nodiscard]] bool buildPipelines(QRhi* rhi, Pipelines& out, const QShader& vs, const QShader& fs,
                                  const QRhiVertexInputLayout& layout,
                                  QRhiShaderResourceBindings* layoutSrb,
                                  QRhiRenderPassDescriptor* rp, int sampleCount) {
    const auto configure = [&](QRhiGraphicsPipeline* p) {
        p->setShaderStages({{QRhiShaderStage::Vertex, vs}, {QRhiShaderStage::Fragment, fs}});
        p->setVertexInputLayout(layout);
        p->setShaderResourceBindings(layoutSrb);
        p->setRenderPassDescriptor(rp);
        p->setDepthTest(true);
        // Winding IS consistent after fan triangulation -- verified by
        // rendering with culling on and off and getting byte-identical pixel
        // statistics.
        p->setCullMode(QRhiGraphicsPipeline::Back);
        // Must match the target, or pipeline creation fails.
        p->setSampleCount(sampleCount);
    };

    out.opaque.reset(rhi->newGraphicsPipeline());
    configure(out.opaque.get());
    out.opaque->setDepthWrite(true);
    if (!out.opaque->create()) return false;

    // The blended variant differs in exactly two things:
    //
    //  * source-alpha / one-minus-source-alpha blending, which is what makes
    //    the shader's `outColor.a = diffuse.a` mean anything. Without it the
    //    alpha reached the framebuffer and was discarded, so the shipped
    //    `transparent True` eye material rendered solid while its GLB export
    //    said `alphaMode: BLEND`.
    //  * depth WRITE off, test still on. A blended surface must not occlude
    //    what is drawn after it, but it must still be hidden by opaque
    //    geometry already in front of it.
    out.blend.reset(rhi->newGraphicsPipeline());
    configure(out.blend.get());
    out.blend->setDepthWrite(false);
    QRhiGraphicsPipeline::TargetBlend blend;
    blend.enable   = true;
    blend.srcColor = QRhiGraphicsPipeline::SrcAlpha;
    blend.dstColor = QRhiGraphicsPipeline::OneMinusSrcAlpha;
    blend.srcAlpha = QRhiGraphicsPipeline::One;
    blend.dstAlpha = QRhiGraphicsPipeline::OneMinusSrcAlpha;
    out.blend->setTargetBlends({blend});
    if (!out.blend->create()) return false;

    // Wireframe, where the device has it. Back-face culling stays ON: the
    // reference's wireframe hides the far side of the body, and without it the
    // silhouette fills with the edges of faces pointing away.
    if (!rhi->isFeatureSupported(QRhi::NonFillPolygonMode)) return true;
    out.wire.reset(rhi->newGraphicsPipeline());
    configure(out.wire.get());
    out.wire->setDepthWrite(true);
    out.wire->setPolygonMode(QRhiGraphicsPipeline::Line);
    // A failure here is not fatal to the scene: everything else is built and
    // usable, and `wireframeSupported()` then answers false because the
    // pipeline is null.
    if (!out.wire->create()) out.wire.reset();
    return true;
}

/// Line endpoints for the two grids the reference draws (`core/mhmain.py:
/// 646-692`): a ground plane at the feet and a vertical backplane behind the
/// figure. Both are needed, and the first render of only the ground one shows
/// why -- at the default head-on camera a horizontal plane is EDGE-ON, so the
/// whole feature was a single faint line across the hips.
///
/// Pairs, for `Topology::Lines`: a strip would join the end of one line to the
/// start of the next and draw a staircase across the floor.
///
/// @param feetY where the ground is. The mesh is CENTRED in memory (the shipped
///        base spans y -8.4 to +8.5) and only the exporters put its feet on
///        zero, so a grid at y = 0 sits at hip height -- which is exactly what
///        the first version of this drew.
[[nodiscard]] std::vector<float> gridVertices(float feetY) {
    std::vector<float> v;
    const auto line = [&v](float x0, float y0, float z0, float x1, float y1, float z1) {
        v.insert(v.end(), {x0, y0, z0, x1, y1, z1});
    };
    const int steps = static_cast<int>(SceneResources::kGridExtent / SceneResources::kGridStep);
    const float e   = SceneResources::kGridExtent;
    for (int i = -steps; i <= steps; ++i) {
        const float at = static_cast<float>(i) * SceneResources::kGridStep;
        // The ground, on the XZ plane at the feet.
        line(at, feetY, -e, at, feetY, e);
        line(-e, feetY, at, e, feetY, at);
    }
    // The backplane, on the XY plane behind the figure. It reaches from the
    // ground to well over head height rather than being square, because a
    // standing figure is twice as tall as it is wide.
    const float top = feetY + 2.0F * e;
    for (int i = -steps; i <= steps; ++i) {
        const float at = static_cast<float>(i) * SceneResources::kGridStep;
        line(at, feetY, -e, at, top, -e);
    }
    for (float y = feetY; y <= top + 0.5F * SceneResources::kGridStep;
         y += SceneResources::kGridStep) {
        line(-e, y, -e, e, y, -e);
    }
    return v;
}

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
    auto pbrVs = loadShader(shaderDir / "pbr.vert.qsb");
    if (!pbrVs) return std::unexpected(pbrVs.error());
    auto pbrFs = loadShader(shaderDir / "pbr.frag.qsb");
    if (!pbrFs) return std::unexpected(pbrFs.error());

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
    r->d_->layoutMeshBuf.reset(
        rhi->newBuffer(QRhiBuffer::Dynamic, QRhiBuffer::UniformBuffer, kMeshUboSize));
    if (!r->d_->layoutMeshBuf->create()) {
        return std::unexpected(RenderError{RenderErrorKind::Failed, "material uniform buffer"});
    }
    bindAll(r->d_->layoutSrb.get(), r->d_->ubuf.get(), r->d_->diffuseTex.get(),
            r->d_->diffuseTex.get(), r->d_->diffuseTex.get(), r->d_->diffuseTex.get(),
            r->d_->sampler.get(), r->d_->layoutMeshBuf.get());
    if (!r->d_->layoutSrb->create()) {
        return std::unexpected(RenderError{RenderErrorKind::Failed, "shader resource bindings"});
    }

    QRhiVertexInputLayout layout;
    layout.setBindings({{kStride}});
    layout.setAttributes({
        {0, 0, QRhiVertexInputAttribute::Float3, 0},
        {0, 1, QRhiVertexInputAttribute::Float3, 3 * sizeof(float)},
        {0, 2, QRhiVertexInputAttribute::Float2, 6 * sizeof(float)},
        {0, 3, QRhiVertexInputAttribute::Float4, 8 * sizeof(float)},
    });

    // Both models share this layout and this SRB layout deliberately: the PBR
    // shader reads the same vertex attributes and the same bindings, differing
    // only in what it does with them. That is what lets `upload` run once and
    // `setShadingModel` be free.
    if (!buildPipelines(rhi, r->d_->pipelines[static_cast<size_t>(ShadingModel::Litsphere)], *vs,
                        *fs, layout, r->d_->layoutSrb.get(), rp, sampleCount)) {
        return std::unexpected(RenderError{RenderErrorKind::Failed, "litsphere pipelines"});
    }
    if (!buildPipelines(rhi, r->d_->pipelines[static_cast<size_t>(ShadingModel::Pbr)], *pbrVs,
                        *pbrFs, layout, r->d_->layoutSrb.get(), rp, sampleCount)) {
        return std::unexpected(RenderError{RenderErrorKind::Failed, "pbr pipelines"});
    }

    // The ground grid: its own geometry, its own pipeline, and an SRB that
    // binds only the camera block -- it has no material, no texture and no
    // per-mesh uniforms, so the mesh layout would oblige it to carry four
    // textures it never samples. Built here rather than in a helper because
    // `Impl` is private and this is the one place that can see it.
    //
    // A missing shader pair is reported like any other rather than leaving the
    // toggle inert: a Grid button that ticks and draws nothing is the painted
    // no-op the whole toolbar group was held back to avoid.
    auto gridVs = loadShader(shaderDir / "grid.vert.qsb");
    if (!gridVs) return std::unexpected(gridVs.error());
    auto gridFs = loadShader(shaderDir / "grid.frag.qsb");
    if (!gridFs) return std::unexpected(gridFs.error());

    // Dynamic, not Immutable: the floor follows the FEET, and a character who
    // gets taller moves them. 1.9 kB re-uploaded only when that height changes.
    const std::vector<float> verts = gridVertices(0.0F);
    r->d_->gridVertices            = static_cast<quint32>(verts.size() / 3);
    r->d_->gridBuf.reset(rhi->newBuffer(QRhiBuffer::Dynamic, QRhiBuffer::VertexBuffer,
                                        static_cast<quint32>(verts.size() * sizeof(float))));
    if (!r->d_->gridBuf->create()) {
        return std::unexpected(RenderError{RenderErrorKind::Failed, "grid vertex buffer"});
    }

    r->d_->gridSrb.reset(rhi->newShaderResourceBindings());
    r->d_->gridSrb->setBindings({QRhiShaderResourceBinding::uniformBuffer(
        0, QRhiShaderResourceBinding::VertexStage, r->d_->ubuf.get())});
    if (!r->d_->gridSrb->create()) {
        return std::unexpected(RenderError{RenderErrorKind::Failed, "grid bindings"});
    }

    QRhiVertexInputLayout gridLayout;
    gridLayout.setBindings({{3 * sizeof(float)}});
    gridLayout.setAttributes({{0, 0, QRhiVertexInputAttribute::Float3, 0}});

    r->d_->gridPipeline.reset(rhi->newGraphicsPipeline());
    r->d_->gridPipeline->setShaderStages(
        {{QRhiShaderStage::Vertex, *gridVs}, {QRhiShaderStage::Fragment, *gridFs}});
    r->d_->gridPipeline->setVertexInputLayout(gridLayout);
    r->d_->gridPipeline->setShaderResourceBindings(r->d_->gridSrb.get());
    r->d_->gridPipeline->setRenderPassDescriptor(rp);
    r->d_->gridPipeline->setTopology(QRhiGraphicsPipeline::Lines);
    // Depth test AND write: the body must hide the lines behind it, and the
    // lines must hide each other consistently where they cross.
    r->d_->gridPipeline->setDepthTest(true);
    r->d_->gridPipeline->setDepthWrite(true);
    r->d_->gridPipeline->setSampleCount(sampleCount);
    if (!r->d_->gridPipeline->create()) {
        return std::unexpected(RenderError{RenderErrorKind::Failed, "grid pipeline"});
    }
    return r;
}

std::expected<void, RenderError> SceneResources::upload(QRhiResourceUpdateBatch* batch,
                                                        std::span<const MeshInstance> meshes) {
    if (meshes.empty()) {
        return std::unexpected(RenderError{RenderErrorKind::EmptyMesh, "no meshes"});
    }

    // The floor sits at the lowest vertex of everything on screen, so it
    // follows the feet through a morph, a pose or a pair of shoes. Re-uploaded
    // only when that height actually moves -- a slider drag rebuilds the scene
    // 60 times a second and the grid is the same 1.9 kB each time.
    float lowest = std::numeric_limits<float>::max();
    for (const MeshInstance& m : meshes) {
        for (const foundation::Vec3& c : m.mesh.coord)
            lowest = std::min(lowest, c.y);
    }
    if (std::isfinite(lowest) && lowest != d_->gridFeetY) {
        d_->gridFeetY                  = lowest;
        const std::vector<float> verts = gridVertices(lowest);
        batch->updateDynamicBuffer(
            d_->gridBuf.get(), 0, static_cast<quint32>(verts.size() * sizeof(float)), verts.data());
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
        QImage aoMap;      ///< null when the instance named no AO map
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
            diffuse = bottomUp(diffuse.convertToFormat(QImage::Format_RGBA8888));
        }

        QImage normalMap;
        if (!instance.normalMap.empty()) {
            normalMap = QImage(QString::fromStdString(instance.normalMap.string()));
            if (normalMap.isNull()) {
                return std::unexpected(
                    RenderError{RenderErrorKind::TextureMissing, instance.normalMap.string()});
            }
            normalMap = bottomUp(normalMap.convertToFormat(QImage::Format_RGBA8888));
        }

        QImage aoMap;
        if (!instance.aoMap.empty()) {
            aoMap = QImage(QString::fromStdString(instance.aoMap.string()));
            if (aoMap.isNull()) {
                return std::unexpected(
                    RenderError{RenderErrorKind::TextureMissing, instance.aoMap.string()});
            }
            aoMap = bottomUp(aoMap.convertToFormat(QImage::Format_RGBA8888));
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
        dr.transparent = instance.transparent;
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
        if (!aoMap.isNull()) {
            dr.aoTex.reset(rhi->newTexture(QRhiTexture::RGBA8, aoMap.size()));
            if (!dr.aoTex->create()) {
                return std::unexpected(
                    RenderError{RenderErrorKind::TextureMissing, instance.aoMap.string()});
            }
        }
        dr.normalMapIntensity = instance.normalMapIntensity;
        dr.metallic           = instance.metallic;
        dr.roughness          = instance.roughness;
        dr.baseColour         = instance.baseColour;
        dr.opacity            = instance.opacity;
        dr.meshBuf.reset(
            rhi->newBuffer(QRhiBuffer::Dynamic, QRhiBuffer::UniformBuffer, kMeshUboSize));
        if (!dr.meshBuf->create()) {
            return std::unexpected(RenderError{RenderErrorKind::Failed, "material uniform buffer"});
        }
        bindAll(dr.srb.get(), d_->ubuf.get(), dr.litTex.get(),
                dr.diffuseTex ? dr.diffuseTex.get() : d_->diffuseTex.get(),
                dr.normalTex ? dr.normalTex.get() : d_->diffuseTex.get(),
                dr.aoTex ? dr.aoTex.get() : d_->diffuseTex.get(), d_->sampler.get(),
                dr.meshBuf.get());
        if (!dr.srb->create()) {
            return std::unexpected(
                RenderError{RenderErrorKind::Failed, "shader resource bindings"});
        }

        dr.indexCount = static_cast<quint32>(mesh.indexCount());
        pending.push_back(Pending{std::move(dr), std::move(verts), std::move(lit),
                                  std::move(diffuse), std::move(normalMap), std::move(aoMap)});
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
        if (p.drawable.aoTex) batch->uploadTexture(p.drawable.aoTex.get(), p.aoMap);
        // x = intensity, y = 1 when a normal map is bound. Written per mesh
        // because whether one exists is a material property, not a frame one.
        const float material[12] = {p.drawable.normalMapIntensity,
                                    p.drawable.normalTex ? 1.0F : 0.0F,
                                    p.drawable.aoTex ? 1.0F : 0.0F, 0.0F,
                                    // The second vec4 is `pbr`: metallic, then
                                    // roughness. The litsphere shader declares
                                    // it and never reads it, which keeps one
                                    // buffer size and one SRB layout for both.
                                    p.drawable.metallic, p.drawable.roughness, 0.0F, 0.0F,
                                    // The third vec4 is `base`: the material's
                                    // diffuse colour, glTF's baseColorFactor, and
                                    // in w its opacity, that factor's alpha. Also
                                    // declared-and-ignored by the litsphere.
                                    p.drawable.baseColour.x, p.drawable.baseColour.y,
                                    p.drawable.baseColour.z, p.drawable.opacity};
        batch->updateDynamicBuffer(p.drawable.meshBuf.get(), 0, kMeshUboSize, material);
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

// The MODEL rotates and the camera stays put (`glmodule.py`), which is what
// keeps the litsphere's fixed eye-space lighting looking right.
QMatrix4x4 modelMatrixOf(const Camera& camera) {
    QMatrix4x4 m;
    m.rotate(camera.pitchDegrees, 1.0F, 0.0F, 0.0F);
    m.rotate(camera.yawDegrees, 0.0F, 1.0F, 0.0F);
    return m;
}

void SceneResources::updateCamera(QRhiResourceUpdateBatch* batch, const Camera& camera,
                                  float aspect) {
    QMatrix4x4 proj = d_->rhi->clipSpaceCorrMatrix();
    proj.perspective(camera.fovY, aspect, 0.1F, 1000.0F);

    const QMatrix4x4 model = modelMatrixOf(camera);
    // Pan is applied in EYE space, after the model rotation, so dragging moves
    // the model across the screen rather than along its own axes.
    QMatrix4x4 view;
    view.translate(camera.panX, camera.panY, -camera.distance);

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

void SceneResources::setShadingModel(ShadingModel model) {
    d_->model = model;
}

void SceneResources::setWireframe(bool on) {
    d_->wireframe = on;
}

bool SceneResources::wireframe() const {
    return d_->wireframe;
}

void SceneResources::setGrid(bool on) {
    d_->grid = on;
}

bool SceneResources::grid() const {
    return d_->grid;
}

bool SceneResources::wireframeSupported() const {
    // Asked of the pipeline that exists rather than of the QRhi feature flag: a
    // device can advertise the feature and still fail to CREATE the pipeline,
    // and it is the pipeline the draw loop needs.
    return d_->pipelines[static_cast<size_t>(ShadingModel::Litsphere)].wire != nullptr &&
           d_->pipelines[static_cast<size_t>(ShadingModel::Pbr)].wire != nullptr;
}

void SceneResources::draw(QRhiCommandBuffer* cb, const QSize& pixelSize) {
    if (d_->drawables.empty()) return;

    cb->setViewport(
        {0, 0, static_cast<float>(pixelSize.width()), static_cast<float>(pixelSize.height())});

    // The floor first: it is opaque and it is behind everything, so drawing it
    // before the body lets the depth test do the occluding. Skipped entirely
    // when off, which costs nothing when it is.
    if (d_->grid && d_->gridPipeline) {
        cb->setGraphicsPipeline(d_->gridPipeline.get());
        cb->setShaderResources(d_->gridSrb.get());
        const QRhiCommandBuffer::VertexInput gridIn(d_->gridBuf.get(), 0);
        cb->setVertexInput(0, 1, &gridIn);
        cb->draw(d_->gridVertices);
    }

    // Opaque first, then blended. Order matters for the blend equation: a
    // transparent surface drawn BEFORE the opaque geometry behind it blends
    // against the clear colour instead, which looks like the transparency
    // simply not working.
    //
    // Within the transparent set there is no back-to-front sort. One shipped
    // material is transparent (the eyes) so the question does not arise yet;
    // it will the moment a second one lands, and a sort belongs then rather
    // than as machinery nothing exercises.
    // Wireframe replaces BOTH passes: it is a view of the whole scene, and a
    // blended wireframe over a wireframe body would show the eyes' edges
    // through the head for no benefit.
    const bool wire = d_->wireframe && d_->active().wire;
    const auto pass = [&](bool transparent) {
        bool bound = false;
        for (const Drawable& dr : d_->drawables) {
            if (dr.transparent != transparent) continue;
            if (!bound) {
                const Pipelines& pl = d_->active();
                cb->setGraphicsPipeline(wire          ? pl.wire.get()
                                        : transparent ? pl.blend.get()
                                                      : pl.opaque.get());
                bound = true;
            }
            cb->setShaderResources(dr.srb.get());
            const QRhiCommandBuffer::VertexInput vin(dr.vbuf.get(), 0);
            cb->setVertexInput(0, 1, &vin, dr.ibuf.get(), 0, QRhiCommandBuffer::IndexUInt32);
            cb->drawIndexed(dr.indexCount);
        }
    };
    pass(false);
    pass(true);
}

}  // namespace mh::render
