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

/// Writes @p mesh as a binary glTF (`.glb`).
///
/// The mesh is unwelded and triangulated first: glTF requires one attribute per
/// index and has no quad primitive, whereas MakeHuman stores quads with
/// independent position and UV index spaces.
[[nodiscard]] std::expected<GltfWriteResult, GltfWriteError> writeGlb(
    const std::filesystem::path& path, const foundation::RenderView& mesh,
    const GltfWriteOptions& options = {}, const foundation::MaterialDesc* material = nullptr,
    const foundation::SkinView* skin = nullptr);

}  // namespace mh::io
