// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "makehuman/foundation/Geometry.h"
#include "makehuman/render/SceneResources.h"

#include <QImage>

#include <expected>
#include <filesystem>
#include <memory>
#include <span>

namespace mh::render {

/// Renders a mesh to an image with no window.
///
/// Offscreen exists so the renderer is testable: a widget cannot be checked in
/// CI, but an image's pixels can be measured, so "it draws" becomes provable
/// rather than claimed after glancing at a screenshot. It shares
/// SceneResources with the interactive viewport, so the two cannot drift.
///
/// This module is Apache-2.0: setting up RHI is our own code written against
/// Qt's API. The *shaders* it loads are AGPL ports living in `resources/`, and
/// loading a file at runtime is not derivation (LICENSING.md 4).

struct RenderSettings {
    int width{512};
    int height{512};
    Camera camera;
    foundation::Vec3 background{0.098F, 0.098F, 0.106F};  ///< design.md surface-0

    /// Clear the background to alpha 0 instead of opaque.
    ///
    /// This is what a PRODUCTION render needs: a character on transparency,
    /// ready to composite. The readback is already RGBA8888, so the alpha
    /// channel was always carried -- it was simply always 1, because the clear
    /// hard-coded it.
    ///
    /// The body still comes out opaque: the shader writes `outColor.a` from the
    /// diffuse map's alpha, and the no-map stand-in is opaque white.
    ///
    /// Default false, so every existing render is unchanged.
    bool transparentBackground{false};

    std::filesystem::path litsphere;

    /// Which shader shades the frame. Defaults to the litsphere so every
    /// existing render -- including the golden parity images -- is unchanged.
    ShadingModel shading{ShadingModel::Litsphere};
};

class OffscreenRenderer {
public:
    [[nodiscard]] static std::expected<std::unique_ptr<OffscreenRenderer>, RenderError> create(
        const std::filesystem::path& shaderDir);

    ~OffscreenRenderer();
    OffscreenRenderer(const OffscreenRenderer&)            = delete;
    OffscreenRenderer& operator=(const OffscreenRenderer&) = delete;

    /// Draws every mesh into one image, RGBA8, top-down. This is what a dressed
    /// character is: the body plus each worn proxy, each with its own material.
    [[nodiscard]] std::expected<QImage, RenderError> render(std::span<const MeshInstance> meshes,
                                                            const RenderSettings& settings);

    /// One mesh shaded by `settings.litsphere`.
    [[nodiscard]] std::expected<QImage, RenderError> render(const foundation::RenderView& mesh,
                                                            const RenderSettings& settings);

    [[nodiscard]] std::string backendName() const;

private:
    OffscreenRenderer();
    struct Impl;
    std::unique_ptr<Impl> d_;
};

}  // namespace mh::render
