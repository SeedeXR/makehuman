// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "makehuman/foundation/Types.h"

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
    std::filesystem::path diffuseTexture;
    std::filesystem::path normalTexture;
};

}  // namespace mh::foundation
