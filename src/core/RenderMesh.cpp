// SPDX-License-Identifier: AGPL-3.0-or-later
#include "makehuman/core/RenderMesh.h"

#include <algorithm>
#include <numeric>

namespace mh::core {
namespace {

/// Packs a (vertex, uv) corner into one sortable key, exactly as
/// module3d.py:816-818 does before calling np.unique.
constexpr uint64_t packCorner(uint32_t v, uint32_t t) noexcept {
    return (static_cast<uint64_t>(v) << 32) | static_cast<uint64_t>(t);
}

}  // namespace

RenderMesh RenderMesh::build(const Mesh& mesh) {
    RenderMesh rm;

    const size_t vpp     = mesh.vertsPerPrimitive();
    const size_t nFaces  = mesh.faceCount();
    const size_t corners = nFaces * vpp;
    if (corners == 0) return rm;

    const bool hasUV = mesh.hasUV();

    // 1. One key per corner.
    std::vector<uint64_t> keys(corners);
    for (size_t i = 0; i < corners; ++i) {
        keys[i] = packCorner(mesh.fvert()[i], hasUV ? mesh.fuvs()[i] : 0U);
    }

    // 2. One sort of the corner order, then a single sweep, rather than
    //    sort-unique followed by a binary search per corner. Same result and
    //    same ordering as the reference's np.unique, but O(n log n) once
    //    instead of O(n log n) plus n log u lookups.
    std::vector<uint32_t> order(corners);
    std::iota(order.begin(), order.end(), 0U);
    std::ranges::sort(order, [&](uint32_t a, uint32_t b) { return keys[a] < keys[b]; });

    std::vector<uint32_t> cornerToRender(corners);
    rm.vmap_.reserve(corners);
    rm.tmap_.reserve(corners);

    uint64_t prevKey = 0;
    bool first       = true;
    for (const uint32_t c : order) {
        const uint64_t k = keys[c];
        if (first || k != prevKey) {
            rm.vmap_.push_back(static_cast<uint32_t>(k >> 32));
            rm.tmap_.push_back(static_cast<uint32_t>(k & 0xFFFFFFFFULL));
            prevKey = k;
            first   = false;
        }
        cornerToRender[c] = static_cast<uint32_t>(rm.vmap_.size() - 1);
    }
    rm.vmap_.shrink_to_fit();
    rm.tmap_.shrink_to_fit();

    // 3. Faces sorted by group, so every group is one contiguous draw range.
    //    Stable, so face order within a group is preserved (module3d.py:847-849).
    std::vector<uint32_t> faceOrder(nFaces);
    std::iota(faceOrder.begin(), faceOrder.end(), 0U);
    const auto groups = mesh.group();
    std::ranges::stable_sort(faceOrder,
                             [&](uint32_t a, uint32_t b) { return groups[a] < groups[b]; });

    // 4. Fan-triangulate: (0,1,2), (0,2,3), ... for any corner count. A quad
    //    gives the usual two triangles; a triangle stored as a degenerate quad
    //    (corner 3 == corner 0) gives one, because the second is degenerate.
    //    Metal and every modern API dropped GL_QUADS, which the reference still
    //    submits (glmodule.py:66).
    rm.index_.reserve(nFaces * (vpp - 2) * 3);

    // Sized from the largest group id actually used, as the reference does
    // (module3d.py:857). Sizing from faceGroups().size() would leave the
    // indices of any face with a larger id unreachable from every draw range.
    uint16_t maxGroup = 0;
    for (const uint16_t g : groups)
        maxGroup = std::max(maxGroup, g);
    rm.groupRanges_.assign(static_cast<size_t>(maxGroup) + 1U, GroupRange{});

    for (const uint32_t f : faceOrder) {
        const size_t base = static_cast<size_t>(f) * vpp;
        const uint16_t g  = groups[f];

        const uint32_t before = static_cast<uint32_t>(rm.index_.size());

        for (size_t c = 2; c < vpp; ++c) {
            const uint32_t v0 = mesh.fvert()[base + 0];
            const uint32_t v1 = mesh.fvert()[base + c - 1];
            const uint32_t v2 = mesh.fvert()[base + c];
            if (v0 == v1 || v1 == v2 || v0 == v2) continue;  // zero-area

            rm.index_.insert(
                rm.index_.end(),
                {cornerToRender[base + 0], cornerToRender[base + c - 1], cornerToRender[base + c]});
        }

        const uint32_t added = static_cast<uint32_t>(rm.index_.size()) - before;
        if (rm.groupRanges_[g].count == 0) rm.groupRanges_[g].first = before;
        rm.groupRanges_[g].count += added;
    }

    // 5. Gather the attribute streams.
    if (hasUV) {
        rm.texco_.resize(rm.vmap_.size());
        for (size_t j = 0; j < rm.vmap_.size(); ++j)
            rm.texco_[j] = mesh.texco()[rm.tmap_[j]];
    } else {
        // tmap_ is documented as an index into texco(); with no UVs it would be
        // an all-zero index into an empty array, so clear it rather than lie.
        rm.tmap_.clear();
    }
    rm.builtVertexCount_     = mesh.vertexCount();
    rm.builtTopologyVersion_ = mesh.topologyVersion();
    rm.refreshPositions(mesh);
    return rm;
}

void RenderMesh::refreshPositions(const Mesh& mesh) {
    // vmap_/tmap_ were validated against the mesh as it was at build time. If
    // its topology changed, every index here may be stale and gathering
    // through them would read out of bounds.
    if (!matches(mesh)) return;

    const size_t n = vmap_.size();

    coord_.resize(n);
    for (size_t j = 0; j < n; ++j)
        coord_[j] = mesh.coord()[vmap_[j]];

    if (mesh.vnorm().size() == mesh.vertexCount()) {
        vnorm_.resize(n);
        for (size_t j = 0; j < n; ++j)
            vnorm_[j] = mesh.vnorm()[vmap_[j]];
    } else {
        vnorm_.clear();
    }

    if (mesh.vtang().size() == mesh.vertexCount()) {
        vtang_.resize(n);
        for (size_t j = 0; j < n; ++j)
            vtang_[j] = mesh.vtang()[vmap_[j]];
    } else {
        vtang_.clear();
    }
}

}  // namespace mh::core
