// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include "makehuman/core/Macro.h"
#include "makehuman/core/TargetIndex.h"
#include "makehuman/foundation/Geometry.h"

#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace mh::core {

/// One exportable shape key: a name a DCC can show, and a delta per RENDER
/// vertex.
///
/// Owns its deltas, unlike `foundation::MorphTarget`, which is a view. A caller
/// keeps the vector alive and hands the writer a span into it.
struct Blendshape {
    std::string name;
    std::vector<foundation::Vec3> deltas;
};

/// The expression units, blended across ethnicities, ready to export.
///
/// **Why this is not just "load every file".** `data/targets/expression/units/`
/// holds **102** `.target` files, but they are **34 units x 3 ethnicities**
/// (`african/`, `asian/`, `caucasian/`). Exporting 102 shape keys would give a
/// DCC three near-duplicate keys per expression, none of them the character's
/// actual shape. A character IS a blend of the three ethnicities, so the
/// correct set is 34, each one
///
///     delta = SUM over race of factors.value(race) * delta(race)
///
/// which is `targetWeight`'s ordinary rule (`humanmodifier.py:644-652`) applied
/// to a group whose only macro dependency is race. The three race weights sum
/// to 1, so the result is a convex combination and needs no renormalisation.
///
/// Group discovery is data-driven -- every `expression-units-*` group the index
/// found -- so a new unit shipped in `data/` appears without a code change, and
/// the count is asserted against `tests/golden/target_groups.txt` rather than
/// hardcoded here.
///
/// @param vmap `RenderMesh::vmap()`; deltas come back parallel to it, so a UV
///        seam's duplicated render vertices all move together.
/// @param meshVertexCount the BASE mesh's vertex count. Targets are indexed
///        against the base mesh, so a subdivided render mesh is not a valid
///        input -- and it fails loudly rather than quietly: a subdivided vmap
///        names vertices past the base count, which
///        `expandTargetToRenderVertices` rejects (Target.cpp:203), so the
///        result is empty rather than a set of wrong-vertex deltas.
/// @return one entry per unit, name stripped of the `expression-units-` prefix.
///         Empty when the index holds no expression units.
[[nodiscard]] std::vector<Blendshape> buildExpressionBlendshapes(const TargetIndex& index,
                                                                 const MacroFactors& factors,
                                                                 std::span<const uint32_t> vmap,
                                                                 size_t meshVertexCount);

}  // namespace mh::core
