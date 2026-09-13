// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Binding an authored point to the body, which is the inverse of `fitProxy`.
//
// The afro shipped with IDENTITY binding: every proxy vertex sat on a base
// vertex and carried an offset. That is enough for a shell, and it is not
// enough for cornrows. MEASURED on the shipped base mesh: the hair-bearing
// scalp is 1.504 dm wide with a median edge of 0.1436 dm -- about TEN vertices
// across. Cornrows need ridge, gap, ridge, so identity binding allows at most
// five rows each exactly one vertex wide, which renders as a corrugated cap
// rather than as braids. The mesh cannot resolve them.
//
// `memory/todo.md` records the way out, and it is a capability rather than a
// parameter: "authored geometry that is NOT in the base mesh can be bound to
// the scalp by three base vertices, barycentric weights and an offset". This
// is that binder.
#pragma once

#include "makehuman/foundation/Types.h"

#include <array>
#include <cstdint>
#include <optional>
#include <span>

namespace mh::core {

class Mesh;

/// Where an authored point attaches to the body.
///
/// Exactly the shape `fitProxy` consumes, so a bound point round-trips: fitting
/// a `SurfaceBinding` on the same body reproduces the point it was bound from.
struct SurfaceBinding {
    std::array<uint32_t, 3> refVerts{};
    std::array<float, 3> weights{};
    foundation::Vec3 offset{};
};

/// Bind @p point to the nearest triangle of @p region.
///
/// The base mesh is quads, so each face is split on its first corner; a
/// barycentric weight is only meaningful on a triangle.
///
/// The weights come from the CLOSEST POINT on that triangle, and the offset is
/// whatever is left over -- so a point sitting on the surface gets a zero
/// offset, and a point standing off it keeps its standoff. That split is what
/// makes hair follow a morphed body: the weights ride the shape, the offset
/// does not.
///
/// @return nothing when @p region names no usable triangle, rather than
///         binding to an arbitrary vertex.
[[nodiscard]] std::optional<SurfaceBinding> bindToSurface(const Mesh& mesh,
                                                          std::span<const uint32_t> region,
                                                          foundation::Vec3 point);

}  // namespace mh::core
