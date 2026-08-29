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

    // 4. Faces sorted by group, so every group is one contiguous draw range.
    //    Stable, so face order within a group is preserved (module3d.py:847-849).
    std::vector<uint32_t> faceOrder(nFaces);
    std::iota(faceOrder.begin(), faceOrder.end(), 0U);
    const auto groups = mesh.group();
    std::ranges::stable_sort(faceOrder,
                             [&](uint32_t a, uint32_t b) { return groups[a] < groups[b]; });

    // 5. Triangulate. A quad becomes (0,1,2) + (0,2,3); a triangle stored as a
    //    degenerate quad (corner 3 == corner 0) contributes only its first
    //    triangle, since the second would be degenerate.
    const size_t trisPerFace = (vpp >= 4) ? 2 : 1;
    rm.index_.reserve(nFaces * trisPerFace * 3);

    const size_t nGroups = mesh.faceGroups().empty() ? 1 : mesh.faceGroups().size();
    rm.groupRanges_.assign(nGroups, GroupRange{});

    for (const uint32_t f : faceOrder) {
        const size_t base = static_cast<size_t>(f) * vpp;
        const uint16_t g  = groups[f];

        const uint32_t before = static_cast<uint32_t>(rm.index_.size());

        const uint32_t c0 = cornerToRender[base + 0];
        const uint32_t c1 = cornerToRender[base + 1];
        const uint32_t c2 = cornerToRender[base + 2];
        rm.index_.insert(rm.index_.end(), {c0, c1, c2});

        if (vpp >= 4) {
            const uint32_t c3 = cornerToRender[base + 3];
            // Skip the second triangle of a degenerate quad (a stored triangle).
            if (mesh.fvert()[base + 3] != mesh.fvert()[base + 0]) {
                rm.index_.insert(rm.index_.end(), {c0, c2, c3});
            }
        }

        const uint32_t added = static_cast<uint32_t>(rm.index_.size()) - before;
        if (g < rm.groupRanges_.size()) {
            if (rm.groupRanges_[g].count == 0) rm.groupRanges_[g].first = before;
            rm.groupRanges_[g].count += added;
        }
    }

    // 6. Gather the attribute streams.
    rm.texco_.resize(rm.vmap_.size());
    if (hasUV) {
        for (size_t j = 0; j < rm.vmap_.size(); ++j)
            rm.texco_[j] = mesh.texco()[rm.tmap_[j]];
    }
    rm.refreshPositions(mesh);
    return rm;
}

void RenderMesh::refreshPositions(const Mesh& mesh) {
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
