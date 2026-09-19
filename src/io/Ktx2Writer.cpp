// SPDX-License-Identifier: Apache-2.0

#include "makehuman/io/Ktx2Writer.h"

#include <cstring>

namespace mh::io {
namespace {

/// KTX 2.0 §3.1. The twelve bytes every file starts with.
constexpr uint8_t kIdentifier[12] = {0xAB, 0x4B, 0x54, 0x58, 0x20, 0x32,
                                     0x30, 0xBB, 0x0D, 0x0A, 0x1A, 0x0A};

/// Khronos Data Format enum values, taken from
/// `KTX-Software/external/dfdutils/KHR/khr_df.h` rather than from memory.
constexpr uint32_t kDfdVersion1_3 = 2;  // the ENUM is 2, not 13 and not 3
constexpr uint8_t kModelEtc1s     = 163;
constexpr uint8_t kPrimariesBt709 = 1;
constexpr uint8_t kChannelRgb     = 0;
constexpr uint8_t kChannelAaa     = 15;

/// ETC1S is a 4x4 block format, and a plane is 8 bytes per block.
constexpr uint8_t kBytesPerPlane = 8;

/// Supercompression scheme 1 = BasisLZ.
constexpr uint32_t kSchemeBasisLz = 1;

void put8(std::vector<uint8_t>& out, uint8_t v) {
    out.push_back(v);
}

void put32(std::vector<uint8_t>& out, uint32_t v) {
    for (int i = 0; i < 4; ++i)
        out.push_back(static_cast<uint8_t>(v >> (8 * i)));
}

void put64(std::vector<uint8_t>& out, uint64_t v) {
    for (int i = 0; i < 8; ++i)
        out.push_back(static_cast<uint8_t>(v >> (8 * i)));
}

void putBytes(std::vector<uint8_t>& out, std::span<const uint8_t> b) {
    out.insert(out.end(), b.begin(), b.end());
}

constexpr size_t alignUp(size_t v, size_t to) {
    return (v + to - 1) / to * to;
}

/// One 16-byte DFD sample.
void putSample(std::vector<uint8_t>& out, uint16_t bitOffset, uint8_t channelType) {
    put8(out, static_cast<uint8_t>(bitOffset & 0xFF));
    put8(out, static_cast<uint8_t>(bitOffset >> 8));
    // STORED MINUS ONE. A 64-bit channel is written as 63. Reading the
    // reference settled this: its alpha sample is `40 00 3F 0F`, which as the
    // 16-bit `bitLength` a spec summary described would be a nonsensical 3904.
    put8(out, 64 - 1);
    put8(out, channelType);
    for (int i = 0; i < 4; ++i)
        put8(out, 0);         // samplePosition0..3
    put32(out, 0);            // sampleLower
    put32(out, 0xFFFFFFFFu);  // sampleUpper
}

}  // namespace

std::optional<std::vector<uint8_t>> ktx2Write(const Ktx2Etc1sImage& image,
                                              std::string_view writer) {
    // `KHR_texture_basisu` requires multiples of 4, and scheme 1 has nowhere
    // but the global data to keep its codebooks. Refuse rather than emit a
    // file a consumer will reject for reasons that point nowhere near here.
    if (image.width == 0 || image.height == 0) return std::nullopt;
    if (image.width % 4 != 0 || image.height % 4 != 0) return std::nullopt;
    if (image.globalData.empty() || image.levelData.empty()) return std::nullopt;

    const uint32_t samples = image.hasAlpha ? 2u : 1u;

    // ---- sizes, before anything is written --------------------------------
    // Offsets go in the index, which precedes what it points at, so every
    // length has to be known up front.
    const size_t dfdBlockSize = 24 + 16 * size_t{samples};
    const size_t dfdLen       = 4 + dfdBlockSize;

    // One key/value entry: `KTXwriter`. Both the key and the string value are
    // NUL-terminated, and the entry is then padded to a 4-byte boundary --
    // padding that COUNTS toward kvdByteLength.
    const std::string_view key = "KTXwriter";
    const size_t entryLen      = key.size() + 1 + writer.size() + 1;
    const size_t kvdLen        = alignUp(4 + entryLen, 4);

    const size_t dfdOffset = sizeof kIdentifier + 36 + 32 + 24;  // header, index, 1 level
    const size_t kvdOffset = dfdOffset + dfdLen;
    // The global data is 8-byte aligned; the padding that achieves it belongs
    // to neither section, which is why it is implied by the offset rather than
    // counted in kvdByteLength.
    const size_t sgdOffset = alignUp(kvdOffset + kvdLen, 8);
    // Level alignment is 1 for a supercompressed file. Not assumed: all three
    // reference encodes put their level at 8925, 931 and 797, and none of
    // those is even 4-aligned.
    const size_t levelOffset = sgdOffset + image.globalData.size();

    std::vector<uint8_t> out;
    out.reserve(levelOffset + image.levelData.size());

    // ---- identifier and header --------------------------------------------
    putBytes(out, kIdentifier);
    // 0 = VK_FORMAT_UNDEFINED. ETC1S carries its format in the DFD's colour
    // model, NOT here; a real Vulkan format would contradict the payload.
    put32(out, 0);
    put32(out, 1);  // typeSize
    put32(out, image.width);
    put32(out, image.height);
    put32(out, 0);  // pixelDepth: 0 for a 2D image
    put32(out, 0);  // layerCount: 0 for non-array
    put32(out, 1);  // faceCount
    put32(out, 1);  // levelCount
    put32(out, kSchemeBasisLz);

    // ---- index ------------------------------------------------------------
    put32(out, static_cast<uint32_t>(dfdOffset));
    put32(out, static_cast<uint32_t>(dfdLen));
    put32(out, static_cast<uint32_t>(kvdOffset));
    put32(out, static_cast<uint32_t>(kvdLen));
    put64(out, sgdOffset);
    put64(out, image.globalData.size());

    // ---- level index ------------------------------------------------------
    put64(out, levelOffset);
    put64(out, image.levelData.size());
    // 0, NOT the inflated size. For a BasisLZ level the uncompressed length is
    // not meaningful -- the payload transcodes to a format chosen at load time,
    // so there is no single size to declare.
    put64(out, 0);

    // ---- data format descriptor -------------------------------------------
    put32(out, static_cast<uint32_t>(dfdLen));  // dfdTotalSize, this field included
    put32(out, 0);                              // vendorId 0 | descriptorType 0
    put32(out, kDfdVersion1_3 | (static_cast<uint32_t>(dfdBlockSize) << 16));
    put32(out, uint32_t{kModelEtc1s} | (uint32_t{kPrimariesBt709} << 8) |
                   (static_cast<uint32_t>(image.transfer) << 16));  // flags 0
    // 4x4, STORED MINUS ONE -- so a 4x4 block reads 3,3. The two unused
    // dimensions stay 0, which already means "1" under the same rule.
    put32(out, 3u | (3u << 8));
    put32(out, uint32_t{kBytesPerPlane} |
                   (image.hasAlpha ? uint32_t{kBytesPerPlane} << 8 : 0u));  // planes 0..3
    put32(out, 0);                                                          // planes 4..7
    putSample(out, 0, kChannelRgb);
    if (image.hasAlpha) putSample(out, 64, kChannelAaa);

    // ---- key/value data ---------------------------------------------------
    put32(out, static_cast<uint32_t>(entryLen));
    putBytes(out, {reinterpret_cast<const uint8_t*>(key.data()), key.size()});
    put8(out, 0);
    putBytes(out, {reinterpret_cast<const uint8_t*>(writer.data()), writer.size()});
    put8(out, 0);
    out.resize(kvdOffset + kvdLen, 0);  // valuePadding

    // ---- supercompression global data, then the level ---------------------
    out.resize(sgdOffset, 0);  // sgdPadding
    putBytes(out, image.globalData);
    putBytes(out, image.levelData);
    return out;
}

}  // namespace mh::io
