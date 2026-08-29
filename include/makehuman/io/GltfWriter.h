// SPDX-License-Identifier: Apache-2.0
//
// Written from the published glTF 2.0 specification. There is NO reference
// implementation to compare against -- the Python MakeHuman has no glTF support
// at all (verified: zero matches for gltf/glb across the tree) -- so this is
// validated by spec conformance and by reading the output back with an
// independent library, not by parity.
#pragma once

#include "makehuman/core/Material.h"
#include "makehuman/core/Mesh.h"
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

enum class GltfWriteErrorKind { CannotOpen, EmptyMesh, TooManyVertices };

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
    const std::filesystem::path& path, const core::Mesh& mesh, const GltfWriteOptions& options = {},
    const core::Material* material = nullptr);

}  // namespace mh::io
