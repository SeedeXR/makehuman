// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "makehuman/foundation/Geometry.h"
#include "makehuman/foundation/Types.h"

#include <QImage>

#include <expected>
#include <filesystem>
#include <memory>
#include <string>

namespace mh::render {

/// Renders a mesh to an image with no window, using Qt RHI on Metal.
///
/// Offscreen first, deliberately. A widget cannot be checked in a test or in
/// CI, but an offscreen render produces a QImage whose pixels can be measured --
/// so "it draws" becomes something provable rather than something claimed after
/// looking at a screenshot. The same device, pipeline and buffers back the
/// interactive viewport later.
///
/// This module is Apache-2.0: setting up RHI is our own code written against
/// Qt's API. The *shaders* it loads are AGPL ports of the reference's GLSL and
/// live in `resources/shaders/rhi/` -- loading a file at runtime is not
/// derivation, so the boundary holds (LICENSING.md 4).

struct RenderSettings {
    int width{512};
    int height{512};

    /// Vertical field of view, degrees.
    float fovY{30.0F};
    /// Camera distance in mesh units. The base mesh is ~17 dm tall.
    float distance{45.0F};
    /// Degrees; the model rotates, the camera does not -- matching the
    /// reference's convention (`glmodule.py`).
    float yawDegrees{0.0F};
    float pitchDegrees{0.0F};

    foundation::Vec3 background{0.098F, 0.098F, 0.106F};  ///< design.md surface-0

    /// A litsphere/matcap. Without one the shader has nothing to shade with.
    std::filesystem::path litsphere;
};

enum class RenderErrorKind {
    NoDevice,       ///< no Metal device, or RHI refused to start
    ShaderMissing,  ///< a .qsb was not found or would not load
    TextureMissing,
    EmptyMesh,
    Failed,
};

struct RenderError {
    RenderErrorKind kind{};
    std::string detail;

    [[nodiscard]] std::string message() const;
};

class OffscreenRenderer {
public:
    /// @param shaderDir where the compiled `.qsb` files live.
    [[nodiscard]] static std::expected<std::unique_ptr<OffscreenRenderer>, RenderError> create(
        const std::filesystem::path& shaderDir);

    ~OffscreenRenderer();
    OffscreenRenderer(const OffscreenRenderer&)            = delete;
    OffscreenRenderer& operator=(const OffscreenRenderer&) = delete;

    /// Draws @p mesh and returns the result. The image is RGBA8, top-down.
    [[nodiscard]] std::expected<QImage, RenderError> render(const foundation::RenderView& mesh,
                                                            const RenderSettings& settings);

    /// The RHI backend actually in use, for reporting.
    [[nodiscard]] std::string backendName() const;

private:
    OffscreenRenderer();
    struct Impl;
    std::unique_ptr<Impl> d_;
};

}  // namespace mh::render
