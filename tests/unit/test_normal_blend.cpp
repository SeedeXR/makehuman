// SPDX-License-Identifier: Apache-2.0
//
// Baking a wrinkle sheet into a normal map, for the exports that cannot carry a
// pose-driven one.
//
// The viewport blends the two per fragment. No interchange format has a
// pose-driven normal map -- glTF, FBX and UsdSkel each give a material ONE
// normal texture -- so an export either bakes the blend at the pose it is
// writing, or ships a character whose creases exist only on the screen they
// were authored on.
//
// The arithmetic has to be the SHADER's arithmetic. Two implementations of one
// blend is how the exported file and the viewport come to disagree about a
// character, which is the failure this whole file exists to make loud.
#include "makehuman/foundation/NormalBlend.h"

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <cstdint>
#include <vector>

using namespace mh::foundation;

namespace {

/// A solid RGBA image of one colour.
std::vector<uint8_t> solid(int w, int h, uint8_t r, uint8_t g, uint8_t b) {
    std::vector<uint8_t> px(static_cast<size_t>(w) * static_cast<size_t>(h) * 4);
    for (size_t i = 0; i < px.size(); i += 4) {
        px[i + 0] = r;
        px[i + 1] = g;
        px[i + 2] = b;
        px[i + 3] = 255;
    }
    return px;
}

NormalMapImage viewOf(const std::vector<uint8_t>& px, int w, int h) {
    return NormalMapImage{px, w, h};
}

/// The blend, written out longhand from the shader, as an independent check on
/// the implementation rather than a copy of it.
///
/// litsphere.frag / pbr.frag:
///     tangentSpace = vec3(unpacked.xy * intensity, unpacked.z)
///     tangentSpace = vec3(tangentSpace.xy + w * crease.xy, tangentSpace.z)
/// then the TBN transform normalizes. TBN is orthonormal, so normalizing before
/// the transform gives the same world normal -- which is what lets this be baked
/// into a texture at all.
std::array<uint8_t, 3> expectedPixel(std::array<uint8_t, 3> base, float intensity,
                                     std::array<uint8_t, 3> crease, float weight) {
    auto unpack     = [](uint8_t c) { return 2.0F * (static_cast<float>(c) / 255.0F) - 1.0F; };
    const float bx  = unpack(base[0]) * intensity;
    const float by  = unpack(base[1]) * intensity;
    const float bz  = unpack(base[2]);
    const float x   = bx + weight * unpack(crease[0]);
    const float y   = by + weight * unpack(crease[1]);
    const float len = std::sqrt(x * x + y * y + bz * bz);
    auto pack       = [](float v) {
        const float s = (v * 0.5F + 0.5F) * 255.0F;
        return static_cast<uint8_t>(std::lround(std::clamp(s, 0.0F, 255.0F)));
    };
    return {pack(x / len), pack(y / len), pack(bz / len)};
}

}  // namespace

TEST_CASE("weight zero means the sheet has no influence at all", "[foundation][normalblend]") {
    // The claim is INDEPENDENCE, not "equals the base". Baking unpacks,
    // normalizes and repacks, and a stored normal map is not necessarily unit
    // length -- measured, (130,120,250) has a length of 0.9628, so normalizing
    // it lengthens z from 250 to 255. Five LSB, from a pixel that looks
    // perfectly ordinary.
    //
    // "Within one LSB of the base" was this case's first assertion and it was
    // simply wrong about the data. What weight zero actually promises is that
    // the sheet cannot be seen: two DIFFERENT sheets must bake to the same
    // bytes, which is a stronger statement than either of them matching the
    // base, and one no round-trip error can flatter.
    const auto base = solid(8, 8, 130, 120, 250);
    const auto one  = solid(4, 4, 200, 60, 240);
    const auto two  = solid(4, 4, 30, 210, 90);

    const auto a = bakeWrinkleIntoNormalMap(viewOf(base, 8, 8), 1.0F, viewOf(one, 4, 4), 0.0F);
    const auto b = bakeWrinkleIntoNormalMap(viewOf(base, 8, 8), 1.0F, viewOf(two, 4, 4), 0.0F);
    REQUIRE(a.has_value());
    REQUIRE(b.has_value());
    CHECK(*a == *b);
    CHECK(a->size() == base.size());
}

TEST_CASE("a unit-length base survives the round trip", "[foundation][normalblend]") {
    // The other half of the case above, on data where "unchanged" IS the
    // contract. A flat sheet is (128,128,255), which unpacks to a length of
    // 1.0000077 -- so unpack, normalize and repack has nothing to correct, and
    // the only movement left is 8-bit rounding.
    const auto base   = solid(4, 4, 128, 128, 255);
    const auto crease = solid(4, 4, 200, 60, 240);
    const auto out = bakeWrinkleIntoNormalMap(viewOf(base, 4, 4), 1.0F, viewOf(crease, 4, 4), 0.0F);
    REQUIRE(out.has_value());
    for (size_t i = 0; i < base.size(); i += 4) {
        for (size_t c = 0; c < 3; ++c) {
            INFO("channel " << c << " got " << int((*out)[i + c]) << " base " << int(base[i + c]));
            CHECK(std::abs(int((*out)[i + c]) - int(base[i + c])) <= 1);
        }
        CHECK((*out)[i + 3] == 255);
    }
}

TEST_CASE("the bake reproduces the shader's blend exactly", "[foundation][normalblend]") {
    // THE gate. Every other case here passes under a `mix` blend too -- the map
    // changes, the sizes work, the errors fire. This is the one that says the
    // baked texture and the viewport describe the same surface.
    const std::array<uint8_t, 3> b{130, 120, 250};
    const std::array<uint8_t, 3> c{200, 60, 240};

    for (const float weight : {0.25F, 0.5F, 1.0F}) {
        for (const float intensity : {1.0F, 0.4F}) {
            const auto base   = solid(2, 2, b[0], b[1], b[2]);
            const auto crease = solid(2, 2, c[0], c[1], c[2]);
            const auto out    = bakeWrinkleIntoNormalMap(viewOf(base, 2, 2), intensity,
                                                         viewOf(crease, 2, 2), weight);
            REQUIRE(out.has_value());
            const auto want = expectedPixel(b, intensity, c, weight);
            INFO("weight " << weight << " intensity " << intensity);
            CHECK((*out)[0] == want[0]);
            CHECK((*out)[1] == want[1]);
            CHECK((*out)[2] == want[2]);
        }
    }
}

TEST_CASE("the crease changes the map at full weight", "[foundation][normalblend]") {
    const auto base   = solid(4, 4, 128, 128, 255);
    const auto crease = solid(4, 4, 230, 40, 200);
    const auto out = bakeWrinkleIntoNormalMap(viewOf(base, 4, 4), 1.0F, viewOf(crease, 4, 4), 1.0F);
    REQUIRE(out.has_value());
    bool moved = false;
    for (size_t i = 0; i < base.size(); ++i)
        if ((*out)[i] != base[i]) moved = true;
    CHECK(moved);
}

TEST_CASE("a smaller crease sheet is sampled up to the base's size", "[foundation][normalblend]") {
    // The shipped case, measured: skin_normal.png is 1024x1024 and the fixture
    // sheet is 256x256. Refusing a mismatch would refuse the only pairing that
    // actually exists, and resampling the BASE down would throw away the pores
    // the base is there for -- so the output is base-sized and the sheet is
    // sampled nearest-neighbour into it.
    //
    // Nearest and not bilinear: this is a normal map, and interpolating packed
    // normals across a crease edge invents a slope that is in neither texel.
    std::vector<uint8_t> crease(2 * 2 * 4, 0);
    const uint8_t values[4]{60, 100, 160, 220};
    for (int i = 0; i < 4; ++i) {
        crease[static_cast<size_t>(i) * 4 + 0] = values[i];
        crease[static_cast<size_t>(i) * 4 + 1] = 128;
        crease[static_cast<size_t>(i) * 4 + 2] = 255;
        crease[static_cast<size_t>(i) * 4 + 3] = 255;
    }
    const auto base = solid(4, 4, 128, 128, 255);
    const auto out = bakeWrinkleIntoNormalMap(viewOf(base, 4, 4), 1.0F, viewOf(crease, 2, 2), 1.0F);
    REQUIRE(out.has_value());
    REQUIRE(out->size() == base.size());

    // Each 2x2 quadrant must carry ITS OWN crease texel, and the assertion is
    // on the ORDER rather than on mere inequality.
    //
    // "The four quadrants differ" was this case's first form, and a TRANSPOSED
    // sampler -- reading (y,x) instead of (x,y), the classic way to get this
    // wrong -- passes it: all four texels are still distinct, just in the wrong
    // corners. The sheet's reds ascend 60, 100, 160, 220 in row-major order,
    // and the bake is monotonic in the sheet's red against a flat base, so
    // pinning the ascending order pins WHICH texel landed WHERE.
    auto redAt = [&](int x, int y) {
        return (*out)[(static_cast<size_t>(y) * 4 + static_cast<size_t>(x)) * 4];
    };
    CHECK(redAt(0, 0) < redAt(3, 0));  // sheet (0,0)=60 -> (1,0)=100
    CHECK(redAt(3, 0) < redAt(0, 3));  // sheet (1,0)=100 -> (0,1)=160
    CHECK(redAt(0, 3) < redAt(3, 3));  // sheet (0,1)=160 -> (1,1)=220
    // ...and within a quadrant they agree, which is what "nearest" means.
    CHECK(redAt(0, 0) == redAt(1, 1));
}

TEST_CASE("a larger crease sheet is sampled down", "[foundation][normalblend]") {
    const auto base   = solid(2, 2, 128, 128, 255);
    const auto crease = solid(8, 8, 200, 60, 240);
    const auto out = bakeWrinkleIntoNormalMap(viewOf(base, 2, 2), 1.0F, viewOf(crease, 8, 8), 1.0F);
    REQUIRE(out.has_value());
    CHECK(out->size() == base.size());
}

TEST_CASE("the bake refuses what it cannot describe", "[foundation][normalblend]") {
    const auto base   = solid(4, 4, 128, 128, 255);
    const auto crease = solid(4, 4, 200, 60, 240);

    SECTION("an empty base") {
        const std::vector<uint8_t> none;
        const auto out =
            bakeWrinkleIntoNormalMap(viewOf(none, 0, 0), 1.0F, viewOf(crease, 4, 4), 1.0F);
        REQUIRE_FALSE(out.has_value());
        CHECK(out.error().kind == NormalBlendErrorKind::Empty);
    }

    SECTION("an empty crease sheet") {
        const std::vector<uint8_t> none;
        const auto out =
            bakeWrinkleIntoNormalMap(viewOf(base, 4, 4), 1.0F, viewOf(none, 0, 0), 1.0F);
        REQUIRE_FALSE(out.has_value());
        CHECK(out.error().kind == NormalBlendErrorKind::Empty);
    }

    SECTION("a size that does not match the buffer") {
        // Reading past the buffer would be a heap overflow. ASan would catch it
        // in the suite; a user would not.
        const auto out =
            bakeWrinkleIntoNormalMap(viewOf(base, 64, 64), 1.0F, viewOf(crease, 4, 4), 1.0F);
        REQUIRE_FALSE(out.has_value());
        CHECK(out.error().kind == NormalBlendErrorKind::SizeMismatch);
    }

    SECTION("a weight outside [0, 1]") {
        // `rig::chooseWrinkle` clamps, so this is a caller bug rather than
        // data -- and a caller bug should be loud rather than clamped again.
        for (const float bad : {-0.1F, 1.5F}) {
            const auto out =
                bakeWrinkleIntoNormalMap(viewOf(base, 4, 4), 1.0F, viewOf(crease, 4, 4), bad);
            INFO("weight " << bad);
            REQUIRE_FALSE(out.has_value());
            CHECK(out.error().kind == NormalBlendErrorKind::BadWeight);
        }
    }

    SECTION("a weight that is not a number") {
        const auto out =
            bakeWrinkleIntoNormalMap(viewOf(base, 4, 4), 1.0F, viewOf(crease, 4, 4), std::nanf(""));
        REQUIRE_FALSE(out.has_value());
        CHECK(out.error().kind == NormalBlendErrorKind::BadWeight);
    }
}
