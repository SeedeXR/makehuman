// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "makehuman/foundation/Geometry.h"
#include "makehuman/render/SceneResources.h"

#include <QImage>

#include <expected>
#include <filesystem>
#include <memory>

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
    std::filesystem::path litsphere;
};

class OffscreenRenderer {
public:
    [[nodiscard]] static std::expected<std::unique_ptr<OffscreenRenderer>, RenderError> create(
        const std::filesystem::path& shaderDir);

    ~OffscreenRenderer();
    OffscreenRenderer(const OffscreenRenderer&)            = delete;
    OffscreenRenderer& operator=(const OffscreenRenderer&) = delete;

    /// Draws @p mesh and returns the result, RGBA8, top-down.
    [[nodiscard]] std::expected<QImage, RenderError> render(const foundation::RenderView& mesh,
                                                            const RenderSettings& settings);

    [[nodiscard]] std::string backendName() const;

private:
    OffscreenRenderer();
    struct Impl;
    std::unique_ptr<Impl> d_;
};

}  // namespace mh::render
