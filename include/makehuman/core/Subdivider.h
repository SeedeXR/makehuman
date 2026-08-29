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
    /// Builds the subdivision topology. Fails unless the mesh is genuinely
    /// quads: the gate is `vertsPerFaceForExport() != 4`, matching the
    /// reference (`:516-518`). A triangle mesh loaded from OBJ has
    /// `vertsPerPrimitive() == 4` (degenerate quads) but export-3, so gating on
    /// the former would subdivide meshes the reference declines.
    ///
    /// Unlike the reference (`:520-521`) a UV-less mesh is accepted; there is
    /// nothing to subdivide in the UV space, and positions are unaffected.
    [[nodiscard]] static std::expected<Subdivider, MeshError> build(const Mesh& parent);

    /// Recomputes subdivided vertex positions from the parent's current
    /// positions, then normals. Cheap: no topology work.
    ///
    /// Does nothing if @p parent's topology changed since build() — see
    /// matches(). Rebuild instead.
    void refresh(const Mesh& parent);

    [[nodiscard]] const Mesh& mesh() const noexcept { return mesh_; }

    [[nodiscard]] Mesh& mesh() noexcept { return mesh_; }

    /// Index of the first face point / first edge point in the result.
    [[nodiscard]] uint32_t faceBase() const noexcept { return cbase_; }

    [[nodiscard]] uint32_t edgeBase() const noexcept { return ebase_; }

    [[nodiscard]] size_t edgeCount() const noexcept { return edgeVerts_.size(); }

    /// True while this topology still matches @p parent. Exact, not a
    /// heuristic: a same-size topology swap bumps the parent's topology version.
    [[nodiscard]] bool matches(const Mesh& parent) const noexcept {
        return builtVertexCount_ == parent.vertexCount() &&
               builtTopologyVersion_ == parent.topologyVersion();
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

    // Parent FACES incident to each parent vertex, counted the way the
    // reference counts them (module3d.py:709-717): the first
    // min(vpp, vertsPerFaceForExport) corners of each face, with no dedup of a
    // repeated vertex. Mesh::buildAdjacency deduplicates, so reusing it would
    // give a different valence for a triangle stored as a degenerate quad and
    // flip the interior/boundary branch. Owning this also removes a hidden
    // precondition: refresh() no longer depends on the parent having had
    // buildAdjacency() called.
    std::vector<uint32_t> vfaceOfVert_;
    std::vector<uint32_t> nfacesOfVert_;
    uint32_t maxFaceValence_{};

    size_t builtVertexCount_{};
    uint64_t builtTopologyVersion_{};
};

}  // namespace mh::core
