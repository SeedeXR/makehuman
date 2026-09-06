// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include "makehuman/core/Types.h"
#include "makehuman/foundation/Geometry.h"

#include <cstdint>
#include <expected>
#include <optional>
#include <span>
#include <string>
#include <unordered_map>
#include <vector>

namespace mh::core {

enum class MeshError {
    VertexIndexOutOfRange,
    UvIndexOutOfRange,
    FaceArraySizeMismatch,  ///< faceVerts not a whole number of primitives
    UvArraySizeMismatch,    ///< faceUVs present but not parallel to faceVerts
    MaskSizeMismatch,       ///< a vertex mask not parallel to the vertex array
};

/// Indexed mesh with a uniform primitive size.
///
/// This mirrors the reference's `Object3D` (legacy/python/core/module3d.py:110)
/// closely enough to be parity-testable, while using struct-of-arrays layout
/// throughout. Two properties of the reference are load-bearing and preserved:
///
/// 1. **Uniform primitive size.** Every face has exactly `vertsPerPrimitive`
///    corners (4 for the base mesh). Triangles are stored as *degenerate quads*
///    by repeating corner 0 — legacy/python/shared/wavefront.py:105-106.
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

    [[nodiscard]] size_t faceCount() const noexcept {
        return vertsPerPrimitive_ ? fvert_.size() / vertsPerPrimitive_ : 0;
    }

    [[nodiscard]] size_t uvCount() const noexcept { return texco_.size(); }

    /// True only when UVs exist AND faces carry UV indices. Derived rather than
    /// stored, so it cannot go stale if setUVs/setFaces are called out of order.
    [[nodiscard]] bool hasUV() const noexcept { return !texco_.empty() && !fuvs_.empty(); }

    [[nodiscard]] uint32_t maxValence() const noexcept { return maxValence_; }

    /// Number of faces incident to vertex @p v. Requires buildAdjacency().
    [[nodiscard]] uint32_t nfacesAt(size_t v) const noexcept {
        return v < nfaces_.size() ? nfaces_[v] : 0U;
    }

    /// Bumped every time the face arrays change. Derived structures (RenderMesh,
    /// Subdivider) record it so they can detect staleness exactly in O(1) --
    /// comparing vertex and face counts alone misses a same-size topology swap,
    /// which would silently produce wrong geometry rather than a caught error.
    [[nodiscard]] uint64_t topologyVersion() const noexcept { return topologyVersion_; }

    /// Re-captures the current positions as the morph base. Meaningful for a
    /// derived mesh (e.g. a subdivision result) whose positions are recomputed
    /// rather than set once.
    void captureOriginal() { origCoord_ = coord_; }

    // -- data (read) --------------------------------------------------------
    [[nodiscard]] std::span<const Vec3> coord() const noexcept { return coord_; }

    [[nodiscard]] std::span<const Vec3> origCoord() const noexcept { return origCoord_; }

    [[nodiscard]] std::span<const Vec3> vnorm() const noexcept { return vnorm_; }

    /// xyz = tangent, w = handedness (+1 / -1). Empty until calcVertexTangents().
    [[nodiscard]] std::span<const Vec4> vtang() const noexcept { return vtang_; }

    [[nodiscard]] std::span<const Vec3> fnorm() const noexcept { return fnorm_; }

    [[nodiscard]] std::span<const Vec2> texco() const noexcept { return texco_; }

    [[nodiscard]] std::span<const uint32_t> fvert() const noexcept { return fvert_; }

    [[nodiscard]] std::span<const uint32_t> fuvs() const noexcept { return fuvs_; }

    [[nodiscard]] std::span<const uint16_t> group() const noexcept { return group_; }

    [[nodiscard]] const std::vector<FaceGroup>& faceGroups() const noexcept { return faceGroups_; }

    /// A plain-data view for the format layer.
    ///
    /// `mh_io` is Apache-2.0 and must not link this AGPL module, so it takes
    /// `foundation::MeshView` rather than a `Mesh`. This is the one place that
    /// bridges the two, and it goes in the AGPL direction, which is legal.
    [[nodiscard]] foundation::MeshView view() const noexcept {
        return foundation::MeshView{coord_,
                                    texco_,
                                    vnorm_,
                                    fvert_,
                                    fuvs_,
                                    group_,
                                    faceGroups_,
                                    vertsPerPrimitive_,
                                    vertsPerFaceForExport()};
    }

    /// Adopts geometry produced by an importer, validating every index.
    ///
    /// `foundation::MeshData` is unvalidated by construction -- it comes from a
    /// file. Routing it through setCoords/setUVs/setFaces means an imported
    /// mesh gets exactly the same checks as one built in process.
    [[nodiscard]] static std::expected<Mesh, MeshError> fromData(foundation::MeshData data);

    /// The base mesh's **static** face mask: hides helper geometry.
    ///
    /// `base.obj` carries 139 face groups of which **138 are helpers** --
    /// `joint-*` markers and `helper-*` cages used to fit clothes. Only `body`
    /// is the visible human. Rendering the raw mesh draws all of it, which
    /// looks like a figure wearing a solid skirt with a box over its face --
    /// plausible enough that it reads as a renderer bug rather than as
    /// geometry that was never meant to be shown.
    ///
    /// The rule is the reference's: hide any group whose name starts with
    /// `joint-` or `helper-` (`apps/human.py:274-289`).
    ///
    /// @return one byte per face, nonzero = visible.
    [[nodiscard]] std::vector<uint8_t> staticFaceMask() const;

    /// Face visibility derived from vertex visibility, 1 = visible.
    ///
    /// **A face is visible if ANY of its corners is visible**, and is hidden
    /// only once every corner is hidden. That is what the reference does:
    /// `changeVertexMask` (guicommon.py:532-557) builds the face mask through
    /// `getFaceMaskForVertices` (module3d.py:1149-1159), which marks every face
    /// incident to a *visible* vertex.
    ///
    /// The inverted reading -- hide a face as soon as one corner is hidden --
    /// is the natural-seeming one and is wrong. It is pinned by the `stride`
    /// fixture, where hiding every 7th vertex (2,737 of 19,158) hides **zero**
    /// of the 18,486 faces; the inverted rule would delete most of the mesh.
    ///
    /// The reference walks the `vface` adjacency table; this scans the corner
    /// array instead. Identical result -- `f` is in `vface[v]` exactly when `v`
    /// is a corner of `f` -- in O(corners) rather than O(verts x MAX_FACES),
    /// and with no dependence on adjacency being built.
    ///
    /// @param vertexVisible  one byte per vertex, nonzero = visible.
    [[nodiscard]] std::expected<std::vector<uint8_t>, MeshError> faceMaskForVisibleVertices(
        std::span<const uint8_t> vertexVisible) const;

    /// Mutable positions, for morph-target application. Callers must call
    /// calcNormals() afterwards; this deliberately does not do it implicitly.
    [[nodiscard]] std::span<Vec3> mutableCoord() noexcept { return coord_; }

    // -- construction -------------------------------------------------------
    /// Sets vertex positions and captures them as the morph base. Rejected if
    /// it would strand a vertex index already recorded in the face arrays --
    /// the same reasoning as setUVs.
    [[nodiscard]] std::expected<void, MeshError> setCoords(std::vector<Vec3> coords);
    /// Sets the UV array. Rejected if it would strand a UV index already
    /// recorded in the face arrays -- setFaces validates against the UVs
    /// present at that moment, so shrinking them afterwards would leave
    /// out-of-range indices for the next consumer to read through.
    [[nodiscard]] std::expected<void, MeshError> setUVs(std::vector<Vec2> uvs);

    /// Sets the face arrays, validating every index against the current vertex
    /// and UV counts. Call setCoords() (and setUVs(), if any) first.
    ///
    /// On error the mesh is left unchanged. Validation happens here because
    /// this is a public trust boundary: the reference's downstream code indexes
    /// `coord[fvert[...]]` unguarded, so an out-of-range index read out of
    /// bounds rather than reporting anything.
    ///
    /// @param faceVerts  vertex index per corner, size = nFaces * vertsPerPrimitive
    /// @param faceUVs    UV index per corner, same size, or empty for no UVs
    /// @param faceGroup  group index per face, size = nFaces (empty -> all zero)
    [[nodiscard]] std::expected<void, MeshError> setFaces(std::vector<uint32_t> faceVerts,
                                                          std::vector<uint32_t> faceUVs,
                                                          std::vector<uint16_t> faceGroup);

    /// Sets the mesh name. OBJ `o` statements route here, matching
    /// wavefront.py:128-129 (which sets the name and creates no face group).
    void setName(std::string name) { name_ = std::move(name); }

    uint16_t addFaceGroup(std::string name);
    [[nodiscard]] std::optional<uint16_t> findFaceGroup(std::string_view name) const;

    /// Restores every vertex to the position captured by setCoords().
    /// Reference: legacy/python/core/algos3d.py:493-494 (`resetObj`).
    void resetToOriginal();

    // -- derived data -------------------------------------------------------

    /// Builds per-vertex face adjacency (`vface`/`nfaces` in the reference) and
    /// sets `maxValence` to the maximum number of incident FACES, floored at 4.
    ///
    /// @warning This is **not** the reference's `MAX_FACES`. The reference takes
    /// `max(maxIncidentFaces, maxpole, 4)` where `maxpole` counts distinct
    /// neighbouring *vertices* (module3d.py:752-770), explicitly because
    /// "catmull-clark expects maxpoles and not maxfaces". Both happen to be 5
    /// for the shipped base mesh. A Catmull-Clark port must compute maxpole
    /// separately rather than reusing this value.
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

    /// Per-vertex tangents by Lengyel's method
    /// (https://terathon.com/blog/tangent-space.html), Gram-Schmidt
    /// orthogonalised against the vertex normal, with handedness in `w`.
    ///
    /// No-op when the mesh has no UVs. Computes normals first if they are
    /// missing, since the Gram-Schmidt step needs them.
    ///
    /// @warning This deliberately does **not** reproduce the reference
    /// (module3d.py:371-449), which is wrong in three independent ways:
    ///   1. `t2 = w3[:,1] = w1[:,1]` (:411) is a chained assignment, so `t2`
    ///      becomes `w1.y` instead of `w3.y - w1.y`.
    ///   2. `np.sum(sdir[vface[ix]])` (:429) omits `axis=`, collapsing the
    ///      per-vertex accumulation to a single scalar.
    ///   3. Unlike calcVertexNormals (:366), it never masks `vface` by
    ///      `nfaces`, so face 0 is summed into every vertex whose valence is
    ///      below the array stride.
    /// Parity tests must therefore assert mathematical properties here, not
    /// equality with the reference. See memory/test.md section 3.4.
    void calcVertexTangents();

    /// Axis-aligned bounds over all vertices. Returns nullopt for an empty mesh.
    [[nodiscard]] std::optional<std::pair<Vec3, Vec3>> boundingBox() const;

    /// Height in centimetres: the Y extent of the BODY, scaled by 10.
    ///
    /// "Body" is load-bearing and is why this is not just `boundingBox()`. The
    /// reference measures `calcBBox(fixedFaceMask = staticFaceMask)` and
    /// documents it as "the bounding box of the basemesh **without the
    /// helpers**" (`legacy/python/apps/human.py:701-706`). The helper cages --
    /// the skirt, the tights, the joint cubes -- stick out past the body, so
    /// including them overstates the figure: measured on the shipped base mesh,
    /// 169.455 cm with helpers against 166.589 cm without, an error of 2.87 cm.
    ///
    /// `boundingBox()` deliberately stays unmasked: the exporters ground the
    /// model on it, and every vertex they write has to be inside it.
    [[nodiscard]] float heightCm() const;

    /// Surface area of the BODY, in square decimetres.
    ///
    /// The same subset `heightCm()` measures -- the static face mask, which on
    /// the shipped base mesh is exactly the `body` group's 13,378 of 18,486
    /// faces. The helper cages and joint cubes roughly double the total, so
    /// including them is not a rounding difference.
    ///
    /// Quads are split (0,1,2) and (2,3,0) and each triangle measured by
    /// Heron's formula, which is what the reference does
    /// (`legacy/python/shared/mesh_operations.py:56-71,130-149`). Heron is a
    /// worse conditioned way to get a triangle's area than half the cross
    /// product, and it is used here anyway: this number is compared against the
    /// reference's, and a better formula would disagree with it.
    [[nodiscard]] float bodySurfaceArea() const;

    /// Estimated weight in kilograms.
    ///
    /// Mosteller's body-surface-area formula, inverted:
    /// `bsa^2 * 3600 / heightCm`, with `bsa` the area in SQUARE METRES
    /// (`legacy/python/apps/human.py:635-638`). An estimate from two
    /// measurements, not a density model -- it knows nothing about muscle or
    /// fat beyond what they do to the silhouette.
    ///
    /// Zero for an empty or zero-height mesh rather than a division by it.
    [[nodiscard]] float weightKg() const;

private:
    std::string name_;
    uint8_t vertsPerPrimitive_{4};
    uint8_t vertsPerFaceForExport_{4};
    uint32_t maxValence_{4};
    uint64_t topologyVersion_{1};

    // per-vertex
    std::vector<Vec3> coord_;
    std::vector<Vec3> origCoord_;
    std::vector<Vec3> vnorm_;
    std::vector<Vec4> vtang_;
    std::vector<uint32_t> vface_;  // flat, stride = maxValence_
    std::vector<uint32_t> nfaces_;

    // per-UV (independent index space)
    std::vector<Vec2> texco_;

    // per-face
    std::vector<uint32_t> fvert_;  // stride = vertsPerPrimitive_
    std::vector<uint32_t> fuvs_;
    std::vector<Vec3> fnorm_;
    std::vector<uint16_t> group_;

    std::vector<FaceGroup> faceGroups_;
    std::unordered_map<std::string, uint16_t> groupsByName_;
};

}  // namespace mh::core
