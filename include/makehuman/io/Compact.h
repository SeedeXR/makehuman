// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "makehuman/foundation/Geometry.h"

#include <cstdint>
#include <span>
#include <vector>

namespace mh::io {

/// A render mesh with every vertex no triangle references removed, plus the
/// per-vertex arrays that have to move with it.
///
/// **Why this exists, measured.** `RenderMesh::setFaceMask` filters the INDEX
/// buffer and deliberately leaves the vertex buffer alone -- the renderer
/// uploads it once and toggling a mask must stay cheap. Export inherited that:
/// a default character's GLB carried **21,833 body vertices of which 14,517
/// were referenced**, so **7,316 -- 33.5% -- of every position, normal, UV,
/// tangent, joint and weight written was dead weight.
///
/// It is not only size. A consumer that bounds the vertex buffer sees the
/// hidden helper cages: Blender reads the exported body as **1.6940 m** where
/// the visible mesh is **1.6594 m**, a 2% error inherited by anything that
/// frames or scales by bounds.
///
/// Deliberately NOT done inside the writers. A writer renumbering vertices
/// behind a caller's back would break anyone round-tripping indices, and the
/// tests that assert `result.vertices == view.vertexCount()` are that caller.
/// This is an explicit step an exporter takes.
struct CompactedMesh {
    static constexpr uint32_t kDropped = ~0U;

    std::vector<foundation::Vec3> coord;
    std::vector<foundation::Vec2> texco;
    std::vector<foundation::Vec3> vnorm;
    std::vector<foundation::Vec4> vtang;
    std::vector<uint32_t> index;

    /// Old vertex index -> new, or `kDropped`. Kept so a caller can move its
    /// own per-vertex arrays -- a skin's joints and weights, a morph's deltas --
    /// without repeating the reachability walk.
    std::vector<uint32_t> remap;

    /// How many vertices were dropped. Zero means the input was already tight.
    [[nodiscard]] size_t dropped() const noexcept { return remap.size() - coord.size(); }

    [[nodiscard]] foundation::RenderView view() const {
        return foundation::RenderView{coord, texco, vnorm, vtang, index};
    }
};

/// Drops the vertices @p mesh's triangles never name.
///
/// Order is preserved: surviving vertices keep their relative order, so a
/// diff against the input is a deletion rather than a shuffle.
[[nodiscard]] CompactedMesh compactUnusedVertices(const foundation::RenderView& mesh);

/// Applies @p remap to a skin's per-vertex arrays, leaving the joint hierarchy
/// alone.
///
/// @param joints  `influences` entries per OLD vertex.
/// @param weights the same, parallel.
/// @return joints and weights for the surviving vertices, in the new order.
[[nodiscard]] std::pair<std::vector<uint32_t>, std::vector<float>> compactSkinAttributes(
    const foundation::SkinView& skin, std::span<const uint32_t> remap, size_t newVertexCount);

/// Applies @p remap to a morph target's per-render-vertex deltas.
///
/// Same remap, same reason as `compactSkinAttributes`: a delta array that is
/// not renumbered alongside the vertex buffer moves the wrong vertices from the
/// first dropped index onward. Written only once the app actually exported
/// blendshapes -- until then it would have been another capability with no
/// caller.
///
/// @param deltas one entry per OLD render vertex.
[[nodiscard]] std::vector<foundation::Vec3> compactDeltas(std::span<const foundation::Vec3> deltas,
                                                          std::span<const uint32_t> remap,
                                                          size_t newVertexCount);

}  // namespace mh::io
