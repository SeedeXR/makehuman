// SPDX-License-Identifier: Apache-2.0
//
// The KTX2 container, written from the Khronos specification.
//
// This wraps an ALREADY-ENCODED ETC1S + BasisLZ payload. It does not encode
// anything: the container and the codec are separate problems, and separating
// them is what makes this half testable on its own, against files a real
// encoder produced.

#pragma once

#include <cstdint>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

namespace mh::io {

/// The transfer function the image declares. The values ARE the Khronos Data
/// Format enum values, so nothing has to map them.
///
/// `KHR_texture_basisu` asks for sRGB on colour and linear on non-colour data
/// (a normal map). Measured, the correct one is also the cheaper one: the same
/// normal map encodes to 21,014 bytes linear against 26,204 sRGB.
enum class Ktx2Transfer : uint8_t { Linear = 1, Srgb = 2 };

/// One encoded image, ready to be wrapped.
struct Ktx2Etc1sImage {
    /// Both MUST be multiples of 4 -- `KHR_texture_basisu` requires it, and a
    /// file that breaks it is rejected by consumers rather than fixed up.
    uint32_t width{};
    uint32_t height{};

    /// Whether the payload carries an alpha slice. This is not cosmetic: it
    /// decides the SHAPE of the data format descriptor -- two samples and
    /// `bytesPlane1 = 8` with alpha, one sample and `bytesPlane1 = 0` without.
    bool hasAlpha{};

    Ktx2Transfer transfer{Ktx2Transfer::Srgb};

    /// BasisLZ supercompression global data: the shared endpoint and selector
    /// codebooks, the Huffman tables, and one image descriptor. Required --
    /// supercompression scheme 1 has nowhere else to put them.
    std::span<const uint8_t> globalData;

    /// The single mip level's payload.
    ///
    /// ONE level, deliberately. The reference encoder's default output is one
    /// level, `KHR_texture_basisu` does not require a mip pyramid, and a
    /// multi-level KTX2 stores its levels in REVERSE order (smallest first) --
    /// a trap worth not walking into for a pyramid nothing currently encodes.
    /// ponytail: single level; take the level index to an array when the
    /// encoder actually produces mips.
    std::span<const uint8_t> levelData;
};

/// Wraps @p image in a KTX2 container, or `nullopt` if it could not be one.
///
/// @p writer is recorded under the standard `KTXwriter` key. Callers pass
/// `"MakeHuman C++ KTX2 writer" + provenance.stamp()`; it is a parameter rather
/// than a constant so the round-trip test can reproduce a reference file
/// byte-for-byte, which is the only reason to believe any of this is right.
[[nodiscard]] std::optional<std::vector<uint8_t>> ktx2Write(const Ktx2Etc1sImage& image,
                                                            std::string_view writer);

}  // namespace mh::io
