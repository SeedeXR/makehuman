// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include "makehuman/core/Mesh.h"

#include <cstdint>
#include <expected>
#include <vector>

namespace mh::core {

/// One level of Catmull-Clark subdivision, split into a topology pass and a
/// geometry pass — mirroring the reference's `create()` / `update_coords()`
/// (legacy-python/apps/catmull_clark_subdivision.py:75 and :384).
///
/// The split matters: topology only changes when the face set does, while
/// geometry changes on every morph. Rebuilding topology per morph is what makes
/// the reference cost 202 ms.
///
/// Vertex layout of the result, three contiguous blocks (`:130-131`):
/// ```
///   [0,      cbase)  base vertices,  one per parent vertex
///   [cbase,  ebase)  face points,    one per parent face
///   [ebase,  end  )  edge points,    one per unique parent edge
/// ```
/// Each parent quad becomes four quads laid out `v_k, e_k, c, e_{k-1}` (`:172-203`).
///
/// **Scope:** quad meshes only, and no face masking. The reference threads a
/// `staticFaceMask` through `vtx_map`/`face_map`/`uv_map` so hidden geometry is
/// never generated (`:50-73`); without a mask those maps are the identity, so
/// they are omitted here. Masking arrives with proxy geometry hiding (M4).
class Subdivider {
public:
    /// Builds the subdivision topology. Fails on a non-quad mesh — the
    /// reference bails out the same way (`:516-518`).
    [[nodiscard]] static std::expected<Subdivider, MeshError> build(const Mesh& parent);

    /// Recomputes subdivided vertex positions from the parent's current
    /// positions, then normals. Cheap: no topology work.
    void refresh(const Mesh& parent);

    [[nodiscard]] const Mesh& mesh() const noexcept { return mesh_; }

    [[nodiscard]] Mesh& mesh() noexcept { return mesh_; }

    /// Index of the first face point / first edge point in the result.
    [[nodiscard]] uint32_t faceBase() const noexcept { return cbase_; }

    [[nodiscard]] uint32_t edgeBase() const noexcept { return ebase_; }

    [[nodiscard]] size_t edgeCount() const noexcept { return edgeVerts_.size(); }

    /// True while this topology still matches @p parent.
    [[nodiscard]] bool matches(const Mesh& parent) const noexcept {
        return builtVertexCount_ == parent.vertexCount() && builtFaceCount_ == parent.faceCount();
    }

private:
    /// One unique parent edge.
    struct Edge {
        uint32_t v0{}, v1{};  ///< endpoint parent vertices
        uint32_t f0{}, f1{};  ///< adjacent parent faces; f0 == f1 means a boundary edge
    };

    Mesh mesh_;

    uint32_t cbase_{};
    uint32_t ebase_{};

    std::vector<Edge> edgeVerts_;

    // Parent edges incident to each parent vertex: flat, stride = maxEdgeValence_.
    std::vector<uint32_t> vedge_;
    std::vector<uint32_t> nedges_;
    uint32_t maxEdgeValence_{};

    size_t builtVertexCount_{};
    size_t builtFaceCount_{};
};

}  // namespace mh::core
