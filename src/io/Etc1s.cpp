// SPDX-License-Identifier: Apache-2.0

#include "makehuman/io/Etc1s.h"

#include <algorithm>

namespace mh::io {
namespace {

constexpr int clamp255(int v) noexcept {
    return v < 0 ? 0 : (v > 255 ? 255 : v);
}

/// The 5-bit base colour nearest an 8-bit one, by expanded value rather than by
/// truncation: `v >> 3` is off by up to 4 because expansion replicates the top
/// bits back into the bottom.
uint8_t nearest5(int v) noexcept {
    const int q = (clamp255(v) * 31 + 127) / 255;
    return static_cast<uint8_t>(q);
}

}  // namespace

void etc1sDecodeBlock(const Etc1sBlock& block, std::span<uint8_t, 48> rgb) noexcept {
    const int base[3] = {etc1Expand5(block.r5), etc1Expand5(block.g5), etc1Expand5(block.b5)};
    const auto& inten = kEtc1IntensityTables[block.table & 7];
    for (size_t i = 0; i < 16; ++i) {
        const int mod = inten[block.selectors[i] & 3];
        for (size_t c = 0; c < 3; ++c)
            rgb[i * 3 + c] = static_cast<uint8_t>(clamp255(base[c] + mod));
    }
}

Etc1sBlock etc1sEncodeBlock(std::span<const uint8_t, 48> rgb) noexcept {
    // Start from the block's mean; it is the best base colour when every pixel
    // ends up on the same modifier, and a good starting point when they do not.
    int sum[3] = {0, 0, 0};
    for (size_t i = 0; i < 16; ++i)
        for (size_t c = 0; c < 3; ++c)
            sum[c] += rgb[i * 3 + c];

    Etc1sBlock best{};
    long bestError = -1;

    for (uint8_t table = 0; table < 8; ++table) {
        const auto& inten = kEtc1IntensityTables[table];
        int cand[3]       = {(sum[0] + 8) / 16, (sum[1] + 8) / 16, (sum[2] + 8) / 16};

        Etc1sBlock block{};
        block.table = table;
        long error  = 0;

        // Two refinement passes: pick selectors for the current base, then move
        // the base to the mean of (pixel - chosen modifier), which is the least
        // squares answer once the selectors are fixed.
        for (int pass = 0; pass < 3; ++pass) {
            const uint8_t r5  = nearest5(cand[0]);
            const uint8_t g5  = nearest5(cand[1]);
            const uint8_t b5  = nearest5(cand[2]);
            const int base[3] = {etc1Expand5(r5), etc1Expand5(g5), etc1Expand5(b5)};

            error       = 0;
            long acc[3] = {0, 0, 0};
            for (size_t i = 0; i < 16; ++i) {
                long bestPixel = -1;
                uint8_t chosen = 0;
                for (uint8_t s = 0; s < 4; ++s) {
                    long e = 0;
                    for (size_t c = 0; c < 3; ++c) {
                        const int d = clamp255(base[c] + inten[s]) - rgb[i * 3 + c];
                        e += static_cast<long>(d) * d;
                    }
                    if (bestPixel < 0 || e < bestPixel) {
                        bestPixel = e;
                        chosen    = s;
                    }
                }
                block.selectors[i] = chosen;
                error += bestPixel;
                for (size_t c = 0; c < 3; ++c)
                    acc[c] += rgb[i * 3 + c] - inten[chosen];
            }
            block.r5 = r5;
            block.g5 = g5;
            block.b5 = b5;

            for (size_t c = 0; c < 3; ++c)
                cand[c] = static_cast<int>((acc[c] + 8) / 16);
        }

        if (bestError < 0 || error < bestError) {
            bestError = error;
            best      = block;
        }
    }
    return best;
}

std::vector<Etc1sBlock> etc1sEncode(std::span<const uint8_t> rgb, uint32_t width, uint32_t height) {
    if (width == 0 || height == 0 || width % 4 != 0 || height % 4 != 0) return {};
    if (rgb.size() != size_t{width} * height * 3) return {};

    const uint32_t bw = width / 4;
    const uint32_t bh = height / 4;
    std::vector<Etc1sBlock> out;
    out.reserve(size_t{bw} * bh);

    std::array<uint8_t, 48> tile{};
    for (uint32_t by = 0; by < bh; ++by) {
        for (uint32_t bx = 0; bx < bw; ++bx) {
            for (uint32_t y = 0; y < 4; ++y) {
                const size_t src = (size_t{by} * 4 + y) * width * 3 + size_t{bx} * 4 * 3;
                std::copy_n(rgb.begin() + static_cast<ptrdiff_t>(src), 12, tile.begin() + y * 12);
            }
            out.push_back(etc1sEncodeBlock(tile));
        }
    }
    return out;
}

}  // namespace mh::io
