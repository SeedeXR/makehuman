// SPDX-License-Identifier: Apache-2.0
//
// Draco compression of a glTF primitive.

#pragma once

#include "makehuman/foundation/Geometry.h"

#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace mh::io {

/// One primitive, compressed, plus what `KHR_draco_mesh_compression` needs to
/// describe it.
struct DracoBuffer {
    /// The encoded bitstream, to be written as its own glTF bufferView.
    std::vector<uint8_t> bytes;

    /// glTF attribute name -> the draco attribute's unique id, which is
    /// literally the `attributes` map the extension carries. The names are
    /// glTF's ("POSITION"), not draco's, because that is what a consumer looks
    /// them up by.
    std::vector<std::pair<std::string, uint32_t>> attributes;
};

/// Whether this build can compress at all.
///
/// Draco is OPTIONAL, like assimp: a build without it still writes glTF, just
/// uncompressed. Reported rather than assumed so a stale CMake cache cannot
/// leave the writer declaring an extension nothing encoded.
[[nodiscard]] bool dracoAvailable() noexcept;

/// Compresses @p mesh, or `nullopt` if this build has no Draco.
///
/// Every attribute the view carries goes in. That is not thoroughness, it is
/// the extension's rule: a consumer reading the compressed buffer has nowhere
/// else to get an attribute from, so one left out is one the file no longer
/// has.
///
/// LOSSY, deliberately and only where it is safe. Positions, normals and UVs
/// are quantised -- 14, 10 and 12 bits, the values the glTF ecosystem
/// converged on -- and nothing else is, so anything added later that carries
/// indices or weights stays exact by default.
[[nodiscard]] std::optional<DracoBuffer> dracoEncode(const foundation::RenderView& mesh);

}  // namespace mh::io
