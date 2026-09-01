// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "makehuman/foundation/Types.h"

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <vector>

namespace mh::foundation {

/// The plain-data boundary between the geometry core and the format layer.
///
/// `mh_io` is Apache-2.0 and must not depend on the AGPL core (LICENSING.md §4).
/// Until now it did: its headers took `core::Mesh` and `core::Material`, and its
/// glTF and FBX paths called `core::RenderMesh::build`, which is a port of
/// `module3d.py`'s unweld. So the Apache stamp bought nobody anything -- linking
/// io pulled in AGPL either way.
///
/// These types are the fix. They carry **data, not behaviour**: no algorithm
/// here is ported from the reference, so they are genuinely Apache-2.0. The
/// direction of the dependency inverts -- core knows how to produce a view, io
/// only knows how to read one.
///
/// The views are non-owning. They must not outlive whatever produced them.

/// Indexed geometry as the mesh natively stores it: two independent index
/// spaces, and a uniform primitive size that may be 4 (quads).
///
/// This is what OBJ wants, because OBJ preserves quads.
struct MeshView {
    std::span<const Vec3> coord;
    std::span<const Vec2> texco;
    std::span<const Vec3> vnorm;

    /// Vertex index per face corner, stride = vertsPerPrimitive.
    std::span<const uint32_t> fvert;
    /// UV index per face corner, parallel to fvert, or empty.
    std::span<const uint32_t> fuvs;
    /// Face group id per face.
    std::span<const uint16_t> group;
    /// Group names, indexed by the ids in `group`.
    std::span<const FaceGroup> groups;

    uint8_t vertsPerPrimitive{4};
    /// 3 when every quad is degenerate (a stored triangle), else the same as
    /// vertsPerPrimitive. Computed by the core; exporters only read it.
    uint8_t vertsPerFaceForExport{4};

    [[nodiscard]] size_t vertexCount() const noexcept { return coord.size(); }

    [[nodiscard]] size_t faceCount() const noexcept {
        return vertsPerPrimitive != 0 ? fvert.size() / vertsPerPrimitive : 0;
    }

    [[nodiscard]] bool hasUV() const noexcept { return !texco.empty() && !fuvs.empty(); }
};

/// Unwelded triangle geometry: one attribute set per index, ready for a GPU or
/// for a format that has no quad primitive.
///
/// glTF and FBX want this. Producing it costs an unweld, which is core's job --
/// requiring the caller to pass one in makes that cost visible instead of
/// hiding it inside a writer.
struct RenderView {
    std::span<const Vec3> coord;
    std::span<const Vec2> texco;
    std::span<const Vec3> vnorm;
    std::span<const Vec4> vtang;
    std::span<const uint32_t> index;  ///< triangle list

    [[nodiscard]] size_t vertexCount() const noexcept { return coord.size(); }

    [[nodiscard]] size_t indexCount() const noexcept { return index.size(); }

    [[nodiscard]] size_t triangleCount() const noexcept { return index.size() / 3; }

    [[nodiscard]] bool hasUV() const noexcept { return !texco.empty(); }
};

/// Geometry produced by an importer: owned, and not yet validated against the
/// core's invariants. The core validates when it adopts this.
struct MeshData {
    std::string name;
    std::vector<Vec3> coord;
    std::vector<Vec2> texco;
    std::vector<uint32_t> fvert;
    std::vector<uint32_t> fuvs;
    uint8_t vertsPerPrimitive{3};  ///< importers triangulate

    [[nodiscard]] size_t vertexCount() const noexcept { return coord.size(); }

    [[nodiscard]] size_t faceCount() const noexcept {
        return vertsPerPrimitive != 0 ? fvert.size() / vertsPerPrimitive : 0;
    }

    [[nodiscard]] bool hasUV() const noexcept { return !texco.empty() && !fuvs.empty(); }
};

/// A skeleton plus per-vertex influences, as a format writer needs them.
///
/// The joint transforms are supplied **unscaled**, in model space. The writer
/// applies the export unit scale itself and derives both the local node
/// transforms and the inverse-bind matrices from the scaled result. Supplying
/// them pre-scaled would let the joints and the mesh drift apart whenever the
/// two scales disagreed -- and a rig that is 10x the mesh looks like a rigging
/// bug, not a unit bug.
struct SkinView {
    std::span<const std::string> jointNames;
    /// Parent index per joint, -1 for a root. Parents must precede children.
    std::span<const int32_t> jointParents;
    /// Each joint's rest transform in model space, unscaled.
    std::span<const Mat4> globalRest;

    /// `influences` entries per RENDER vertex (i.e. after the unweld), not per
    /// mesh vertex. glTF's JOINTS_0/WEIGHTS_0 are attributes, so they are
    /// indexed exactly like positions.
    std::span<const uint32_t> joints;
    std::span<const float> weights;
    uint8_t influences{4};

    [[nodiscard]] size_t jointCount() const noexcept { return globalRest.size(); }

    [[nodiscard]] size_t vertexCount() const noexcept {
        return influences != 0 ? joints.size() / influences : 0;
    }

    /// Structural consistency only. The required influence COUNT is a
    /// format's business, not this type's -- glTF wants exactly 4, other
    /// formats differ -- so the writer checks that separately.
    [[nodiscard]] bool valid() const noexcept {
        return influences > 0 && !globalRest.empty() && jointParents.size() == globalRest.size() &&
               jointNames.size() == globalRest.size() && joints.size() == weights.size();
    }
};

/// One morph target (blend shape), as a format writer needs it.
///
/// `deltas` are **displacements from the base mesh**, not absolute positions,
/// and there is one per RENDER vertex — glTF morph targets are parallel
/// attribute arrays, so they are indexed exactly like POSITION.
///
/// MakeHuman stores targets sparsely (a few thousand touched vertices out of
/// 19,158), which is the better representation and is why applying one is
/// cheap. glTF can express that with a sparse accessor, but support is patchy;
/// dense is what every engine reads. The caller therefore chooses which targets
/// to export -- all 1,280 densely would be ~335 MB.
struct MorphTarget {
    std::string name;
    std::span<const Vec3> deltas;
};

/// The material values a format writer needs. Deliberately not the whole
/// `core::Material`: exporters use these eight fields and nothing else, and the
/// full parser is a port of `material.py` that cannot cross into Apache code.
struct MaterialDesc {
    std::string name{"Material"};
    Vec3 ambient{1, 1, 1};
    Vec3 diffuse{1, 1, 1};
    Vec3 specular{1, 1, 1};
    float shininess{0.2F};
    float opacity{1.0F};
    /// The material declares itself transparent, independently of `opacity`.
    ///
    /// A texture's alpha channel is worthless without this: glTF's default
    /// alphaMode is OPAQUE and the spec then says alpha is *ignored*, so a
    /// cut-out cornea over `opacity 1.0` renders solid. `.mhmat` carries the
    /// flag (`transparent True`) and it must survive into the description.
    bool transparent{false};
    std::filesystem::path diffuseTexture;
    std::filesystem::path normalTexture;
};

/// The largest Blinn-Phong specular exponent OpenGL's fixed-function pipeline
/// accepts (`GL_SHININESS` is specified as 0..128), and the scale every
/// Blinn-Phong interchange format inherited from it.
inline constexpr float kMaxSpecularExponent = 128.0F;

/// `MaterialDesc::shininess` as the specular EXPONENT a Blinn-Phong file wants.
///
/// The two ranges are the whole reason this exists. `shininess` is 0..1 --
/// `.mhmat` clamps it on the way in -- while FBX, Collada and assimp's
/// `AI_MATKEY_SHININESS` all carry an exponent. Writing the 0..1 number into an
/// exponent field says "almost perfectly matte" for a value that means "almost
/// perfectly polished".
[[nodiscard]] constexpr float specularExponentOf(float shininess) noexcept {
    return shininess * kMaxSpecularExponent;
}

/// The inverse, and it must stay the inverse: a round trip through any
/// Blinn-Phong format goes out through one and back through the other.
///
/// Clamped, because the incoming number is whatever the file says. assimp's own
/// Collada exporter emits a fixed exponent of 10 when none is supplied, and 10
/// stored unscaled in a 0..1 field then makes `1 - shininess` (the glTF and USD
/// roughness conversion) come out **negative** -- clamping to a perfect mirror.
[[nodiscard]] constexpr float shininessFromExponent(float exponent) noexcept {
    return std::clamp(exponent / kMaxSpecularExponent, 0.0F, 1.0F);
}

}  // namespace mh::foundation
