// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "makehuman/foundation/Geometry.h"
#include "makehuman/foundation/Types.h"

#include <QMatrix4x4>
#include <QSize>

#include <cstdint>
#include <expected>
#include <filesystem>
#include <memory>
#include <span>
#include <string>
#include <vector>

class QRhi;
class QRhiCommandBuffer;
class QRhiRenderPassDescriptor;
class QRhiResourceUpdateBatch;

namespace mh::render {

/// MSAA samples every target in this application asks for.
///
/// One constant because the viewport and the offscreen renderer had drifted
/// apart at exactly this number -- the widget requested 4 and the production
/// render passed a hard-coded 1, so `makehuman --render` wrote an image whose
/// silhouette had **no partial coverage at any pixel** while the same scene on
/// screen was smooth. Sharing SceneResources was not enough, because the sample
/// count is the one parameter that lives outside it.
///
/// A request, not a guarantee: a backend may refuse it, so both users check
/// what they actually got rather than assuming.
inline constexpr int kSampleCount = 4;

/// How the camera is placed. The MODEL rotates and the camera stays put, which
/// is the reference's convention (`glmodule.py`) and is what makes the
/// litsphere's fixed eye-space lighting look right.
struct Camera {
    float fovY{30.0F};
    float distance{45.0F};  ///< in mesh units; the base mesh is ~17 dm tall
    float yawDegrees{0.0F};
    float pitchDegrees{0.0F};
    /// Pan, in MESH units, applied in eye space so it tracks the screen rather
    /// than the model. The `.mhm` format stores this as a fraction of the
    /// model's half-extents instead -- see `core::OrbitView::translation`; the
    /// conversion belongs to whoever knows the model's size, not here.
    float panX{0.0F};
    float panY{0.0F};
};

enum class RenderErrorKind {
    NoDevice,
    ShaderMissing,
    TextureMissing,
    EmptyMesh,
    Failed,
};

struct RenderError {
    RenderErrorKind kind{};
    std::string detail;

    [[nodiscard]] std::string message() const;
};

/// One mesh to draw, with the litsphere it is shaded by.
///
/// A dressed character is the body plus every worn proxy, and each carries its
/// own material -- so the litsphere belongs to the mesh, not to the frame.
struct MeshInstance {
    foundation::RenderView mesh;
    std::filesystem::path litsphere{};

    /// The material's diffuse/albedo map, sampled by the MESH's UVs.
    ///
    /// Empty means "no map", which binds a 1x1 white stand-in so
    /// `shading * diffuse` is a no-op and the mesh is pure matcap -- the
    /// behaviour every mesh had before this existed.
    ///
    /// This is per MESH, not shared: a dressed character is a body skin plus a
    /// separate eye material, and one shared diffuse would paint the eyes with
    /// the body's skin.
    std::filesystem::path diffuse{};

    /// An already-decoded RGBA8 litsphere, used INSTEAD of `litsphere` when
    /// non-empty.
    ///
    /// This exists for `autoBlendSkin`: the skin tone is a blend of the three
    /// ethnic litspheres computed per character (`core::blendEthnicLitsphere`),
    /// so it has no file to name. Writing it to a temp file just to hand back a
    /// path would put disk I/O on the slider-drag path.
    ///
    /// Not owning: the span must outlive the `render()` call, like `mesh`.
    std::span<const uint8_t> litsphereRgba{};
    int litsphereWidth{};
    int litsphereHeight{};

    /// Tangent-space normal map. Empty means none, and the shader then uses the
    /// interpolated normal rather than sampling a flat placeholder -- the two
    /// are NOT equivalent, because unpacking a flat map renormalizes and the
    /// no-map path deliberately does not.
    ///
    /// This is what carries surface detail: pores, wrinkles and fine skin
    /// structure live here, not in the albedo.
    std::filesystem::path normalMap{};
    /// The reference's `normalmapIntensity` uniform, scaling the unpacked
    /// tangent-space vector.
    float normalMapIntensity{1.0F};

    /// The material declares itself transparent, so this mesh is drawn with
    /// alpha blending after the opaque ones.
    ///
    /// The shader has always written `outColor.a = diffuse.a`, and the pipeline
    /// has always thrown it away: `QRhiGraphicsPipeline`'s default target blend
    /// is disabled, so the alpha reached the framebuffer and was ignored. The
    /// shipped `brown.mhmat` is `transparent True` over an RGBA `brown_eye.png`
    /// (colour type 6, verified), and the GLB export already writes
    /// `alphaMode: BLEND` for it -- so the file and the screen disagreed about
    /// the same material.
    bool transparent{false};

    /// Ambient-occlusion map, multiplied over the shaded result. Empty means
    /// none. This is what darkens creases and contact areas -- the `.mhmat`
    /// `AoMap` channel.
    std::filesystem::path aoMap{};
};

/// The buffers, textures and pipeline for drawing meshes with the litsphere
/// shader.
///
/// Split out of the offscreen renderer so the interactive widget can use the
/// same code. QRhiWidget brings its own QRhi and render target, so anything
/// that binds to a specific device has to be constructible against one supplied
/// from outside — duplicating it instead is how the window and the offscreen
/// image quietly stop matching.
class SceneResources {
public:
    /// @param rhi      the device to build against; not owned.
    /// @param rp       the render pass these pipelines must be compatible with.
    /// @param sampleCount MSAA samples of the target, which the pipeline must
    ///                 agree with or creation fails.
    [[nodiscard]] static std::expected<std::unique_ptr<SceneResources>, RenderError> create(
        QRhi* rhi, QRhiRenderPassDescriptor* rp, const std::filesystem::path& shaderDir,
        int sampleCount);

    ~SceneResources();
    SceneResources(const SceneResources&)            = delete;
    SceneResources& operator=(const SceneResources&) = delete;

    /// Queues every mesh and its litsphere onto @p batch. Call once per change
    /// to the set of meshes, not per frame.
    ///
    /// Each mesh gets its own vertex/index buffers, its own litsphere texture
    /// and its own resource bindings; the pipeline, sampler and camera uniform
    /// are shared, since they are the same for the whole frame.
    [[nodiscard]] std::expected<void, RenderError> upload(QRhiResourceUpdateBatch* batch,
                                                          std::span<const MeshInstance> meshes);

    /// Queues the per-frame matrices. Cheap; call every frame.
    void updateCamera(QRhiResourceUpdateBatch* batch, const Camera& camera, float aspect);

    /// Records one draw per uploaded mesh. `upload` must have run in an earlier
    /// or the same batch.
    void draw(QRhiCommandBuffer* cb, const QSize& pixelSize);

private:
    SceneResources();
    struct Impl;
    std::unique_ptr<Impl> d_;
};

}  // namespace mh::render
