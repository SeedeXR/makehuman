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
#include <optional>
#include <span>
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

enum class SceneIoErrorKind {
    EmptyMesh,
    UnsupportedFormat,
    ExportFailed,
    ImportFailed,
    NotFound,
    /// A skin or morph target does not describe this mesh.
    InvalidSkinOrMorph,
};

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

/// One mesh in a multi-mesh scene: a dressed character is the body plus each
/// worn proxy, each with its own name and material.
struct SceneEntry {
    foundation::RenderView mesh;
    std::string name{"MakeHuman"};
    const foundation::MaterialDesc* material{nullptr};
    /// Only ONE entry in a scene may carry a skin. Only the body is rigged --
    /// worn proxies follow it by being re-fitted, not skinned -- and a second
    /// skeleton in the graph is a scene shape nothing needs and nothing tests.
    const foundation::SkinView* skin{nullptr};
    /// Only ONE entry may carry morph targets, for the same reason as the skin:
    /// a worn proxy is re-fitted to the body rather than blended, so a second
    /// set is a scene shape nothing needs and nothing tests.
    std::span<const foundation::MorphTarget> morphTargets{};
};

/// Writes every entry as its own mesh in one scene.
///
/// assimp's `aiScene` holds many meshes natively, so unlike OBJ and glTF this
/// needed no index arithmetic -- each entry becomes an `aiMesh` with its own
/// material index, all referenced from the root node.
///
/// **A skin belongs to the single-mesh overload.** Only the body is rigged, and
/// One entry may carry a skin. It used not to: the scene overload took no skin
/// at all, so the moment a character wore anything its FBX and Collada exports
/// became statues -- and FBX is the format a rigged character is usually handed
/// over in.
///
/// `feetOnGround` levels the whole scene by the lowest point of any entry:
/// levelling each mesh alone would drop the clothes to the floor beside the
/// body.
[[nodiscard]] std::expected<SceneExportResult, SceneIoError> exportScene(
    const std::filesystem::path& path, std::span<const SceneEntry> entries, SceneFormat format,
    const SceneExportOptions& options = {});

/// Writes @p mesh in @p format.
///
/// The mesh is unwelded and triangulated first, since every format here wants
/// one attribute per index and none of them has a quad primitive we rely on.
[[nodiscard]] std::expected<SceneExportResult, SceneIoError> exportScene(
    const std::filesystem::path& path, const foundation::RenderView& mesh, SceneFormat format,
    const SceneExportOptions& options = {}, const foundation::MaterialDesc* material = nullptr,
    const foundation::SkinView* skin                      = nullptr,
    std::span<const foundation::MorphTarget> morphTargets = {});

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
/// Only the first mesh is returned; use importScene() for all of them.
[[nodiscard]] std::expected<ImportedMesh, SceneIoError> importMesh(
    const std::filesystem::path& path);

/// One bone's influence on a mesh, as the file states it.
struct ImportedBone {
    std::string name;
    /// The inverse bind matrix: mesh space -> this bone's local space.
    foundation::Mat4 offset{};
    /// Parallel arrays, one entry per influenced vertex.
    std::vector<uint32_t> verts;
    std::vector<float> weights;
};

struct ImportedSkin {
    std::vector<ImportedBone> bones;
};

struct ImportedSceneMesh {
    std::string name;  ///< the file's own name for it, or "mesh<N>" if unnamed
    foundation::MeshData mesh;

    /// The mesh's material, when the file carried one.
    ///
    /// Absent means the file had no material for this mesh -- NOT that it had a
    /// default one. A caller applying its own default needs to tell those apart.
    ///
    /// Texture paths come back exactly as the file states them, which is
    /// usually relative to the file. Resolving them is the caller's job,
    /// because only the caller knows where the file came from.
    std::optional<foundation::MaterialDesc> material;

    /// The mesh's skin, when the file carried one.
    ///
    /// Owning, unlike `foundation::SkinView`: an importer has to allocate what
    /// it read, and a view over assimp's scene would dangle the moment the
    /// importer goes out of scope.
    std::optional<ImportedSkin> skin;
};

struct ImportedScene {
    std::vector<ImportedSceneMesh> meshes;

    /// How many METRES one unit of the imported coordinates represents, or 0
    /// when the file does not say.
    ///
    /// **Coordinates are returned in the FILE's units, never converted.** That
    /// is deliberate -- a caller measuring a file (as the unit tests do) needs
    /// the numbers the file actually holds, and silently rescaling would make
    /// that impossible. But a caller feeding an import into the app must
    /// convert, and this is what makes that possible instead of guesswork.
    ///
    /// The internal unit is the **decimetre**, so:
    ///
    ///     decimetres = fileUnits * metersPerUnit * 10
    ///
    /// This matters: our own GLB round-trips a 16.9455 dm human back as
    /// 1.6946, because glTF is metres. Used as-is that is a 17 cm doll --
    /// the same 10x class of error recorded against the reference's FBX
    /// (project_context.md §8).
    ///
    /// Sources, in order of trust:
    ///   * **glTF/GLB — 1.0 by specification.** The format defines metres and
    ///     carries no unit metadata at all (verified: assimp reports none).
    ///   * **FBX — the file's own `UnitScaleFactor`**, which is centimetres per
    ///     unit, so metres per unit is that over 100.
    ///   * anything else — 0, meaning the caller must decide. OBJ and STL are
    ///     genuinely unitless; inventing a number would be worse than saying so.
    double metersPerUnit{0.0};
};

/// Reads EVERY mesh from a file, not just the first.
///
/// This is what makes import symmetrical with export: a dressed character is
/// written as body + one entry per worn proxy, so importing only `mMeshes[0]`
/// silently drops everything the character was wearing.
///
/// Same trust-boundary handling as importMesh -- validation before any step
/// that dereferences indices, and non-finite coordinates refused. A mesh with
/// no triangles is **skipped rather than fatal**: assimp scenes legitimately
/// carry empty or non-triangular helper meshes, and rejecting the whole file
/// for one of them would make many real assets unopenable. The scene is an
/// error only when NOTHING usable came back.
[[nodiscard]] std::expected<ImportedScene, SceneIoError> importScene(
    const std::filesystem::path& path);

}  // namespace mh::io
