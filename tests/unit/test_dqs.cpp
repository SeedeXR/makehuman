// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Dual quaternion skinning, beside the linear blend we already ship.
//
// LBS blends MATRICES. Halfway between two rotations that differ a lot, the
// averaged matrix is no longer a rotation -- it shrinks -- and the mesh
// collapses toward the bone axis. On a limb twisted about its own length that
// is the "candy wrapper", and it is the single most visible artefact in skinned
// character work.
//
// DQS blends the rigid transforms as dual quaternions and renormalises, so the
// result is always a rotation plus a translation. It cannot represent scale or
// shear, which is why non-rigid input is refused here rather than silently
// producing something plausible and wrong.
//
// The reference has no DQS at all; there is nothing to be parity-tested
// against, so these check the PROPERTIES that make DQS worth having.
#include "makehuman/foundation/Transform.h"
#include "makehuman/rig/Skinning.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <cmath>
#include <numbers>
#include <vector>

using Catch::Matchers::WithinAbs;
using namespace mh;
using mh::foundation::Mat4;
using mh::foundation::Vec3;

namespace {

/// Weights naming one bone per vertex at full strength, or two at 50/50.
rig::CompiledWeights twoBoneWeights(size_t vertices, float toSecond) {
    rig::CompiledWeights w;
    w.influences = 2;
    w.boneIndex.resize(vertices * 2);
    w.weight.resize(vertices * 2);
    for (size_t v = 0; v < vertices; ++v) {
        w.boneIndex[v * 2]     = 0;
        w.boneIndex[v * 2 + 1] = 1;
        w.weight[v * 2]        = 1.0F - toSecond;
        w.weight[v * 2 + 1]    = toSecond;
    }
    return w;
}

Mat4 rotationAboutY(double radians) {
    return foundation::rotationMatrix(radians, Vec3{0.0F, 1.0F, 0.0F});
}

double radius(const Vec3& v) {
    return std::hypot(static_cast<double>(v.x), static_cast<double>(v.z));
}

}  // namespace

TEST_CASE("identity matrices leave every vertex exactly alone", "[rig][dqs]") {
    const std::vector<Vec3> rest{{1, 0, 0}, {0, 1, 0}, {-3, 2, 5}};
    const std::vector<Mat4> skinning{Mat4::identity(), Mat4::identity()};
    std::vector<Vec3> out;

    REQUIRE(rig::skinPositionsDqs(rest, twoBoneWeights(rest.size(), 0.5F), skinning, out));
    REQUIRE(out.size() == rest.size());
    for (size_t i = 0; i < rest.size(); ++i) {
        INFO("vertex " << i);
        CHECK_THAT(static_cast<double>(out[i].x), WithinAbs(static_cast<double>(rest[i].x), 1e-6));
        CHECK_THAT(static_cast<double>(out[i].y), WithinAbs(static_cast<double>(rest[i].y), 1e-6));
        CHECK_THAT(static_cast<double>(out[i].z), WithinAbs(static_cast<double>(rest[i].z), 1e-6));
    }
}

TEST_CASE("one influence gives exactly what LBS gives", "[rig][dqs]") {
    // With a single bone there is nothing to blend, so the two methods must
    // agree to float precision. Anywhere they disagree here is a bug in the
    // matrix/dual-quaternion round trip, not a property of blending.
    const std::vector<Vec3> rest{{1, 0, 0}, {0, 2, 0}, {-3, 2, 5}, {0.5F, -1, 2}};
    std::vector<Mat4> skinning{rotationAboutY(1.1), Mat4::identity()};
    skinning[0].m[0][3] = 2.0F;  // and a translation, so it is not pure rotation
    skinning[0].m[1][3] = -1.0F;

    rig::CompiledWeights w = twoBoneWeights(rest.size(), 0.0F);  // all on bone 0

    std::vector<Vec3> lbs;
    std::vector<Vec3> dqs;
    REQUIRE(rig::skinPositions(rest, w, skinning, lbs));
    REQUIRE(rig::skinPositionsDqs(rest, w, skinning, dqs));
    for (size_t i = 0; i < rest.size(); ++i) {
        INFO("vertex " << i);
        CHECK_THAT(static_cast<double>(dqs[i].x), WithinAbs(static_cast<double>(lbs[i].x), 1e-5));
        CHECK_THAT(static_cast<double>(dqs[i].y), WithinAbs(static_cast<double>(lbs[i].y), 1e-5));
        CHECK_THAT(static_cast<double>(dqs[i].z), WithinAbs(static_cast<double>(lbs[i].z), 1e-5));
    }
}

TEST_CASE("a twisted limb keeps its volume where LBS collapses it", "[rig][dqs]") {
    // The headline. A ring of vertices around the Y axis, weighted 50/50
    // between an unrotated bone and one twisted 180 degrees about that axis.
    //
    // LBS averages the two matrices. Halfway between identity and a 180-degree
    // rotation the averaged rotation part is the ZERO matrix, so every vertex
    // lands on the axis and the limb pinches to nothing. DQS interpolates the
    // rotation itself and the ring keeps its radius.
    constexpr double kR = 2.0;
    std::vector<Vec3> ring;
    for (int i = 0; i < 12; ++i) {
        const double a = 2.0 * std::numbers::pi * i / 12.0;
        ring.push_back(
            Vec3{static_cast<float>(kR * std::cos(a)), 0.0F, static_cast<float>(kR * std::sin(a))});
    }
    const std::vector<Mat4> skinning{Mat4::identity(), rotationAboutY(std::numbers::pi)};
    const rig::CompiledWeights w = twoBoneWeights(ring.size(), 0.5F);

    std::vector<Vec3> lbs;
    std::vector<Vec3> dqs;
    REQUIRE(rig::skinPositions(ring, w, skinning, lbs));
    REQUIRE(rig::skinPositionsDqs(ring, w, skinning, dqs));

    double worstLbs = kR;
    double worstDqs = kR;
    for (size_t i = 0; i < ring.size(); ++i) {
        worstLbs = std::min(worstLbs, radius(lbs[i]));
        worstDqs = std::min(worstDqs, radius(dqs[i]));
    }
    INFO("smallest radius: LBS " << worstLbs << ", DQS " << worstDqs << ", rest " << kR);

    // LBS collapses essentially to the axis...
    CHECK(worstLbs < 0.01);
    // ...and DQS holds the radius. Rigid transforms preserve distance from the
    // axis exactly; the tolerance is float storage, not method error.
    CHECK_THAT(worstDqs, WithinAbs(kR, 1e-4));
}

TEST_CASE("blending is antipodality-safe", "[rig][dqs]") {
    // q and -q are the same rotation, so blending the wrong pair goes the LONG
    // way round and gives a smooth, plausible, completely wrong result.
    //
    // **Choosing the case took two failed guesses and then a measurement.**
    // `quaternionFromMatrix` canonicalises the LARGEST component positive, not
    // `w`. So neither obvious candidate exercises this at all:
    //   * identity against a 300-degree rotation -- the dot stays positive
    //   * +170 against -170 about the same axis -- both get y = +0.996 from the
    //     same branch, so the dot is dominated by that and stays positive
    // A mutation deleting the correction passed both. A sweep over 400,000
    // random pairs found 27% with a negative dot, all of them about DIFFERENT
    // axes, and this is the readable one it led to: dot = -0.378.
    const std::vector<Vec3> rest{{1, 2, -1}};
    const double wide = 150.0 * std::numbers::pi / 180.0;
    const std::vector<Mat4> skinning{
        foundation::rotationMatrix(wide, Vec3{1, 1, 1}),
        foundation::rotationMatrix(wide, Vec3{1, 1, -1}),
    };
    const foundation::Quat q0 = foundation::quaternionFromMatrix(skinning[0]);
    const foundation::Quat q1 = foundation::quaternionFromMatrix(skinning[1]);
    const double d            = (q0.w * q1.w) + (q0.x * q1.x) + (q0.y * q1.y) + (q0.z * q1.z);
    INFO("dot of the canonical quaternions: " << d);
    REQUIRE(d < 0.0);  // the case only tests anything while this holds

    const rig::CompiledWeights w = twoBoneWeights(rest.size(), 0.5F);
    std::vector<Vec3> dqs;
    REQUIRE(rig::skinPositionsDqs(rest, w, skinning, dqs));

    // Checked against `quaternionSlerp`, which handles the sign itself and is
    // an INDEPENDENT implementation of the same correction. At the halfway
    // point normalised-lerp and slerp land on the same rotation, so this is an
    // equality rather than an approximation.
    const Mat4 wanted = foundation::quaternionMatrix(foundation::quaternionSlerp(q0, q1, 0.5));
    std::vector<Vec3> expected;
    REQUIRE(rig::skinPositions(rest, twoBoneWeights(rest.size(), 0.0F),
                               std::vector<Mat4>{wanted, Mat4::identity()}, expected));

    INFO("got (" << dqs[0].x << ", " << dqs[0].y << ", " << dqs[0].z << ") want (" << expected[0].x
                 << ", " << expected[0].y << ", " << expected[0].z << ")");
    CHECK_THAT(static_cast<double>(dqs[0].x), WithinAbs(static_cast<double>(expected[0].x), 1e-4));
    CHECK_THAT(static_cast<double>(dqs[0].y), WithinAbs(static_cast<double>(expected[0].y), 1e-4));
    CHECK_THAT(static_cast<double>(dqs[0].z), WithinAbs(static_cast<double>(expected[0].z), 1e-4));
}

TEST_CASE("only the RATIO of the weights matters, not their sum", "[rig][dqs]") {
    // What renormalising the blend buys, and the only property that catches its
    // absence -- `quaternionMatrix` already normalises internally, so dropping
    // the explicit step leaves the ROTATION correct and corrupts only the
    // TRANSLATION. A mutation that removed it passed every test until this one.
    //
    // Scaling both weights by the same factor must change nothing.
    const std::vector<Vec3> rest{{1, 2, -1}};
    std::vector<Mat4> skinning{rotationAboutY(0.7), rotationAboutY(-0.4)};
    skinning[0].m[0][3] = 3.0F;
    skinning[0].m[2][3] = -2.0F;
    skinning[1].m[1][3] = 1.5F;

    rig::CompiledWeights unit   = twoBoneWeights(rest.size(), 0.5F);
    rig::CompiledWeights scaled = unit;
    for (float& value : scaled.weight)
        value *= 0.6F;

    std::vector<Vec3> a;
    std::vector<Vec3> b;
    REQUIRE(rig::skinPositionsDqs(rest, unit, skinning, a));
    REQUIRE(rig::skinPositionsDqs(rest, scaled, skinning, b));
    INFO("unit (" << a[0].x << ", " << a[0].y << ", " << a[0].z << ") scaled (" << b[0].x << ", "
                  << b[0].y << ", " << b[0].z << ")");
    CHECK_THAT(static_cast<double>(b[0].x), WithinAbs(static_cast<double>(a[0].x), 1e-4));
    CHECK_THAT(static_cast<double>(b[0].y), WithinAbs(static_cast<double>(a[0].y), 1e-4));
    CHECK_THAT(static_cast<double>(b[0].z), WithinAbs(static_cast<double>(a[0].z), 1e-4));
}

TEST_CASE("a non-rigid matrix is refused, not silently approximated", "[rig][dqs]") {
    // DQS represents a rotation and a translation, nothing else. A scaled bone
    // has no dual-quaternion spelling, and quietly dropping the scale would
    // give a mesh that is subtly the wrong size with nothing to explain it.
    const std::vector<Vec3> rest{{1, 0, 0}};
    std::vector<Mat4> skinning{Mat4::identity(), Mat4::identity()};
    skinning[0].m[0][0] = 2.0F;  // scale on X

    std::vector<Vec3> out;
    CHECK_FALSE(rig::skinPositionsDqs(rest, twoBoneWeights(rest.size(), 0.0F), skinning, out));
}

TEST_CASE("the input contract matches skinPositions", "[rig][dqs]") {
    // Same refusals as the linear path, so a caller can swap one for the other
    // without learning a second set of rules.
    const std::vector<Vec3> rest{{1, 0, 0}, {0, 1, 0}};
    const std::vector<Mat4> skinning{Mat4::identity(), Mat4::identity()};
    std::vector<Vec3> out;

    // Weights describing a different number of vertices.
    CHECK_FALSE(rig::skinPositionsDqs(rest, twoBoneWeights(5, 0.5F), skinning, out));

    // A weight naming a bone the pose does not have.
    rig::CompiledWeights beyond = twoBoneWeights(rest.size(), 0.5F);
    beyond.boneIndex[1]         = 99;
    CHECK_FALSE(rig::skinPositionsDqs(rest, beyond, skinning, out));
}
