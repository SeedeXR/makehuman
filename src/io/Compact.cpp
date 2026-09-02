// SPDX-License-Identifier: Apache-2.0
#include "makehuman/io/Compact.h"

#include <algorithm>

namespace mh::io {

CompactedMesh compactUnusedVertices(const foundation::RenderView& mesh) {
    CompactedMesh out;
    const size_t n = mesh.vertexCount();
    out.remap.assign(n, CompactedMesh::kDropped);

    // Two passes rather than one: the first decides which vertices survive so
    // the second can assign new indices in ascending order. Numbering them in
    // first-use order instead would shuffle the buffer for no gain and make a
    // diff against the input unreadable.
    for (const uint32_t v : mesh.index) {
        if (v < n) out.remap[v] = 0U;
    }
    uint32_t next = 0;
    for (size_t v = 0; v < n; ++v) {
        if (out.remap[v] == CompactedMesh::kDropped) continue;
        out.remap[v] = next++;
    }

    const size_t kept = next;
    out.coord.resize(kept);
    if (!mesh.texco.empty()) out.texco.resize(kept);
    if (!mesh.vnorm.empty()) out.vnorm.resize(kept);
    if (!mesh.vtang.empty()) out.vtang.resize(kept);
    for (size_t v = 0; v < n; ++v) {
        const uint32_t to = out.remap[v];
        if (to == CompactedMesh::kDropped) continue;
        out.coord[to] = mesh.coord[v];
        if (!out.texco.empty()) out.texco[to] = mesh.texco[v];
        if (!out.vnorm.empty()) out.vnorm[to] = mesh.vnorm[v];
        if (!out.vtang.empty()) out.vtang[to] = mesh.vtang[v];
    }

    out.index.reserve(mesh.index.size());
    for (const uint32_t v : mesh.index) {
        // An out-of-range index cannot be remapped, and dropping the triangle
        // silently would be worse than keeping a bad index the writer's own
        // validation will reject.
        out.index.push_back(v < n ? out.remap[v] : v);
    }
    return out;
}

std::pair<std::vector<uint32_t>, std::vector<float>> compactSkinAttributes(
    const foundation::SkinView& skin, std::span<const uint32_t> remap, size_t newVertexCount) {
    const size_t infl = skin.influences;
    std::vector<uint32_t> joints(newVertexCount * infl, 0U);
    std::vector<float> weights(newVertexCount * infl, 0.0F);
    for (size_t v = 0; v < remap.size(); ++v) {
        const uint32_t to = remap[v];
        if (to == CompactedMesh::kDropped) continue;
        for (size_t i = 0; i < infl; ++i) {
            joints[to * infl + i]  = skin.joints[v * infl + i];
            weights[to * infl + i] = skin.weights[v * infl + i];
        }
    }
    return {std::move(joints), std::move(weights)};
}

std::vector<foundation::Vec3> compactDeltas(std::span<const foundation::Vec3> deltas,
                                            std::span<const uint32_t> remap,
                                            size_t newVertexCount) {
    std::vector<foundation::Vec3> out(newVertexCount, foundation::Vec3{});
    const size_t n = std::min(deltas.size(), remap.size());
    for (size_t old = 0; old < n; ++old) {
        const uint32_t nu = remap[old];
        if (nu == CompactedMesh::kDropped) continue;
        if (nu < newVertexCount) out[nu] = deltas[old];
    }
    return out;
}

}  // namespace mh::io
