// SPDX-License-Identifier: Apache-2.0
//
// Written from the published formats, not translated from the AGPL reference.
// See LICENSING.md section 4.
#pragma once

#include "makehuman/foundation/Geometry.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <string_view>

namespace mh::io {

/// Length units an export can be written in.
///
/// MakeHuman's internal unit is the **decimetre**
/// (legacy/python/apps/human.py:694-699 computes `heightCm = 10 * bboxY`), and
/// the factors are the ones its export UI offers
/// (apps/gui/guiexport.py:124-129).
enum class Unit : uint8_t { Decimeter, Meter, Centimeter, Inch };

[[nodiscard]] float unitScale(Unit u) noexcept;
[[nodiscard]] std::string_view unitName(Unit u) noexcept;

/// The two numbers every writer derives from its options, together.
///
/// They were computed independently in five places as the same eight lines.
/// Keeping them apart is how a writer ends up scaling its mesh and forgetting
/// its skeleton, or levelling by an offset it took before scaling.
struct Transform {
    float scale{1.0F};
    /// Added to y AFTER scaling, so the lowest point lands at zero.
    float groundOffset{0.0F};

    [[nodiscard]] constexpr float placedY(float y) const noexcept {
        return y * scale + groundOffset;
    }

    [[nodiscard]] constexpr foundation::Vec3 place(const foundation::Vec3& v) const noexcept {
        return {v.x * scale, placedY(v.y), v.z * scale};
    }
};

/// The transform for a whole scene: ONE ground offset shared by every entry.
///
/// The shared offset is the point. Levelling each mesh on its own drops the
/// clothes to the floor beside the body.
///
/// A template because each writer has its own entry type -- `GltfSceneEntry`,
/// `SceneEntry`, `UsdSceneEntry` -- and they agree only on having a `.mesh`.
/// Converting them all to a common span would allocate on every export to
/// satisfy a signature.
///
/// **The scale is applied before the minimum is taken.** Offsetting by the
/// unscaled minimum leaves the model below the floor by (scale-1)x its depth,
/// and looks correct at the default scale of 1.
template <typename Entries>
[[nodiscard]] Transform sceneTransform(float scale, bool feetOnGround, const Entries& entries) {
    Transform t{scale, 0.0F};
    if (!feetOnGround) return t;

    float lowest = std::numeric_limits<float>::infinity();
    for (const auto& e : entries) {
        for (const foundation::Vec3& v : e.mesh.coord) {
            lowest = std::min(lowest, v.y * scale);
        }
    }
    // Still infinite means no vertices at all. Negating it would put every
    // coordinate at -inf and write a file of NaNs rather than an empty one.
    if (std::isfinite(lowest)) t.groundOffset = -lowest;
    return t;
}

/// The same, for the writers' single-mesh entry points.
[[nodiscard]] Transform meshTransform(float scale, bool feetOnGround,
                                      const foundation::RenderView& mesh);

}  // namespace mh::io
