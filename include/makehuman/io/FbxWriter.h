// SPDX-License-Identifier: Apache-2.0
//
// FBX 7.x binary export, written from the published record layout.
//
// **Provenance, because this format attracts the question.** No Autodesk FBX
// SDK is linked -- `CLAUDE.md` hard rule 6 forbids it as a dependency and
// `LICENSING.md` §5.1 records it, with Maya and Blender, as a VALIDATOR only.
// No GPL exporter is translated either. What those tools contributed is files:
// `tools/fbxdump.py` reads them byte by byte, and the layout below was learned
// by walking output from Maya's FBX SDK and from assimp and taking the parts
// they agree on.

#pragma once

#include "makehuman/foundation/Geometry.h"
#include "makehuman/io/Transform.h"

#include <cstdint>
#include <expected>
#include <filesystem>
#include <string>

namespace mh::io {

struct FbxWriteOptions {
    /// FBX's conventional unit is the centimetre, and every DCC assumes it
    /// unless the file says otherwise. `GlobalSettings.UnitScaleFactor` says
    /// otherwise, and is written to match.
    Unit unit{Unit::Centimeter};
    float scale{1.0F};
    bool feetOnGround{false};

    std::string meshName{"MakeHuman"};
    std::string materialName{"Skin"};
};

enum class FbxWriteErrorKind : uint8_t {
    CannotOpen,
    /// No vertices or no faces. A zero-vertex FBX parses cleanly and imports as
    /// nothing, so it is refused rather than written.
    EmptyMesh,
    /// A coordinate was NaN or infinite. FBX stores doubles raw, so unlike JSON
    /// this would produce a readable file with a poisoned mesh in it.
    NonFiniteValue,
};

struct FbxWriteError {
    FbxWriteErrorKind kind{};
    std::string file;
    std::string detail;

    [[nodiscard]] std::string message() const;
};

struct FbxWriteResult {
    size_t vertices{};
    /// FBX stores polygons of any size, but a `RenderView` is already a
    /// triangle list -- so a quad that reached it as two triangles is written
    /// as two. This writer does not try to weld them back.
    size_t polygons{};
    size_t bytes{};
};

/// Writes @p mesh as a binary FBX.
///
/// Stage 1 writes the container and the geometry. Normals, UVs, materials and
/// the skin follow, and each is validated by Maya AND Blender opening the file
/// rather than by our own reader agreeing with our own writer.
[[nodiscard]] std::expected<FbxWriteResult, FbxWriteError> writeFbx(
    const std::filesystem::path& path, const foundation::RenderView& mesh,
    const FbxWriteOptions& options = {}, const foundation::MaterialDesc* material = nullptr);

}  // namespace mh::io
