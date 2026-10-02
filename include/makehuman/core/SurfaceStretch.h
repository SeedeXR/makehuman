// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Per-vertex stretch: how much the surface around a vertex has grown or shrunk
// against its rest shape.
//
// This is the signal a TENSION MAP needs, and directive 12.3 named it before it
// existed -- "future masks: muscle flex, tension-driven shading" sit alongside
// geometry correctives and wrinkle blending as consumers of the pose. The
// others read the RBF weight vector; this one reads the deformed mesh, because
// stretch is a property of the surface rather than of the pose that caused it.
// A corrective, a hand-authored pose and a retargeted animation all produce it
// the same way, which a pose-weight signal could not.
#pragma once

#include "makehuman/foundation/Types.h"

#include <cstdint>
#include <span>
#include <vector>

namespace mh::core {

class Mesh;

/// Per-vertex stretch of @p posed against @p rest, one value per vertex.
///
/// **1.0 is unchanged**, below 1 is compressed, above 1 is stretched. The scale
/// is a LENGTH ratio, not an area one: the mean over a vertex's incident edges
/// of `|posed edge| / |rest edge|`. Length rather than area because the two
/// disagree on a shear -- a quad sheared to a rhombus keeps its area while its
/// diagonals change by a third -- and a crease is a shear.
///
/// @param rest   the mesh whose faces give the adjacency, and whose coordinates
///               are the reference length.
/// @param posed  one position per vertex of @p rest, in the same order.
///
/// Returns empty if @p posed is not one position per vertex: a tension buffer
/// of the wrong length would be read past its end by the interleaver, and a
/// silently short one would tension the wrong vertices.
///
/// A vertex with no incident edges -- which the base mesh has, since helper
/// cages carry loose geometry -- reads exactly 1.0 rather than 0. Zero would
/// read as total compression and light that vertex as if it were a deep crease.
[[nodiscard]] std::vector<float> surfaceStretch(const Mesh& rest,
                                                std::span<const foundation::Vec3> posed);

}  // namespace mh::core
