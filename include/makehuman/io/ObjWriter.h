// SPDX-License-Identifier: Apache-2.0
//
// Written from the published Wavefront OBJ format, not translated from the
// AGPL reference. See LICENSING.md section 4 for why this module is
// Apache-2.0 and what that requires.
#pragma once

#include "makehuman/foundation/Geometry.h"
#include "makehuman/io/Transform.h"  // Unit, unitScale, Transform

#include <cstdint>
#include <expected>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace mh::io {

struct ObjWriteOptions {
    Unit unit{Unit::Decimeter};
    /// Extra scale on top of the unit conversion.
    float scale{1.0F};

    /// Translate so the lowest point sits at y = 0.
    ///
    /// The reference offsets by a named "ground" joint and applies it on Y only
    /// regardless of orientation, which it flags as wrong in its own TODO
    /// (core/export.py:102-109). Using the mesh's actual minimum is both
    /// simpler and correct for any orientation.
    bool feetOnGround{false};

    bool writeNormals{true};
    bool writeUVs{true};

    /// Emit a sibling `.mtl` and reference it. Ignored when no material is given.
    bool writeMaterial{true};

    /// Faces whose mask byte is 0 are skipped. Empty means "write everything".
    ///
    /// Applies to `writeObj` only: a scene carries a mask and a name per entry,
    /// since one setting cannot describe several meshes. `writeObjScene`
    /// ignores both fields.
    std::span<const uint8_t> faceMask;

    std::string objectName{"mesh"};
};

enum class ObjWriteErrorKind { CannotOpen, EmptyMesh, MaskSizeMismatch, InconsistentMaterials };

struct ObjWriteError {
    ObjWriteErrorKind kind{};
    std::string file;
    std::string detail;

    [[nodiscard]] std::string message() const;
};

struct ObjWriteResult {
    size_t vertices{};
    size_t uvs{};
    size_t faces{};    ///< faces actually written
    size_t skipped{};  ///< faces omitted by the mask
    bool wroteMtl{false};
};

/// One mesh in a multi-mesh OBJ: a dressed character is the body plus each worn
/// proxy, and each keeps its own name, material and face mask.
struct ObjSceneEntry {
    foundation::MeshView mesh;
    std::string name{"mesh"};
    const foundation::MaterialDesc* material{nullptr};
    /// Faces whose mask byte is 0 are skipped. Empty means "write everything".
    std::span<const uint8_t> faceMask;
};

/// Writes every entry into one OBJ, each as its own `g` group.
///
/// OBJ indices are file-global and 1-based, so each entry's faces are offset by
/// the vertices, UVs and normals already written. Getting that wrong is the
/// classic multi-mesh OBJ bug -- every mesh after the first draws the first
/// one's geometry.
///
/// Attributes are emitted per entry rather than all-positions-then-all-UVs.
/// Both are valid OBJ, and per-entry keeps the offset arithmetic local; for a
/// single entry the output is byte-identical to writeObj, which a test pins.
[[nodiscard]] std::expected<ObjWriteResult, ObjWriteError> writeObjScene(
    const std::filesystem::path& path, std::span<const ObjSceneEntry> entries,
    const ObjWriteOptions& options = {});

/// Writes @p mesh as Wavefront OBJ.
///
/// Indices are 1-based, as the format requires. A quad mesh is written as
/// quads; a mesh whose quads are degenerate triangles is written as triangles,
/// following `vertsPerFaceForExport`.
[[nodiscard]] std::expected<ObjWriteResult, ObjWriteError> writeObj(
    const std::filesystem::path& path, const foundation::MeshView& mesh,
    const ObjWriteOptions& options = {}, const foundation::MaterialDesc* material = nullptr);

}  // namespace mh::io
