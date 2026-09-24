// SPDX-License-Identifier: Apache-2.0
//
// ETC1S: the 4x4 block format `KHR_texture_basisu` carries.
//
// ETC1S is a SUBSET of ETC1, and the subset was established by measurement
// rather than from prose -- 65,536 blocks of a real `basisu` encode are
// differential mode 100% of the time, flip 0%, colour delta zero 100%, and
// both intensity-table indices equal 100%. So a block is exactly what
// `Etc1sBlock` holds: one base colour, one table index, sixteen selectors.
//
// WHY THIS STAYS, although nothing in `src/` calls it.
//
// It has no production caller and cannot get one: it emits raw ETC1S blocks
// with no BasisLZ codebooks, so it cannot feed a KTX2 container, and libktx
// does that whole job on the live path. The hand-written container writer that
// waited for it was deleted on 2026-09-24 for exactly that reason.
//
// This is kept because it is not dead weight, it is a CONTROL. It is the only
// encode of a given image in this repo that does NOT quantise to shared
// codebooks, and `app_ktx2_holds_the_quality_bar` reads its 40.20 dB to
// explain why libktx's 38.83 dB on the same image is the expected cost of
// codebooks rather than a regression. Delete this and that gate's number
// becomes a figure nobody can account for.
//
// `app_etc1s_holds_the_quality_bar` and `app_etc1s_quality_is_pinned_from_above`
// are what keep the baseline honest, from below and above.

#pragma once

#include <array>
#include <cstdint>
#include <span>
#include <vector>

namespace mh::io {

/// One 4x4 block.
struct Etc1sBlock {
    /// Base colour, 5 bits per channel. Expanded to 8 bits as `(v << 3) | (v >> 2)`.
    uint8_t r5{};
    uint8_t g5{};
    uint8_t b5{};

    /// Which of the eight ETC1 intensity tables this block uses, 0-7.
    uint8_t table{};

    /// Per-pixel, row-major within the block, each 0-3. These are indices into
    /// the intensity table, NOT the two bits ETC1 stores -- the stored value
    /// maps through [2, 3, 1, 0], which serialisation deals with.
    std::array<uint8_t, 16> selectors{};
};

/// The eight ETC1 intensity tables. Public because the codebook work needs the
/// same numbers and two copies would be one too many.
inline constexpr std::array<std::array<int, 4>, 8> kEtc1IntensityTables{{
    {-8, -2, 2, 8},
    {-17, -5, 5, 17},
    {-29, -9, 9, 29},
    {-42, -13, 13, 42},
    {-60, -18, 18, 60},
    {-80, -24, 24, 80},
    {-106, -33, 33, 106},
    {-183, -47, 47, 183},
}};

/// Expands a 5-bit channel to 8 bits the way ETC1 does.
[[nodiscard]] constexpr uint8_t etc1Expand5(uint8_t v) noexcept {
    return static_cast<uint8_t>((v << 3) | (v >> 2));
}

/// Decodes one block to 16 RGB pixels, row-major, 3 bytes each.
///
/// Verified against an INDEPENDENT decoder before any encoder existed: run over
/// the 65,536 blocks of a real `basisu` ETC1 file, this reproduces basisu's own
/// decode with **zero** differing pixels out of 1,048,576. That check is what
/// makes the round-trip quality number below mean anything -- an encoder and
/// decoder written together are self-consistent whether or not they are right.
void etc1sDecodeBlock(const Etc1sBlock& block, std::span<uint8_t, 48> rgb) noexcept;

/// Encodes one 4x4 block of RGB pixels (row-major, 3 bytes each).
///
/// Exhaustive over the eight intensity tables, and for each one it refines the
/// base colour from the selectors it just chose, twice. Not a clever encoder --
/// a clear one whose output quality is measured rather than asserted.
[[nodiscard]] Etc1sBlock etc1sEncodeBlock(std::span<const uint8_t, 48> rgb) noexcept;

/// Encodes a whole image, left to right then top to bottom.
///
/// Width and height must be multiples of 4; anything else returns empty, since
/// `KHR_texture_basisu` requires it and a partial block has no meaning here.
[[nodiscard]] std::vector<Etc1sBlock> etc1sEncode(std::span<const uint8_t> rgb, uint32_t width,
                                                  uint32_t height);

}  // namespace mh::io
