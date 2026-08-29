// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include "makehuman/core/Mesh.h"
#include "makehuman/core/Types.h"

#include <cstdint>
#include <span>
#include <vector>

namespace mh::core {

/// GPU-ready view of a Mesh.
///
/// A Mesh stores positions and UVs in two independent index spaces: a face
/// corner is `(fvert[i], fuvs[i])` (module3d.py:627 vs :629). A GPU needs one
/// attribute per index, so the mesh must be **unwelded**: every distinct
/// `(vertex, uv)` pair becomes one render vertex.
///
/// This mirrors `updateIndexBufferVerts` / `updateIndexBufferFaces`
/// (module3d.py:815-863), including the ordering: pairs are packed into a
/// uint64 and sorted, so the render-vertex order matches the reference's
/// `np.unique` result exactly and is directly comparable against a fixture.
///
/// Note: the reference also filters by a per-face visibility mask before
/// building the index array (module3d.py:843-849). Face masking arrives with
/// proxy geometry hiding (M4); until something sets a mask there is nothing to
/// filter, so it is deliberately not modelled here yet.
class RenderMesh {
public:
    /// Build from a mesh. The mesh must already have faces set; normals and
    /// tangents are gathered only if the mesh has them.
    static RenderMesh build(const Mesh& mesh);

    [[nodiscard]] size_t vertexCount() const noexcept { return vmap_.size(); }

    [[nodiscard]] size_t indexCount() const noexcept { return index_.size(); }

    /// unwelded index -> Mesh::coord index
    [[nodiscard]] std::span<const uint32_t> vmap() const noexcept { return vmap_; }

    /// unwelded index -> Mesh::texco index
    [[nodiscard]] std::span<const uint32_t> tmap() const noexcept { return tmap_; }

    /// Gathered attribute streams, one entry per render vertex.
    [[nodiscard]] std::span<const Vec3> coord() const noexcept { return coord_; }

    [[nodiscard]] std::span<const Vec2> texco() const noexcept { return texco_; }

    [[nodiscard]] std::span<const Vec3> vnorm() const noexcept { return vnorm_; }

    [[nodiscard]] std::span<const Vec4> vtang() const noexcept { return vtang_; }

    /// Triangle indices into the render-vertex streams. Quads are split into
    /// two triangles (0,1,2) and (0,2,3) — Metal and every modern API dropped
    /// GL_QUADS, which the reference still submits (glmodule.py:66).
    [[nodiscard]] std::span<const uint32_t> index() const noexcept { return index_; }

    /// Per face group: {first index, index count} into index(). Faces are
    /// sorted by group so each group is one contiguous draw range.
    struct GroupRange {
        uint32_t first{};
        uint32_t count{};
    };

    [[nodiscard]] std::span<const GroupRange> groupRanges() const noexcept { return groupRanges_; }

    /// Re-gathers positions (and normals/tangents when present) after the mesh
    /// has been morphed, without rebuilding the topology. This is the hot path:
    /// the unweld table only changes when faces change, which morphing never does.
    ///
    /// Does nothing if @p mesh no longer has the vertex and face counts this
    /// table was built from -- the mapping would be stale, and indexing through
    /// it would read out of bounds. Rebuild with build() instead.
    void refreshPositions(const Mesh& mesh);

    /// True when this table still matches @p mesh's topology. Exact, not a
    /// heuristic: a same-size topology swap bumps the mesh's topology version.
    [[nodiscard]] bool matches(const Mesh& mesh) const noexcept {
        return builtVertexCount_ == mesh.vertexCount() &&
               builtTopologyVersion_ == mesh.topologyVersion();
    }

private:
    std::vector<uint32_t> vmap_;
    std::vector<uint32_t> tmap_;

    std::vector<Vec3> coord_;
    std::vector<Vec2> texco_;
    std::vector<Vec3> vnorm_;
    std::vector<Vec4> vtang_;

    std::vector<uint32_t> index_;
    std::vector<GroupRange> groupRanges_;

    // Topology fingerprint, so a stale table is detected in O(1).
    size_t builtVertexCount_{};
    uint64_t builtTopologyVersion_{};
};

}  // namespace mh::core
