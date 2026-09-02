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

    /// Level the stage so the lowest point of any entry sits at y = 0.
    ///
    /// USD was the one exporter without this, which made it the one that could
    /// not be set like the rest -- exactly the inconsistency the `unit` comment
    /// above says M7 exists to remove. Measured before it existed: the app's
    /// exports put the origin at hip height, feet at -0.82 m and head at
    /// +0.84 m, so a character arrived buried to the waist.
    ///
    /// One offset for the whole SCENE, like every other writer: levelling each
    /// mesh alone would drop the clothes to the floor beside the body.
    bool feetOnGround{false};

    std::string primName{"MakeHuman"};
};

enum class UsdWriteErrorKind {
    CannotOpen,
    EmptyMesh,
    NonFiniteValue,
    /// A blend shape's deltas are not parallel to its mesh, or more than one
    /// entry carries a set.
    InvalidMorphTarget,
};

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
    const foundation::MaterialDesc* material{nullptr};
    /// UsdSkel blend shapes for this entry. Only ONE entry may carry a set, the
    /// same rule the skin and the glTF/FBX writers follow: a worn proxy is
    /// re-fitted to the body rather than blended.
    ///
    /// **Any set at all makes the root a `SkelRoot`**, skeleton or not.
    /// `usdchecker` rejects `SkelBindingAPI` on a prim not rooted at one -- and
    /// Blender imports that invalid stage without complaint, so it cannot be
    /// the thing that tells you.
    std::span<const foundation::MorphTarget> morphTargets{};
};

/// Writes every entry as its own `Mesh` prim under one `Xform`.
///
/// A USD stage is a scene graph, so this is its natural shape -- no index
/// arithmetic, unlike OBJ and glTF. `extent` is per prim, as USD expects.
///
/// Materials become `UsdPreviewSurface` shaders under a single `Looks` scope,
/// bound per mesh. A scene with no materials emits no `Looks` scope at all, so
/// its output is exactly what it was before materials existed.
///
/// A `diffuseTexture` is referenced by asset path and **copied beside the
/// stage**, the same rule the OBJ writer follows for `map_Kd`: a stage naming a
/// texture that is not there is a broken file.
/// @param skin optional UsdSkel binding. When present the root becomes a
///        `SkelRoot`, a `Skeleton` prim is emitted, and the **first** entry is
///        bound to it -- the same "one skin per scene" rule glTF export
///        follows, because only the body is rigged.
[[nodiscard]] std::expected<UsdWriteResult, UsdWriteError> writeUsdaScene(
    const std::filesystem::path& path, std::span<const UsdSceneEntry> entries,
    const UsdWriteOptions& options = {}, const foundation::SkinView* skin = nullptr);

[[nodiscard]] std::expected<UsdWriteResult, UsdWriteError> writeUsda(
    const std::filesystem::path& path, const foundation::RenderView& mesh,
    const UsdWriteOptions& options = {});

/// Writes the scene as a **USDZ**: one self-contained file, stage plus textures.
///
/// USDZ is a zip with two rules that are not optional, both verified against a
/// reference archive produced by Apple's own `usdzip`:
///
///   * every entry is **STORED**, never deflated -- a consumer memory-maps the
///     archive and reads the stage in place, so compressed data is unreadable;
///   * every entry's DATA must begin on a **64-byte boundary**, padded through
///     the zip extra field with header id `0x1986` (that is what `usdzip`
///     emits: id 0x1986, size 22, zero payload, giving 30 + 8 + 26 = 64).
///
/// The stage is the FIRST entry, which is how a reader finds it.
///
/// Nothing here re-implements the stage: `writeUsdaScene` already copies each
/// texture beside the stage and references it by filename, so a written stage
/// and its siblings are exactly the set to package.
[[nodiscard]] std::expected<UsdWriteResult, UsdWriteError> writeUsdzScene(
    const std::filesystem::path& path, std::span<const UsdSceneEntry> entries,
    const UsdWriteOptions& options = {}, const foundation::SkinView* skin = nullptr);

}  // namespace mh::io
