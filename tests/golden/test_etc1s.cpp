// SPDX-License-Identifier: Apache-2.0
//
// ETC1S block encoding, checked where it can be checked exactly.
//
// The quality of the encoder is a PSNR number and is gated separately, on a
// real texture. What belongs here is everything that has a right answer: that
// the decoder reproduces a hand-built block, that a block the encoder can
// represent EXACTLY comes back exactly, and that the refusals refuse.
//
// The decoder itself was validated against an independent implementation
// before any of this existed -- run over the 65,536 blocks of a real `basisu`
// ETC1 file it reproduced basisu's own decode with zero differing samples out
// of 3,145,728. That is what makes a round-trip number meaningful at all: an
// encoder and a decoder written together agree with each other whether or not
// either is right.

#include "makehuman/io/Etc1s.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdlib>
#include <vector>

using namespace mh::io;

namespace {

/// A flat 4x4 tile of one colour.
std::array<uint8_t, 48> flatTile(uint8_t r, uint8_t g, uint8_t b) {
    std::array<uint8_t, 48> t{};
    for (size_t i = 0; i < 16; ++i) {
        t[i * 3]     = r;
        t[i * 3 + 1] = g;
        t[i * 3 + 2] = b;
    }
    return t;
}

}  // namespace

TEST_CASE("a hand-built block decodes to the modifier it names", "[etc1s]") {
    Etc1sBlock block{};
    block.r5    = 21;  // expands to (21<<3)|(21>>2) = 173
    block.g5    = 21;
    block.b5    = 20;  // expands to 165
    block.table = 0;   // {-8, -2, 2, 8}
    block.selectors.fill(0);
    block.selectors[5] = 3;  // the large positive modifier

    std::array<uint8_t, 48> rgb{};
    etc1sDecodeBlock(block, rgb);

    CHECK(etc1Expand5(21) == 173);
    CHECK(etc1Expand5(20) == 165);
    // pixel 0 took modifier index 0, which is -8
    CHECK(rgb[0] == 165);
    CHECK(rgb[2] == 157);
    // pixel 5 took index 3, which is +8
    CHECK(rgb[5 * 3] == 181);
    CHECK(rgb[5 * 3 + 2] == 173);
}

TEST_CASE("clamping happens at the ends of the range", "[etc1s]") {
    Etc1sBlock block{};
    block.r5    = 31;  // 255
    block.g5    = 0;   // 0
    block.b5    = 31;
    block.table = 7;  // {-183, -47, 47, 183}
    block.selectors.fill(3);
    std::array<uint8_t, 48> rgb{};
    etc1sDecodeBlock(block, rgb);
    CHECK(rgb[0] == 255);  // 255 + 183 clamps, it does not wrap
    CHECK(rgb[1] == 183);
    block.selectors.fill(0);
    etc1sDecodeBlock(block, rgb);
    CHECK(rgb[0] == 72);
    CHECK(rgb[1] == 0);  // 0 - 183 clamps to 0
}

TEST_CASE("a colour the format can represent survives exactly", "[etc1s]") {
    // 173 is what 5-bit 21 expands to, so a flat tile of it needs no
    // approximation at all and must come back untouched.
    const auto tile  = flatTile(173, 173, 165);
    const auto block = etc1sEncodeBlock(tile);
    std::array<uint8_t, 48> back{};
    etc1sDecodeBlock(block, back);
    CHECK(back == tile);
}

TEST_CASE("the encoder picks a table that spans the block", "[etc1s]") {
    // Half the tile dark, half light: a block that needs a WIDE modifier, so a
    // correct encoder must not settle for table 0.
    std::array<uint8_t, 48> tile{};
    for (size_t i = 0; i < 16; ++i) {
        const uint8_t v = i < 8 ? 40 : 210;
        tile[i * 3] = tile[i * 3 + 1] = tile[i * 3 + 2] = v;
    }
    const auto block = etc1sEncodeBlock(tile);
    CHECK(block.table >= 5);

    std::array<uint8_t, 48> back{};
    etc1sDecodeBlock(block, back);
    long worst = 0;
    for (size_t i = 0; i < back.size(); ++i)
        worst = std::max(worst, std::abs(long(back[i]) - long(tile[i])));
    INFO("worst channel error " << worst);
    CHECK(worst <= 20);
}

TEST_CASE("whole-image encoding covers every block once", "[etc1s]") {
    const uint32_t w = 16, h = 8;
    std::vector<uint8_t> img(size_t{w} * h * 3);
    for (size_t i = 0; i < img.size(); ++i)
        img[i] = static_cast<uint8_t>(i * 7);
    const auto blocks = etc1sEncode(img, w, h);
    REQUIRE(blocks.size() == size_t{w / 4} * (h / 4));
}

TEST_CASE("the encoder refuses what ETC1S cannot describe", "[etc1s]") {
    const std::vector<uint8_t> ok(size_t{8} * 8 * 3, 128);
    REQUIRE_FALSE(etc1sEncode(ok, 8, 8).empty());  // the control

    // These carry a buffer of EXACTLY the right length for the size given, so
    // the dimension guard is the only thing that can reject them. Sized wrong,
    // the buffer-length guard fires first and the test passes whether or not
    // the multiple-of-4 rule is enforced at all -- which is how a mutation
    // deleting that rule survived the first version of this test.
    const std::vector<uint8_t> six_by_eight(size_t{6} * 8 * 3, 128);
    const std::vector<uint8_t> eight_by_six(size_t{8} * 6 * 3, 128);
    CHECK(etc1sEncode(six_by_eight, 6, 8).empty());  // width not a multiple of 4
    CHECK(etc1sEncode(eight_by_six, 8, 6).empty());  // height not a multiple of 4
    CHECK(etc1sEncode(ok, 0, 8).empty());            // zero width
    CHECK(etc1sEncode(ok, 8, 0).empty());            // zero height
    CHECK(etc1sEncode(ok, 12, 8).empty());           // buffer too small for the size given
}
