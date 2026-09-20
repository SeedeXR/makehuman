// SPDX-License-Identifier: Apache-2.0
//
// ETC1S + BasisLZ encoding, via libktx.
//
// This is the half `Ktx2Writer.h` deliberately does not do. That file wraps an
// already-encoded payload and says so; producing the payload needs a BasisLZ
// encoder -- the codebooks, the Huffman tables, the range coder -- and this
// one is libktx's, not ours. libktx emits a COMPLETE KTX2 file, so on this
// path it supersedes our container writer rather than feeding it.

#pragma once

#include <cstdint>
#include <optional>
#include <span>
#include <vector>

#include "makehuman/io/Ktx2Writer.h"  // Ktx2Transfer

namespace mh::io {

/// Whether this build can encode KTX2 at all.
///
/// OPTIONAL on the same terms as draco (`io::dracoAvailable()`): a build
/// without libktx writes textures uncompressed rather than failing, and
/// nothing may declare `KHR_texture_basisu` without an encoder behind it.
/// libktx is not in Homebrew, so unlike draco it is fetched rather than found,
/// and `MH_WITH_KTX2` defaults OFF -- see `src/io/CMakeLists.txt`.
[[nodiscard]] bool ktx2Available() noexcept;

/// Encodes 8-bit RGBA into a complete KTX2 file carrying ETC1S + BasisLZ.
///
/// @param rgba   tightly packed, 4 bytes per pixel, `width * height * 4` long.
/// @param width  MUST be a multiple of 4 -- `KHR_texture_basisu` requires it,
/// @param height and a file that breaks the rule is rejected by consumers
///               rather than fixed up, so this refuses rather than pads.
/// @param transfer sRGB for colour, Linear for data such as a normal map.
///        Measured on the same normal map: linear encodes to 21,014 bytes
///        against 26,204 for sRGB, so the correct choice is also the cheaper.
///
/// @return the KTX2 bytes, or `nullopt` if this build has no encoder, the
///         dimensions break the multiple-of-4 rule, the span length does not
///         match, or libktx reports a failure. Never a partially written file.
[[nodiscard]] std::optional<std::vector<uint8_t>> ktx2EncodeEtc1s(std::span<const uint8_t> rgba,
                                                                  uint32_t width, uint32_t height,
                                                                  Ktx2Transfer transfer);

}  // namespace mh::io
