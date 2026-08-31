// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "makehuman/foundation/Geometry.h"
#include "makehuman/io/ObjWriter.h"  // Unit, unitScale

#include <expected>
#include <filesystem>
#include <span>
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
    /// ambiguity to inherit. Derived from `unit` rather than set separately:
    /// two independent knobs for the same physical fact is how a file ends up
    /// claiming metres while holding centimetres.
    [[nodiscard]] double metersPerUnit() const noexcept;
    /// Ours is a Y-up world. USD allows "Y" or "Z" and stores which, so a
    /// consumer does not have to guess -- unlike BVH.
    bool yUp{true};

    /// The same Unit every other writer takes. It was a bare float here, which
    /// meant USD was the one exporter whose scale could not be set the same way
    /// as the rest -- exactly the inconsistency M7 exists to remove.
    Unit unit{Unit::Meter};
    float scale{1.0F};  ///< extra scale on top of the unit conversion

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

/// One mesh in a USD stage: a dressed character is the body plus each worn
/// proxy, each its own Mesh prim so a DCC tool can select them apart.
struct UsdSceneEntry {
    foundation::RenderView mesh;
    std::string name{"mesh"};
};

/// Writes every entry as its own `Mesh` prim under one `Xform`.
///
/// A USD stage is a scene graph, so this is its natural shape -- no index
/// arithmetic, unlike OBJ and glTF. `extent` is per prim, as USD expects.
///
/// **No materials.** This writer has never emitted any, for one mesh or many;
/// a `UsdSceneEntry` therefore carries no `MaterialDesc`, rather than accepting
/// one and silently ignoring it.
[[nodiscard]] std::expected<UsdWriteResult, UsdWriteError> writeUsdaScene(
    const std::filesystem::path& path, std::span<const UsdSceneEntry> entries,
    const UsdWriteOptions& options = {});

[[nodiscard]] std::expected<UsdWriteResult, UsdWriteError> writeUsda(
    const std::filesystem::path& path, const foundation::RenderView& mesh,
    const UsdWriteOptions& options = {});

}  // namespace mh::io
