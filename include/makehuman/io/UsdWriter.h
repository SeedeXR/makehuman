// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "makehuman/foundation/Geometry.h"

#include <expected>
#include <filesystem>
#include <string>

namespace mh::io {

/// USD ASCII (`.usda`) writer.
///
/// Written from the published USD format rather than by linking OpenUSD.
/// OpenUSD is a very large build for what a mesh export needs, and it carries
/// Pixar's modified Apache-2.0 with a trademark clause; `.usda` is documented
/// text, so writing it directly keeps this on the permissive side with no new
/// dependency. assimp is no help here -- it has no USD support at all, neither
/// import nor export (verified against the linked build's format list).
///
/// The output is validated against Blender's USD importer, which is a third
/// implementation with no stake in our conventions.

struct UsdWriteOptions {
    /// USD records real-world scale explicitly, so unlike OBJ there is no
    /// ambiguity to inherit. 1.0 means the points are metres.
    double metersPerUnit{1.0};
    /// Ours is a Y-up world. USD allows "Y" or "Z" and stores which, so a
    /// consumer does not have to guess -- unlike BVH.
    bool yUp{true};

    float scale{0.1F};  ///< decimetres to metres, matching the glTF default
    bool writeNormals{true};
    bool writeUVs{true};

    std::string primName{"MakeHuman"};
};

enum class UsdWriteErrorKind { CannotOpen, EmptyMesh, NonFiniteValue };

struct UsdWriteError {
    UsdWriteErrorKind kind{};
    std::string file;
    std::string detail;

    [[nodiscard]] std::string message() const;
};

struct UsdWriteResult {
    size_t vertices{};
    size_t triangles{};
    size_t fileBytes{};
};

[[nodiscard]] std::expected<UsdWriteResult, UsdWriteError> writeUsda(
    const std::filesystem::path& path, const foundation::RenderView& mesh,
    const UsdWriteOptions& options = {});

}  // namespace mh::io
