// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Ethnic skin blending, against `legacy/python/apps/autoskinblender.py`.
//
// This is what `autoBlendSkin true` in a `.mhmat` selects, and it is how ONE
// set of assets becomes a range of skin tones -- the mechanism behind varied
// human colouring, rather than shipping a texture per tone.

#include "makehuman/core/SkinTone.h"

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <span>
#include <vector>

using namespace mh::core;

namespace {

std::vector<uint8_t> flat(size_t n, uint8_t v) {
    return std::vector<uint8_t>(n, v);
}

std::vector<uint8_t> blend(const std::vector<uint8_t>& c, const std::vector<uint8_t>& a,
                           const std::vector<uint8_t>& s, EthnicWeights w) {
    const std::array<std::span<const uint8_t>, 3> in{c, a, s};
    auto r = blendEthnicLitsphere(in, w);
    REQUIRE(r.has_value());
    return *r;
}

}  // namespace

TEST_CASE("the ethnic diffuse colour matches the reference", "[core][skintone]") {
    // autoskinblender.py:46-48 and :116-118.
    const auto cauc = ethnicDiffuseColor({1.0F, 0.0F, 0.0F});
    CHECK(std::abs(cauc.x - 0.843F) < 1e-6F);
    CHECK(std::abs(cauc.y - 0.639F) < 1e-6F);
    CHECK(std::abs(cauc.z - 0.517F) < 1e-6F);

    const auto afr = ethnicDiffuseColor({0.0F, 1.0F, 0.0F});
    CHECK(std::abs(afr.x - 0.207F) < 1e-6F);
    CHECK(std::abs(afr.y - 0.113F) < 1e-6F);
    CHECK(std::abs(afr.z - 0.066F) < 1e-6F);

    const auto asi = ethnicDiffuseColor({0.0F, 0.0F, 1.0F});
    CHECK(std::abs(asi.x - 0.721F) < 1e-6F);
    CHECK(std::abs(asi.y - 0.568F) < 1e-6F);
    CHECK(std::abs(asi.z - 0.431F) < 1e-6F);

    // An even mix is the mean of the three, which is the whole point: the tone
    // moves continuously with the sliders instead of snapping between presets.
    const float t  = 1.0F / 3.0F;
    const auto mid = ethnicDiffuseColor({t, t, t});
    CHECK(std::abs(mid.x - (0.843F + 0.207F + 0.721F) / 3.0F) < 1e-5F);
    CHECK(std::abs(mid.y - (0.639F + 0.113F + 0.568F) / 3.0F) < 1e-5F);
    CHECK(std::abs(mid.z - (0.517F + 0.066F + 0.431F) / 3.0F) < 1e-5F);

    // Distinct tones, or the feature does nothing.
    CHECK(afr.x < mid.x);
    CHECK(mid.x < cauc.x);
}

TEST_CASE("a single ethnicity returns its litsphere unmixed", "[core][skintone]") {
    // The reference takes the image itself here (`len(blends) == 1`), with no
    // rounding pass. Sending it through the mixer instead would shift bytes.
    const auto c   = flat(16, 200);
    const auto a   = flat(16, 10);
    const auto s   = flat(16, 90);
    const auto out = blend(c, a, s, {1.0F, 0.0F, 0.0F});
    REQUIRE(out.size() == 16);
    for (const uint8_t v : out)
        CHECK(v == 200);
}

TEST_CASE("two ethnicities use BOTH weights, not a lerp", "[core][skintone]") {
    // mix(img1, img2, w1, w2) with w2 supplied -- so 0.25*a + 0.25*b, which is
    // NOT 0.25*a + 0.75*b. Getting this wrong still produces a plausible skin.
    const auto c   = flat(8, 100);
    const auto a   = flat(8, 200);
    const auto s   = flat(8, 0);
    const auto out = blend(c, a, s, {0.25F, 0.25F, 0.0F});
    // 0.25*100 + 0.25*200 + 0.5 = 75.5 -> 75
    for (const uint8_t v : out)
        CHECK(v == 75);
}

TEST_CASE("three ethnicities fold the third in at weight 1.0", "[core][skintone]") {
    // mix(acc, third, 1.0, w3): the accumulator is NOT re-weighted.
    const auto c   = flat(8, 90);
    const auto a   = flat(8, 30);
    const auto s   = flat(8, 60);
    const float t  = 1.0F / 3.0F;
    const auto out = blend(c, a, s, {t, t, t});
    // acc = 1/3*90 + 1/3*30 + 0.5 = 40.5 -> 40
    // out = 1.0*40 + 1/3*60 + 0.5 = 60.5 -> 60
    for (const uint8_t v : out)
        CHECK(v == 60);
}

TEST_CASE("blending refuses inputs it cannot combine", "[core][skintone]") {
    const auto big   = flat(16, 1);
    const auto small = flat(8, 1);
    const std::array<std::span<const uint8_t>, 3> mismatched{big, small, big};
    CHECK_FALSE(blendEthnicLitsphere(mismatched, {0.5F, 0.5F, 0.0F}).has_value());

    // All weights zero: the reference would index blends[0] and raise.
    const std::array<std::span<const uint8_t>, 3> ok{big, big, big};
    const auto none = blendEthnicLitsphere(ok, {0.0F, 0.0F, 0.0F});
    REQUIRE_FALSE(none.has_value());
    CHECK(none.error() == SkinBlendErrorKind::NoWeight);
}
