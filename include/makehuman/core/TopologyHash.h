// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include "makehuman/core/Mesh.h"

#include <cstdint>

namespace mh::core {

/// The identity of a mesh's TOPOLOGY, as one 64-bit number.
///
/// Owner directive 12.7 asks for a "topology hash guard, so a base mesh edit
/// fails loudly instead of silently corrupting every delta". Correctives, skin
/// weights and blend-shape deltas are all indexed by VERTEX; they are only
/// meaningful against the topology they were authored on. This is what an
/// export stamps so a consumer can tell.
///
/// **Positions are deliberately excluded.** Every character IS the base mesh
/// with different positions, so a hash that moved with the sliders would mark
/// every body incompatible with every other. What goes in is what a delta or a
/// face mask is expressed in terms of: the counts, the vertex and UV index
/// arrays, the per-face group ids, and the group NAMES -- `staticFaceMask`
/// hides groups by name, so a rename changes which geometry is visible without
/// touching an index.
///
/// FNV-1a, written out rather than taken from `std::hash`: the value is
/// written into files, so it has to be the same number on every platform and
/// every standard library, and `std::hash` guarantees neither.
[[nodiscard]] uint64_t topologyHash(const Mesh& mesh);

}  // namespace mh::core
