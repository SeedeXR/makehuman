// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include "makehuman/core/Mesh.h"

#include <expected>
#include <filesystem>
#include <string>

namespace mh::core {

enum class ObjErrorKind {
    NotFound,
    Unreadable,
    LooseVertex,      ///< a vertex referenced by no face
    BadIndex,         ///< face index out of range or unparseable
    MalformedVertex,  ///< a v/vt line with missing or non-numeric components
    DegenerateFace,   ///< an f statement with fewer than 3 corners
    MixedPrimitives,  ///< faces with more than 4 corners
    EmptyMesh,
    InvalidTopology,  ///< face arrays rejected by Mesh::setFaces
};

struct ObjError {
    ObjErrorKind kind{};
    std::string file;
    uint32_t line{};
    std::string detail;

    [[nodiscard]] std::string message() const;
};

/// Reads a Wavefront OBJ into a Mesh.
///
/// Scope deliberately matches the reference reader
/// (legacy-python/shared/wavefront.py:47-151), because the base mesh and every
/// proxy mesh are authored against it:
///
///  * `v`, `vt`, `f`, `g` are handled. `o` sets the mesh **name** and creates
///    no face group, matching wavefront.py:128-129.
///  * A malformed `v`/`vt` line is an error, not a skipped line: dropping it
///    would shift every subsequent index and silently produce a different mesh.
///  * `vn` is **ignored** — the reference does not read normals and assumes
///    smooth shading (wavefront.py:50-52). Normals are always recomputed.
///  * `usemtl` is ignored (wavefront.py:125-126); materials come from `.mhmat`.
///  * Triangles are stored as degenerate quads by repeating corner 0
///    (wavefront.py:105-106), so `vertsPerPrimitive` stays 4.
///  * A loose vertex is an error, as in the reference (wavefront.py:132-142).
///
/// Indices may be negative (relative to the end), per the OBJ specification.
[[nodiscard]] std::expected<Mesh, ObjError> loadObj(const std::filesystem::path& path);

}  // namespace mh::core
