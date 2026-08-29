// SPDX-License-Identifier: Apache-2.0
//
// Written from the published Wavefront OBJ format, not translated from the
// AGPL reference. See LICENSING.md section 4 for why this module is
// Apache-2.0 and what that requires.
#pragma once

#include "makehuman/core/Material.h"
#include "makehuman/core/Mesh.h"
#include "makehuman/core/Types.h"

#include <cstdint>
#include <expected>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace mh::io {

/// Length units an export can be written in.
///
/// MakeHuman's internal unit is the **decimetre**
/// (legacy/python/apps/human.py:694-699 computes `heightCm = 10 * bboxY`), and
/// the factors below are the ones its export UI offers
/// (apps/gui/guiexport.py:124-129).
enum class Unit : uint8_t { Decimeter, Meter, Centimeter, Inch };

[[nodiscard]] float unitScale(Unit u) noexcept;
[[nodiscard]] std::string_view unitName(Unit u) noexcept;

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
    std::span<const uint8_t> faceMask;

    std::string objectName{"mesh"};
};

enum class ObjWriteErrorKind { CannotOpen, EmptyMesh, MaskSizeMismatch };

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

/// Writes @p mesh as Wavefront OBJ.
///
/// Indices are 1-based, as the format requires. A quad mesh is written as
/// quads; a mesh whose quads are degenerate triangles is written as triangles,
/// following `vertsPerFaceForExport`.
[[nodiscard]] std::expected<ObjWriteResult, ObjWriteError> writeObj(
    const std::filesystem::path& path, const core::Mesh& mesh, const ObjWriteOptions& options = {},
    const core::Material* material = nullptr);

}  // namespace mh::io
