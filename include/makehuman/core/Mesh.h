// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include "makehuman/core/Types.h"

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <unordered_map>
#include <vector>

namespace mh::core {

/// Indexed mesh with a uniform primitive size.
///
/// This mirrors the reference's `Object3D` (legacy-python/core/module3d.py:110)
/// closely enough to be parity-testable, while using struct-of-arrays layout
/// throughout. Two properties of the reference are load-bearing and preserved:
///
/// 1. **Uniform primitive size.** Every face has exactly `vertsPerPrimitive`
///    corners (4 for the base mesh). Triangles are stored as *degenerate quads*
///    by repeating corner 0 — legacy-python/shared/wavefront.py:105-106.
///
/// 2. **Dual index space.** A face corner references `(fvert[i], fuvs[i])`.
///    Positions and UVs have independent index spaces
///    (module3d.py:627 and :629), so the mesh must be "unwelded" before it can
///    be handed to a GPU.
class Mesh {
public:
    Mesh() = default;
    explicit Mesh(std::string name, uint8_t vertsPerPrimitive = 4);

    // -- identity -----------------------------------------------------------
    [[nodiscard]] const std::string& name() const noexcept { return name_; }
    [[nodiscard]] uint8_t vertsPerPrimitive() const noexcept { return vertsPerPrimitive_; }

    /// 3 when the quads are degenerate triangles, else `vertsPerPrimitive`.
    /// Reference: module3d.py:634-639 — decided from the *first* face only.
    [[nodiscard]] uint8_t vertsPerFaceForExport() const noexcept { return vertsPerFaceForExport_; }

    // -- counts -------------------------------------------------------------
    [[nodiscard]] size_t vertexCount() const noexcept { return coord_.size(); }
    [[nodiscard]] size_t faceCount()   const noexcept {
        return vertsPerPrimitive_ ? fvert_.size() / vertsPerPrimitive_ : 0;
    }
    [[nodiscard]] size_t uvCount()     const noexcept { return texco_.size(); }
    [[nodiscard]] bool   hasUV()       const noexcept { return hasUV_; }
    [[nodiscard]] uint8_t maxValence() const noexcept { return maxValence_; }

    // -- data (read) --------------------------------------------------------
    [[nodiscard]] std::span<const Vec3> coord()     const noexcept { return coord_; }
    [[nodiscard]] std::span<const Vec3> origCoord() const noexcept { return origCoord_; }
    [[nodiscard]] std::span<const Vec3> vnorm()     const noexcept { return vnorm_; }
    [[nodiscard]] std::span<const Vec3> fnorm()     const noexcept { return fnorm_; }
    [[nodiscard]] std::span<const Vec2> texco()     const noexcept { return texco_; }
    [[nodiscard]] std::span<const uint32_t> fvert() const noexcept { return fvert_; }
    [[nodiscard]] std::span<const uint32_t> fuvs()  const noexcept { return fuvs_; }
    [[nodiscard]] std::span<const uint16_t> group() const noexcept { return group_; }
    [[nodiscard]] const std::vector<FaceGroup>& faceGroups() const noexcept { return faceGroups_; }

    /// Mutable positions, for morph-target application. Callers must call
    /// calcNormals() afterwards; this deliberately does not do it implicitly.
    [[nodiscard]] std::span<Vec3> mutableCoord() noexcept { return coord_; }

    // -- construction -------------------------------------------------------
    void setCoords(std::vector<Vec3> coords);
    void setUVs(std::vector<Vec2> uvs);

    /// @param faceVerts  vertex index per corner, size = nFaces * vertsPerPrimitive
    /// @param faceUVs    UV index per corner, same size, or empty for no UVs
    /// @param faceGroup  group index per face, size = nFaces (empty -> all zero)
    void setFaces(std::vector<uint32_t> faceVerts,
                  std::vector<uint32_t> faceUVs,
                  std::vector<uint16_t> faceGroup);

    uint16_t addFaceGroup(std::string name);
    [[nodiscard]] std::optional<uint16_t> findFaceGroup(std::string_view name) const;

    /// Restores every vertex to the position captured by setCoords().
    /// Reference: legacy-python/core/algos3d.py:493-494 (`resetObj`).
    void resetToOriginal();

    // -- derived data -------------------------------------------------------

    /// Builds per-vertex face adjacency (`vface`/`nfaces` in the reference) and
    /// sets `maxValence` to the true maximum, floored at 4.
    ///
    /// The reference does this in nested Python loops
    /// (module3d.py:697-770) and it is the single slowest operation in the
    /// codebase at ~212 ms; the `.npz` mesh cache exists purely to skip it.
    void buildAdjacency();

    /// Face normals: cross product of the first three corners, **left
    /// unnormalised**, exactly as module3d.py:333-353. Vertex normals then sum
    /// the incident face normals and normalise, which makes them area-weighted
    /// (module3d.py:355-369).
    void calcNormals();
    void calcFaceNormals();
    void calcVertexNormals();

    /// Axis-aligned bounds over all vertices. Returns nullopt for an empty mesh.
    [[nodiscard]] std::optional<std::pair<Vec3, Vec3>> boundingBox() const;

    /// Height in centimetres, i.e. the Y extent scaled by 10.
    /// Reference: legacy-python/apps/human.py:694-699.
    [[nodiscard]] float heightCm() const;

private:
    std::string name_;
    uint8_t     vertsPerPrimitive_{4};
    uint8_t     vertsPerFaceForExport_{4};
    uint8_t     maxValence_{4};
    bool        hasUV_{false};

    // per-vertex
    std::vector<Vec3>     coord_;
    std::vector<Vec3>     origCoord_;
    std::vector<Vec3>     vnorm_;
    std::vector<uint32_t> vface_;   // flat, stride = maxValence_
    std::vector<uint8_t>  nfaces_;

    // per-UV (independent index space)
    std::vector<Vec2> texco_;

    // per-face
    std::vector<uint32_t> fvert_;   // stride = vertsPerPrimitive_
    std::vector<uint32_t> fuvs_;
    std::vector<Vec3>     fnorm_;
    std::vector<uint16_t> group_;

    std::vector<FaceGroup>                    faceGroups_;
    std::unordered_map<std::string, uint16_t> groupsByName_;
};

} // namespace mh::core
