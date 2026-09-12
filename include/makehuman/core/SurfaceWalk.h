// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Walking a mesh surface, instead of raycasting at it.
//
// The hair-style attempt of 2026-09-11 placed roots by casting a ray from the
// head centre onto the scalp. The scalp is not a closed dome: mapped over a
// direction grid the rays miss straight up, dead front and dead back, where the
// body carries seams. The nearest-direction FALLBACK for a missed ray looked
// robust and was the trap -- it silently collapses every miss onto the scalp
// rim, so a wide sweep piles up at the front edge instead of spreading. Two
// rounds of retuning changed nothing, because no parameter was wrong.
//
// `memory/todo.md` records the conclusion: placement must WALK THE SURFACE, not
// raycast from a centre. This is that, and only that -- the styles are a
// separate chunk that needs this to exist first.
#pragma once

#include <cstdint>
#include <span>
#include <vector>

namespace mh::core {

class Mesh;

/// Distance from @p sources to every vertex, measured along mesh EDGES.
///
/// Dijkstra over the edge graph, restricted to @p region: an edge is walkable
/// only when BOTH of its ends are in the region, so the region is a wall the
/// walk cannot cross. That is what keeps a scalp walk off the face without any
/// geometric test at all.
///
/// **Edge distance, not true geodesic distance.** A path is forced through
/// vertices rather than across face interiors, which overestimates by a few per
/// cent on a regular mesh. That is the right trade here: placement wants
/// "spread out over the surface, never leaving it", which this gives exactly,
/// and an exact solver is a great deal of code for a tighter number nothing
/// needs. Revisit if something ever needs the distance itself to be accurate
/// rather than ordered.
///
/// A source outside @p region is ignored rather than seeding the walk: it would
/// otherwise put a zero into a region the caller deliberately excluded, and
/// every distance downstream would be measured from the wrong place.
///
/// @return one distance per MESH vertex -- indexed by vertex id, not by
///         position within the region -- infinity where unreachable.
[[nodiscard]] std::vector<float> surfaceDistance(const Mesh& mesh, std::span<const uint32_t> region,
                                                 std::span<const uint32_t> sources);

/// @p count vertices of @p region, spread as far apart over the surface as the
/// region allows.
///
/// Farthest-point sampling: start at the region's first vertex, then repeatedly
/// take whichever vertex is furthest (by `surfaceDistance`) from everything
/// picked so far. The result is even coverage with no direction grid, no rays
/// and so no misses to fall back from.
///
/// Asking for more than the region holds returns the whole region, once each --
/// a caller wanting eighty roots on a patch of nine vertices gets nine, not
/// nine plus seventy-one repeats.
[[nodiscard]] std::vector<uint32_t> spreadOverSurface(const Mesh& mesh,
                                                      std::span<const uint32_t> region,
                                                      size_t count);

/// The chain of vertices from @p source to @p target, walking mesh edges inside
/// @p region.
///
/// `surfaceDistance` says how far the nape is from the hairline; this says
/// which vertices lie between them, which is what routing a parting or a
/// cornrow actually needs.
///
/// @return source first, target last, each consecutive pair joined by an edge.
///         Empty when either end is outside @p region or no route exists --
///         empty rather than truncated, so a partial route cannot be mistaken
///         for a whole one.
[[nodiscard]] std::vector<uint32_t> pathOverSurface(const Mesh& mesh,
                                                    std::span<const uint32_t> region,
                                                    uint32_t source, uint32_t target);

}  // namespace mh::core
