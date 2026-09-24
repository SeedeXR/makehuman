// SPDX-License-Identifier: Apache-2.0
//
// ETC1S + BasisLZ encoding, via libktx.
//
// libktx emits a COMPLETE KTX2 file: the ETC1S blocks, the BasisLZ
// supercompression -- codebooks, Huffman tables, range coder -- and the
// container around them.
//
// There was once a hand-written container writer here too, `Ktx2Writer.h`. It
// wrapped an ALREADY-supercompressed payload, and nothing in this repo has
// ever produced one: `etc1sEncode` emits raw ETC1S blocks with no BasisLZ
// codebooks, so the writer's required `globalData` was never assigned on any
// path and it returned `nullopt` every time. The two halves could not be
// joined without writing a BasisLZ encoder, which is the bulk of basisu. It
// was deleted on 2026-09-24 rather than kept as a "fallback if libktx is
// dropped", because it could not have been one -- see `memory/todo.md`. Git
// has it if a dependency-free container is ever wanted again.

#pragma once

#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace mh::io {

/// The transfer function the image declares. The values ARE the Khronos Data
/// Format enum values, so nothing has to map them.
///
/// `KHR_texture_basisu` asks for sRGB on colour and linear on non-colour data
/// (a normal map). Measured, the correct one is also the cheaper one: the same
/// normal map encodes to 21,014 bytes linear against 26,204 sRGB.
///
/// Lived in `Ktx2Writer.h` until that file was deleted on 2026-09-24; it is
/// the one piece of it the live path actually used.
enum class Ktx2Transfer : uint8_t { Linear = 1, Srgb = 2 };

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
