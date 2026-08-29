// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "makehuman/foundation/Geometry.h"
#include "makehuman/foundation/Types.h"

#include <QMatrix4x4>
#include <QSize>

#include <expected>
#include <filesystem>
#include <memory>
#include <string>

class QRhi;
class QRhiCommandBuffer;
class QRhiRenderPassDescriptor;
class QRhiResourceUpdateBatch;

namespace mh::render {

/// How the camera is placed. The MODEL rotates and the camera stays put, which
/// is the reference's convention (`glmodule.py`) and is what makes the
/// litsphere's fixed eye-space lighting look right.
struct Camera {
    float fovY{30.0F};
    float distance{45.0F};  ///< in mesh units; the base mesh is ~17 dm tall
    float yawDegrees{0.0F};
    float pitchDegrees{0.0F};
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

/// The buffers, textures and pipeline for drawing one mesh with the litsphere
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

    /// Queues the mesh and litsphere onto @p batch. Call once per mesh change.
    [[nodiscard]] std::expected<void, RenderError> upload(QRhiResourceUpdateBatch* batch,
                                                          const foundation::RenderView& mesh,
                                                          const std::filesystem::path& litsphere);

    /// Queues the per-frame matrices. Cheap; call every frame.
    void updateCamera(QRhiResourceUpdateBatch* batch, const Camera& camera, float aspect);

    /// Records the draw. `upload` must have run in an earlier or the same batch.
    void draw(QRhiCommandBuffer* cb, const QSize& pixelSize);

private:
    SceneResources();
    struct Impl;
    std::unique_ptr<Impl> d_;
};

}  // namespace mh::render
