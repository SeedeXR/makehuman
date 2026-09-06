// SPDX-License-Identifier: Apache-2.0
//
// Written from the published glTF 2.0 specification. There is NO reference
// implementation to compare against -- the Python MakeHuman has no glTF support
// at all (verified: zero matches for gltf/glb across the tree) -- so this is
// validated by spec conformance and by reading the output back with an
// independent library, not by parity.
#pragma once

#include "makehuman/foundation/Geometry.h"

#include "makehuman/io/ObjWriter.h"  // Unit

#include <cstdint>
#include <expected>
#include <filesystem>
#include <span>
#include <string>

namespace mh::io {

struct GltfWriteOptions {
    /// glTF's unit is the **metre**, and MakeHuman's is the decimetre, so the
    /// default converts. Writing decimetres would make every model ten times
    /// too large in every engine that honours the spec.
    Unit unit{Unit::Meter};
    float scale{1.0F};

    bool feetOnGround{false};
    bool writeNormals{true};
    bool writeUVs{true};

    /// Compress geometry with `KHR_draco_mesh_compression`.
    ///
    /// Off by default and it should stay that way: the extension goes in
    /// `extensionsRequired`, so a consumer without a Draco decoder cannot open
    /// the file at all. That is the spec's rule, not a choice -- the geometry
    /// is not present in any other form -- and it makes compression something
    /// the user asks for rather than something we do to them.
    ///
    /// Silently ignored by a build without draco (`io::dracoAvailable()`).
    bool draco{false};

    std::string meshName{"MakeHuman"};
    std::string materialName{"Skin"};
};

/// glTF's JOINTS_0 / WEIGHTS_0 are 4-wide by definition; a set of 4 is one
/// attribute pair. More influences need JOINTS_1/WEIGHTS_1, which almost no
/// engine reads, so 4 is what we write.
inline constexpr uint8_t kGltfInfluences = 4;

enum class GltfWriteErrorKind {
    CannotOpen,
    EmptyMesh,
    TooManyVertices,
    /// A coordinate or material value was NaN or infinite. JSON has no literal
    /// for either, so writing one produces a file no parser will read; we
    /// refuse rather than emit a plausible-looking corrupt asset.
    NonFiniteValue,
    /// The skin does not describe this mesh: wrong vertex count, a joint index
    /// past the end of the skeleton, or an influence count other than 4.
    InvalidSkin,
    /// A morph target's delta array is not parallel to the mesh's vertices.
    InvalidMorphTarget,
    TextureUnsupported,
};

struct GltfWriteError {
    GltfWriteErrorKind kind{};
    std::string file;
    std::string detail;

    [[nodiscard]] std::string message() const;
};

struct GltfWriteResult {
    size_t vertices{};  ///< render (unwelded) vertices
    size_t triangles{};
    size_t fileBytes{};
};

/// One mesh in a multi-mesh GLB: a dressed character is the body plus each worn
/// proxy, each with its own name and material.
struct GltfSceneEntry {
    foundation::RenderView mesh;
    std::string name{"MakeHuman"};
    const foundation::MaterialDesc* material{nullptr};
    /// Only ONE entry in a scene may carry a skin -- see writeGlbScene.
    const foundation::SkinView* skin{nullptr};
    std::span<const foundation::MorphTarget> morphTargets{};
};

/// Writes every entry into one GLB, each as its own mesh and node.
///
/// glTF addresses buffer views, accessors, meshes, nodes and materials by
/// index, so each entry's block is offset by everything written before it.
/// Sharing one accessor block -- the classic multi-mesh glTF bug -- yields
/// several meshes drawn on top of each other.
///
/// **At most one entry may carry a skin.** Joint nodes follow the mesh nodes,
/// so a second skeleton would need its own node block; nothing needs that yet
/// (only the body is rigged) and guessing at it would be untested code. A
/// second skin is refused rather than silently dropped.
///
/// `feetOnGround` levels the whole scene by the lowest point of any entry:
/// levelling each mesh independently would drop the clothes to the floor beside
/// the body.
[[nodiscard]] std::expected<GltfWriteResult, GltfWriteError> writeGlbScene(
    const std::filesystem::path& path, std::span<const GltfSceneEntry> entries,
    const GltfWriteOptions& options = {});

/// Writes @p mesh as a binary glTF (`.glb`).
///
/// The mesh is unwelded and triangulated first: glTF requires one attribute per
/// index and has no quad primitive, whereas MakeHuman stores quads with
/// independent position and UV index spaces.
[[nodiscard]] std::expected<GltfWriteResult, GltfWriteError> writeGlb(
    const std::filesystem::path& path, const foundation::RenderView& mesh,
    const GltfWriteOptions& options = {}, const foundation::MaterialDesc* material = nullptr,
    const foundation::SkinView* skin                      = nullptr,
    std::span<const foundation::MorphTarget> morphTargets = {});

}  // namespace mh::io
