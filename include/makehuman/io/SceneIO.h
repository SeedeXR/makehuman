// SPDX-License-Identifier: Apache-2.0
//
// Multi-format import/export backed by assimp (BSD-3-Clause).
//
// NOTE ON FBX AND THE AUTODESK SDK. The Autodesk FBX SDK is free of charge, but
// its licence agreement is incompatible with distributing an AGPL binary:
//   - section 2.1.5(iii) requires the licensee to ensure their use does not
//     "cause the Software to be subject to any licensing terms other than those
//     set forth in this Agreement";
//   - section 21 defines "Open Source" by explicitly naming GPL and LGPL as the
//     licence family whose terms require source disclosure and free
//     redistribution of derivative works;
//   - section 10.2.1(g) requires that any permitted redistribution be governed
//     by an EULA that *prohibits* further redistribution -- the direct opposite
//     of what AGPL section 6 requires.
// Those obligations attach on distribution. Building locally for yourself is
// unaffected; shipping an AGPL binary linked against it is not permissible.
//
// assimp writes FBX 7500 (FBX 2016), which is NEWER than the 7300 (2013) the
// Python reference emits, is BSD-3-Clause, and carries no such restriction.
// See LICENSING.md section 5.
#pragma once

#include "makehuman/foundation/Geometry.h"

#include "makehuman/io/ObjWriter.h"  // Unit

#include <cstdint>
#include <expected>
#include <filesystem>
#include <string>
#include <vector>

namespace mh::io {

/// Formats reachable through the assimp backend.
enum class SceneFormat : uint8_t {
    /// **Binary FBX, version 7500 (FBX 2016)** -- the default, and the form
    /// every DCC and engine expects. Verified from the written bytes: the file
    /// opens with `Kaydara FBX Binary` and carries version 7500, newer than the
    /// 7300 (FBX 2013) the Python reference emits.
    FbxBinary,
    FbxAscii,   ///< text FBX; for debugging only
    Collada,    ///< "collada"
    StlBinary,  ///< "stlb"
    StlAscii,   ///< "stl"
    ThreeMf,    ///< "3mf"
};

// PLY is deliberately ABSENT.
//
// assimp 6.0.4's PLY exporter writes corrupt face indices whenever the mesh
// carries texture coordinates. Minimal reproduction: a 10-quad grid WITH UVs
// exports without error and segfaults on reimport, while the same grid WITHOUT
// UVs round-trips fine at 80,002 vertices. assimp's own validator rejects the
// file: "aiMesh::mFaces[0]::mIndices[0] is out of range".
//
// The crash is non-deterministic -- it depends on heap contents -- which is
// exactly why it must not be shipped. Every mesh this project exports has UVs,
// so offering PLY would mean offering a format that silently produces broken
// files. It can return if the upstream writer is fixed.

[[nodiscard]] std::string_view formatId(SceneFormat f) noexcept;
[[nodiscard]] std::string_view formatExtension(SceneFormat f) noexcept;

struct SceneExportOptions {
    Unit unit{Unit::Centimeter};  ///< FBX's conventional unit is the centimetre
    float scale{1.0F};
    bool feetOnGround{false};
    bool writeNormals{true};
    bool writeUVs{true};

    std::string meshName{"MakeHuman"};
    std::string materialName{"Skin"};
};

enum class SceneIoErrorKind { EmptyMesh, UnsupportedFormat, ExportFailed, ImportFailed, NotFound };

struct SceneIoError {
    SceneIoErrorKind kind{};
    std::string file;
    std::string detail;  ///< the backend's own message, when it has one

    [[nodiscard]] std::string message() const;
};

struct SceneExportResult {
    size_t vertices{};
    size_t triangles{};
    size_t fileBytes{};
};

/// Writes @p mesh in @p format.
///
/// The mesh is unwelded and triangulated first, since every format here wants
/// one attribute per index and none of them has a quad primitive we rely on.
[[nodiscard]] std::expected<SceneExportResult, SceneIoError> exportScene(
    const std::filesystem::path& path, const foundation::RenderView& mesh, SceneFormat format,
    const SceneExportOptions& options = {}, const foundation::MaterialDesc* material = nullptr);

struct ImportedMesh {
    foundation::MeshData mesh;
    size_t meshCount{};  ///< meshes in the file; only the first is returned
};

/// Reads a mesh from any format assimp supports (FBX, glTF, OBJ, DAE, STL…).
///
/// **The Python reference has no import capability at all** — verified: no
/// importer machinery exists anywhere in the tree, and its only mesh reader is
/// the OBJ loader used for its own assets. This is new capability, not a port.
///
/// Only the first mesh is returned; a full scene graph belongs to a later
/// milestone.
[[nodiscard]] std::expected<ImportedMesh, SceneIoError> importMesh(
    const std::filesystem::path& path);

}  // namespace mh::io
