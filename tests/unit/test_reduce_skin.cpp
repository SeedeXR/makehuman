// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Collapsing a skin onto a smaller skeleton, tested on a hand-built rig where
// the right answer is known by construction. No character, no .mhskel, no
// weights file -- the question is whether the arithmetic is right, and a real
// rig would make a failure ambiguous between "the maths is wrong" and "the
// data changed".
#include "makehuman/rig/ReduceSkin.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

using namespace mh::rig;
using Catch::Approx;

namespace {

/// root -> spine -> chest -> arm, with `spine` deliberately UNMAPPED so the
/// interesting case -- an influence with no counterpart -- is the default.
SkinData sourceRig() {
    SkinData s;
    s.jointNames   = {"root", "spine", "chest", "arm"};
    s.jointParents = {-1, 0, 1, 2};
    s.globalRest.assign(4, mh::foundation::Mat4{});
    // Row 3, column 0 -- an arbitrary but identifiable slot, so a matrix taken
    // from the wrong source bone is visible rather than plausible.
    for (size_t i = 0; i < 4; ++i)
        s.globalRest[i].m[3][0] = static_cast<float>(i);
    s.influences = 4;
    return s;
}

RetargetMap mapping() {
    RetargetMap m;
    m.toBone = {{"Hips", "root"}, {"Chest", "chest"}, {"Arm", "arm"}};  // no "spine"
    return m;
}

int32_t indexOf(const SkinData& s, const std::string& name) {
    for (size_t i = 0; i < s.jointNames.size(); ++i) {
        if (s.jointNames[i] == name) return static_cast<int32_t>(i);
    }
    return -1;
}

}  // namespace

TEST_CASE("the reduced skeleton keeps only the mapped bones", "[reduceskin]") {
    SkinData s   = sourceRig();
    s.joints     = {0, 0, 0, 0};
    s.weights    = {1.0F, 0.0F, 0.0F, 0.0F};
    const auto r = reduceSkin(s, mapping());
    REQUIRE(r.has_value());
    CHECK(r->jointNames.size() == 3);
    CHECK(indexOf(*r, "Hips") >= 0);
    CHECK(indexOf(*r, "Chest") >= 0);
    CHECK(indexOf(*r, "Arm") >= 0);
    CHECK(indexOf(*r, "spine") == -1);
}

TEST_CASE("an unmapped bone's influence goes to its nearest mapped ancestor", "[reduceskin]") {
    // THE CASE THAT MATTERS. `spine` is not in the map, but the vertices it
    // holds still have to be driven by something. Dropping it leaves a ring of
    // the torso weighted to nothing, which renders as a collapsed waist rather
    // than as an error -- so it must land on `root`, its nearest mapped
    // ancestor, and NOT be discarded.
    SkinData s   = sourceRig();
    s.joints     = {1, 0, 0, 0};  // all of it on `spine`
    s.weights    = {1.0F, 0.0F, 0.0F, 0.0F};
    const auto r = reduceSkin(s, mapping());
    REQUIRE(r.has_value());
    CHECK(r->joints[0] == static_cast<uint32_t>(indexOf(*r, "Hips")));
    CHECK(r->weights[0] == Approx(1.0));
}

TEST_CASE("weights that collapse onto one bone are SUMMED", "[reduceskin]") {
    // `root` and `spine` both become `Hips`. Keeping the last instead of
    // adding them would throw away half this vertex.
    SkinData s   = sourceRig();
    s.joints     = {0, 1, 0, 0};
    s.weights    = {0.4F, 0.6F, 0.0F, 0.0F};
    const auto r = reduceSkin(s, mapping());
    REQUIRE(r.has_value());
    const auto hips = static_cast<uint32_t>(indexOf(*r, "Hips"));
    CHECK(r->joints[0] == hips);
    CHECK(r->weights[0] == Approx(1.0));
    CHECK(r->weights[1] == Approx(0.0));
}

TEST_CASE("the hierarchy squeezes out the unmapped bones", "[reduceskin]") {
    // `chest`'s parent is `spine`, which does not survive. Taking the source's
    // parent directly would leave Chest parented to nothing; it must reparent
    // to Hips.
    SkinData s   = sourceRig();
    s.joints     = {0, 0, 0, 0};
    s.weights    = {1.0F, 0.0F, 0.0F, 0.0F};
    const auto r = reduceSkin(s, mapping());
    REQUIRE(r.has_value());
    const int32_t hips  = indexOf(*r, "Hips");
    const int32_t chest = indexOf(*r, "Chest");
    const int32_t arm   = indexOf(*r, "Arm");
    CHECK(r->jointParents[static_cast<size_t>(hips)] == -1);
    CHECK(r->jointParents[static_cast<size_t>(chest)] == hips);
    CHECK(r->jointParents[static_cast<size_t>(arm)] == chest);
}

TEST_CASE("a parent always precedes its children", "[reduceskin]") {
    // Not cosmetic: glTF and FBX both require it, and an unordered_map's
    // iteration order would break it at random.
    SkinData s   = sourceRig();
    s.joints     = {0, 0, 0, 0};
    s.weights    = {1.0F, 0.0F, 0.0F, 0.0F};
    const auto r = reduceSkin(s, mapping());
    REQUIRE(r.has_value());
    for (size_t i = 0; i < r->jointParents.size(); ++i) {
        CHECK(r->jointParents[i] < static_cast<int32_t>(i));
    }
}

TEST_CASE("the bind and posed matrices come from the source bone", "[reduceskin]") {
    SkinData s   = sourceRig();
    s.globalPose = s.globalRest;
    for (auto& m : s.globalPose)
        m.m[3][1] = 7.0F;
    s.joints     = {0, 0, 0, 0};
    s.weights    = {1.0F, 0.0F, 0.0F, 0.0F};
    const auto r = reduceSkin(s, mapping());
    REQUIRE(r.has_value());
    // "arm" sat at x = 3 in the source; "Arm" must still.
    CHECK(r->globalRest[static_cast<size_t>(indexOf(*r, "Arm"))].m[3][0] == Approx(3.0));
    REQUIRE(r->globalPose.size() == r->globalRest.size());
    CHECK(r->globalPose[static_cast<size_t>(indexOf(*r, "Arm"))].m[3][1] == Approx(7.0));
}

TEST_CASE("weights are renormalised after truncation", "[reduceskin]") {
    SkinData s   = sourceRig();
    s.influences = 2;
    s.joints     = {0, 2, 0, 0};
    s.weights    = {0.25F, 0.25F, 0.0F, 0.0F};  // sums to 0.5, not 1
    s.joints.resize(2);
    s.weights.resize(2);
    const auto r = reduceSkin(s, mapping());
    REQUIRE(r.has_value());
    float total = 0.0F;
    for (size_t k = 0; k < 2; ++k)
        total += r->weights[k];
    CHECK(total == Approx(1.0));
}

TEST_CASE("a map describing another rig is refused, not silently emptied", "[reduceskin]") {
    SkinData s = sourceRig();
    s.joints   = {0, 0, 0, 0};
    s.weights  = {1.0F, 0.0F, 0.0F, 0.0F};
    RetargetMap other;
    other.toBone = {{"Hips", "pelvis_that_does_not_exist"}};
    CHECK_FALSE(reduceSkin(s, other).has_value());
    CHECK_FALSE(reduceSkin(SkinData{}, mapping()).has_value());
}
