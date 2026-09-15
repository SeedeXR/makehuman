// SPDX-License-Identifier: AGPL-3.0-or-later
//
// The similarity function optimised centres of rotation are built on
// (Le & Hodgins 2016).
//
// There is no reference implementation to compare against -- the Python
// original has no CoR skinning -- so every case here follows from the
// DEFINITION rather than from a recorded output, which is directive 12.7's
// analytic oracle.
#include "makehuman/rig/CentersOfRotation.h"

#include "makehuman/rig/Skinning.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <array>
#include <cmath>
#include <cstdint>
#include <numbers>
#include <vector>

using Catch::Matchers::WithinAbs;
using namespace mh;

namespace {
constexpr float kSigma = 0.1F;
}  // namespace

TEST_CASE("a rigid vertex is similar to nothing, including itself", "[rig][cor]") {
    // Every term of the sum needs two DIFFERENT bones that BOTH vertices are
    // attached to. One bone at weight 1 offers no such pair, so the sum is
    // empty. This is the case `computeCentersOfRotation` has to notice: a
    // rigid vertex has no blending artefact, so it has no centre to find, and
    // dividing by this zero would put it at the origin.
    const std::array<uint32_t, 1> bone{7};
    const std::array<float, 1> full{1.0F};
    CHECK_THAT(rig::weightSimilarity(bone, full, bone, full, kSigma), WithinAbs(0.0, 1e-9));
}

TEST_CASE("vertices sharing fewer than two bones are not similar", "[rig][cor]") {
    // The pruning rule, stated as a property rather than as an optimisation:
    // if this were non-zero for a single shared bone, the precompute could not
    // skip the overwhelming majority of the mesh.
    const std::array<uint32_t, 2> ab{0, 1};
    const std::array<uint32_t, 2> cd{2, 3};
    const std::array<uint32_t, 2> bc{1, 2};
    const std::array<float, 2> half{0.5F, 0.5F};

    INFO("no bones in common");
    CHECK_THAT(rig::weightSimilarity(ab, half, cd, half, kSigma), WithinAbs(0.0, 1e-9));
    INFO("exactly ONE bone in common");
    CHECK_THAT(rig::weightSimilarity(ab, half, bc, half, kSigma), WithinAbs(0.0, 1e-9));
}

TEST_CASE("identical weights give the largest similarity", "[rig][cor]") {
    // The exponent vanishes only when the two weight RATIOS agree, so a vertex
    // is most similar to one skinned exactly like it. Checked as an ordering,
    // not a magnitude: the magnitude is a function of sigma and the weights
    // and pinning it would pin the formula to itself.
    const std::array<uint32_t, 2> ab{0, 1};
    const std::array<float, 2> even{0.5F, 0.5F};
    const std::array<float, 2> lopsided{0.9F, 0.1F};

    const float same      = rig::weightSimilarity(ab, even, ab, even, kSigma);
    const float different = rig::weightSimilarity(ab, even, ab, lopsided, kSigma);
    CHECK(same > 0.0F);
    CHECK(same > different);
}

TEST_CASE("similarity does not depend on the order of the two vertices", "[rig][cor]") {
    // s(p,v) == s(v,p). The sum is symmetric under swapping p and v -- the
    // exponent's bracket only changes sign, and it is squared. Worth pinning
    // because the precompute visits each pair once and relies on it.
    const std::array<uint32_t, 2> ab{0, 1};
    const std::array<float, 2> even{0.5F, 0.5F};
    const std::array<float, 2> lopsided{0.8F, 0.2F};
    CHECK_THAT(static_cast<double>(rig::weightSimilarity(ab, even, ab, lopsided, kSigma)),
               WithinAbs(static_cast<double>(rig::weightSimilarity(ab, lopsided, ab, even, kSigma)),
                         1e-9));
}

TEST_CASE("a smaller sigma is less forgiving of a ratio mismatch", "[rig][cor]") {
    // sigma is the only tunable, and this is what it tunes. Without this case
    // it could be ignored entirely and every test above would still pass.
    const std::array<uint32_t, 2> ab{0, 1};
    const std::array<float, 2> even{0.5F, 0.5F};
    const std::array<float, 2> lopsided{0.9F, 0.1F};
    const float tight = rig::weightSimilarity(ab, even, ab, lopsided, 0.05F);
    const float loose = rig::weightSimilarity(ab, even, ab, lopsided, 0.5F);
    CHECK(loose > tight);
}

// ------------------------------------------------------------ the precompute

namespace {

/// A unit square in the XY plane as two triangles.
///
/// Its area-weighted centroid is exactly (0.5, 0.5, 0) -- both triangles have
/// area 0.5 and centroids (2/3,1/3) and (1/3,2/3), which average to the centre.
/// That exact number is what makes the "everything is equally similar" case
/// below an analytic oracle rather than a recorded output.
struct Square {
    std::vector<foundation::Vec3> rest{
        {0.0F, 0.0F, 0.0F}, {1.0F, 0.0F, 0.0F}, {1.0F, 1.0F, 0.0F}, {0.0F, 1.0F, 0.0F}};
    std::vector<uint32_t> tris{0, 1, 2, 0, 2, 3};
};

/// Every vertex bound to the same two bones at the same weights.
rig::CompiledWeights evenlyShared(size_t vertices) {
    rig::CompiledWeights w;
    w.influences = 2;
    for (size_t i = 0; i < vertices; ++i) {
        w.boneIndex.push_back(0);
        w.boneIndex.push_back(1);
        w.weight.push_back(0.5F);
        w.weight.push_back(0.5F);
    }
    return w;
}

}  // namespace

TEST_CASE("a rigid mesh leaves every centre at its rest position", "[rig][cor]") {
    // The zero-denominator case, which is the one that would put the whole mesh
    // at the origin if it were handled by dividing anyway.
    const Square sq;
    rig::CompiledWeights rigid;
    rigid.influences = 1;
    for (size_t i = 0; i < sq.rest.size(); ++i) {
        rigid.boneIndex.push_back(3);
        rigid.weight.push_back(1.0F);
    }

    const auto centers = rig::computeCentersOfRotation(sq.rest, sq.tris, rigid);
    REQUIRE(centers.size() == sq.rest.size());
    for (size_t i = 0; i < centers.size(); ++i) {
        INFO("vertex " << i);
        CHECK_THAT(static_cast<double>(centers[i].x),
                   WithinAbs(static_cast<double>(sq.rest[i].x), 1e-6));
        CHECK_THAT(static_cast<double>(centers[i].y),
                   WithinAbs(static_cast<double>(sq.rest[i].y), 1e-6));
    }
}

TEST_CASE("when every vertex bends alike, every centre is the mesh centroid", "[rig][cor]") {
    // THE analytic case. If all vertices carry identical weights then s is the
    // same constant for every (vertex, triangle) pair, so it cancels top and
    // bottom and the formula collapses to the area-weighted centroid of the
    // whole surface -- (0.5, 0.5, 0) for a unit square, exactly.
    //
    // This pins the CENTROID -- using vertex positions instead gives a
    // different answer -- but NOT the area weighting: a square's two triangles
    // have EQUAL areas, so dropping the area term leaves 0.5 unchanged.
    // Measured: that mutation survived this case. `Wedge` below is the one
    // that kills it, and this comment used to claim otherwise.
    const Square sq;
    const auto centers =
        rig::computeCentersOfRotation(sq.rest, sq.tris, evenlyShared(sq.rest.size()));
    REQUIRE(centers.size() == sq.rest.size());
    for (size_t i = 0; i < centers.size(); ++i) {
        INFO("vertex " << i);
        CHECK_THAT(static_cast<double>(centers[i].x), WithinAbs(0.5, 1e-5));
        CHECK_THAT(static_cast<double>(centers[i].y), WithinAbs(0.5, 1e-5));
        CHECK_THAT(static_cast<double>(centers[i].z), WithinAbs(0.0, 1e-6));
    }
}

TEST_CASE("mismatched inputs give nothing rather than a wrong centre", "[rig][cor]") {
    const Square sq;
    // Not a whole number of triangles.
    const std::vector<uint32_t> ragged{0, 1, 2, 0};
    CHECK(rig::computeCentersOfRotation(sq.rest, ragged, evenlyShared(sq.rest.size())).empty());
    // Weights for a different mesh.
    CHECK(
        rig::computeCentersOfRotation(sq.rest, sq.tris, evenlyShared(sq.rest.size() + 1)).empty());
}

// -------------------------------------------------------------- the runtime

namespace {

foundation::Mat4 rotationAboutY(double radians) {
    foundation::Mat4 m = foundation::Mat4::identity();
    m.m[0][0]          = static_cast<float>(std::cos(radians));
    m.m[0][2]          = static_cast<float>(std::sin(radians));
    m.m[2][0]          = static_cast<float>(-std::sin(radians));
    m.m[2][2]          = static_cast<float>(std::cos(radians));
    return m;
}

double radiusXZ(const foundation::Vec3& v) {
    const double x = static_cast<double>(v.x);
    const double z = static_cast<double>(v.z);
    return std::sqrt((x * x) + (z * z));
}

}  // namespace

TEST_CASE("a rigidly bound vertex gets exactly linear blend skinning", "[rig][cor]") {
    // The reduction that makes CoR safe to turn on everywhere: when the centre
    // IS the rest position -- which is what a rigid vertex gets -- the formula
    // collapses to R(v-v) + LBS(v) = LBS(v). Not approximately; identically.
    const std::vector<foundation::Vec3> rest{{1.0F, 2.0F, 3.0F}, {-4.0F, 0.5F, 2.0F}};
    rig::CompiledWeights w;
    w.influences = 1;
    for (size_t i = 0; i < rest.size(); ++i) {
        w.boneIndex.push_back(0);
        w.weight.push_back(1.0F);
    }
    foundation::Mat4 move = rotationAboutY(0.7);
    move.m[0][3]          = 5.0F;
    move.m[1][3]          = -2.0F;
    const std::vector<foundation::Mat4> skinning{move};

    std::vector<foundation::Vec3> lbs;
    std::vector<foundation::Vec3> cor;
    REQUIRE(rig::skinPositions(rest, w, skinning, lbs));
    REQUIRE(rig::skinPositionsCor(rest, w, skinning, rest, cor));
    for (size_t i = 0; i < rest.size(); ++i) {
        INFO("vertex " << i);
        CHECK_THAT(static_cast<double>(cor[i].x), WithinAbs(static_cast<double>(lbs[i].x), 1e-5));
        CHECK_THAT(static_cast<double>(cor[i].y), WithinAbs(static_cast<double>(lbs[i].y), 1e-5));
        CHECK_THAT(static_cast<double>(cor[i].z), WithinAbs(static_cast<double>(lbs[i].z), 1e-5));
    }
}

TEST_CASE("a twisted limb keeps its volume under CoR too", "[rig][cor]") {
    // The same ring tests/unit/test_dqs.cpp uses, where LBS pinches to 0.0 and
    // DQS holds 2.0. CoR must hold it as well -- it blends the rotation the
    // same way; what differs is the pivot.
    //
    // With every centre at the origin the answer is exact: the blend of
    // identity and a 180-degree turn about Y is a 90-degree turn about Y, and
    // rotating a ring about its own axis preserves every radius.
    constexpr double kR = 2.0;
    std::vector<foundation::Vec3> ring;
    for (int i = 0; i < 12; ++i) {
        const double a = 2.0 * std::numbers::pi * i / 12.0;
        ring.push_back(foundation::Vec3{static_cast<float>(kR * std::cos(a)), 0.0F,
                                        static_cast<float>(kR * std::sin(a))});
    }
    rig::CompiledWeights w;
    w.influences = 2;
    for (size_t i = 0; i < ring.size(); ++i) {
        w.boneIndex.push_back(0);
        w.boneIndex.push_back(1);
        w.weight.push_back(0.5F);
        w.weight.push_back(0.5F);
    }
    const std::vector<foundation::Mat4> skinning{foundation::Mat4::identity(),
                                                 rotationAboutY(std::numbers::pi)};
    const std::vector<foundation::Vec3> centers(ring.size(), foundation::Vec3{0.0F, 0.0F, 0.0F});

    std::vector<foundation::Vec3> lbs;
    std::vector<foundation::Vec3> cor;
    REQUIRE(rig::skinPositions(ring, w, skinning, lbs));
    REQUIRE(rig::skinPositionsCor(ring, w, skinning, centers, cor));

    double worstLbs = kR;
    double worstCor = kR;
    for (size_t i = 0; i < ring.size(); ++i) {
        worstLbs = std::min(worstLbs, radiusXZ(lbs[i]));
        worstCor = std::min(worstCor, radiusXZ(cor[i]));
    }
    INFO("smallest radius: LBS " << worstLbs << ", CoR " << worstCor << ", rest " << kR);
    CHECK(worstLbs < 0.01);
    CHECK_THAT(worstCor, WithinAbs(kR, 1e-4));
}

TEST_CASE("CoR refuses inputs that do not line up", "[rig][cor]") {
    const std::vector<foundation::Vec3> rest{{0.0F, 0.0F, 0.0F}, {1.0F, 0.0F, 0.0F}};
    rig::CompiledWeights w;
    w.influences = 1;
    for (size_t i = 0; i < rest.size(); ++i) {
        w.boneIndex.push_back(0);
        w.weight.push_back(1.0F);
    }
    const std::vector<foundation::Mat4> skinning{foundation::Mat4::identity()};
    std::vector<foundation::Vec3> out;

    // One centre short: silently reusing the last one would deform the tail of
    // the mesh about the wrong pivot.
    const std::vector<foundation::Vec3> tooFew{rest[0]};
    CHECK_FALSE(rig::skinPositionsCor(rest, w, skinning, tooFew, out));
    // A weight naming a bone the pose does not have.
    rig::CompiledWeights bad = w;
    bad.boneIndex[0]         = 9;
    CHECK_FALSE(rig::skinPositionsCor(rest, bad, skinning, rest, out));
}

TEST_CASE("the area weighting is a weighting, not a formality", "[rig][cor]") {
    // Two triangles of DELIBERATELY different area. The square above cannot
    // tell the area term from its absence -- its halves are equal -- so this
    // is the case that does.
    //
    //   small = (0,0) (1,0) (0,1)        area 0.5, centroid (1/3, 1/3)
    //   large = (1,0) (0,1) (3,3)        area 2.5, centroid (4/3, 4/3)
    //
    // Area-weighted: (0.5*(1/3) + 2.5*(4/3)) / 3.0 = 7/6 = 1.1667.
    // Unweighted it would be the mean of the two centroids, 5/6 = 0.8333.
    // Every vertex carries the same weights, so the similarity cancels and the
    // answer is exactly one of those two numbers.
    const std::vector<foundation::Vec3> rest{
        {0.0F, 0.0F, 0.0F}, {1.0F, 0.0F, 0.0F}, {0.0F, 1.0F, 0.0F}, {3.0F, 3.0F, 0.0F}};
    const std::vector<uint32_t> tris{0, 1, 2, 1, 2, 3};

    rig::CompiledWeights w;
    w.influences = 2;
    for (size_t i = 0; i < rest.size(); ++i) {
        w.boneIndex.push_back(0);
        w.boneIndex.push_back(1);
        w.weight.push_back(0.5F);
        w.weight.push_back(0.5F);
    }

    const auto centers = rig::computeCentersOfRotation(rest, tris, w);
    REQUIRE(centers.size() == rest.size());
    for (size_t i = 0; i < centers.size(); ++i) {
        INFO("vertex " << i << " -- 7/6 with the area weighting, 5/6 without");
        CHECK_THAT(static_cast<double>(centers[i].x), WithinAbs(7.0 / 6.0, 1e-5));
        CHECK_THAT(static_cast<double>(centers[i].y), WithinAbs(7.0 / 6.0, 1e-5));
    }
}

TEST_CASE("two rotations blend the short way round", "[rig][cor]") {
    // The quaternion sign correction, and finding a case that actually TESTS
    // it took three attempts. Both failures are worth recording, because both
    // were confident guesses about what `quaternionFromMatrix` returns:
    //
    //   identity vs 180 deg -- dot is exactly 0, so no flip is ever needed.
    //   +170 deg vs -170 deg -- MEASURED, those come back as
    //     (0.0872, 0, 0.9962, 0) and (-0.0872, 0, 0.9962, 0), dot +0.98.
    //     The extraction does NOT normalise w to be positive; it flips w and
    //     keeps the axis, so opposing turns do not oppose as quaternions.
    //
    // A genuinely negative dot needs 170 vs 270 degrees: (0.0872, 0, 0.9962, 0)
    // against (0.7071, 0, -0.7071, 0), dot -0.64. Corrected, the blend is a
    // 220-degree turn -- midway, the short way. Uncorrected it is 40 degrees,
    // which is not between them at all, and lands the point on the opposite
    // side.
    const std::vector<foundation::Vec3> rest{{1.0F, 0.0F, 0.0F}};
    rig::CompiledWeights w;
    w.influences     = 2;
    w.boneIndex      = {0, 1};
    w.weight         = {0.5F, 0.5F};
    const double deg = std::numbers::pi / 180.0;
    const std::vector<foundation::Mat4> skinning{rotationAboutY(170.0 * deg),
                                                 rotationAboutY(270.0 * deg)};
    const std::vector<foundation::Vec3> centers{{0.0F, 0.0F, 0.0F}};

    std::vector<foundation::Vec3> cor;
    REQUIRE(rig::skinPositionsCor(rest, w, skinning, centers, cor));
    // cos(220 deg) = -0.766 corrected; cos(40 deg) = +0.766 without.
    INFO("landed at x = " << cor[0].x << " (-0.766 corrected, +0.766 not)");
    CHECK_THAT(static_cast<double>(cor[0].x), WithinAbs(-0.766, 5e-3));
}

TEST_CASE("the precompute gives the same answer at any thread count", "[rig][cor]") {
    // The precompute is split across threads, so this is the property that
    // keeps it honest: one vertex's centre depends on nothing but shared const
    // data, and each thread writes only the slot it claimed. Nothing is summed
    // ACROSS vertices, which is what would make the float result depend on the
    // order threads happened to finish in.
    //
    // BIT-IDENTICAL, not approximately equal. A precompute that drifted with
    // the core count would make every downstream golden result machine-
    // dependent, and the drift would be small enough to look like noise.
    //
    // A wedge, because it has vertices with genuinely different neighbourhoods.
    const std::vector<foundation::Vec3> rest{{0.0F, 0.0F, 0.0F},
                                             {1.0F, 0.0F, 0.0F},
                                             {0.0F, 1.0F, 0.0F},
                                             {3.0F, 3.0F, 0.0F},
                                             {-2.0F, 1.0F, 0.5F}};
    const std::vector<uint32_t> tris{0, 1, 2, 1, 2, 3, 0, 2, 4};
    rig::CompiledWeights w;
    w.influences = 2;
    for (size_t i = 0; i < rest.size(); ++i) {
        w.boneIndex.push_back(0);
        w.boneIndex.push_back(1);
        w.weight.push_back(i % 2 == 0 ? 0.7F : 0.4F);
        w.weight.push_back(i % 2 == 0 ? 0.3F : 0.6F);
    }

    const auto one = rig::computeCentersOfRotation(rest, tris, w, 0.1F, 1);
    REQUIRE(one.size() == rest.size());
    for (const unsigned n : {2U, 4U, 8U}) {
        const auto many = rig::computeCentersOfRotation(rest, tris, w, 0.1F, n);
        REQUIRE(many.size() == one.size());
        for (size_t i = 0; i < one.size(); ++i) {
            INFO(n << " threads, vertex " << i);
            // Bit-exact: `==` on the floats, not a tolerance.
            CHECK(many[i].x == one[i].x);
            CHECK(many[i].y == one[i].y);
            CHECK(many[i].z == one[i].z);
        }
    }
}
