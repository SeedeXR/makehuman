// SPDX-License-Identifier: AGPL-3.0-or-later
//
// The pose-signal evaluator: the missing link between the rig and the
// correctives, and the thing owner directive 12.3 calls "one pose-signal
// evaluator, several consumers".
//
// Everything else in the pipeline was built first and had no way to be fed:
// `foundation::swingTwist` splits one quaternion, `foundation::rbfSolve`
// interpolates between points in signal space, and `core::CorrectiveBuffer`
// applies the weights -- but nothing walked a posed skeleton and produced the
// point. This does, and until it existed `swingTwist` had no caller outside its
// own tests.
//
// THE FACT IT ALL RESTS ON: in a bone's own frame the bone points along +Y.
// `buildRestMatrices` writes the axes as COLUMNS with the normalised bone
// direction in column 1 (src/rig/Skeleton.cpp), and `poseToBoneLocal`
// conjugates a pose into that frame. So the twist axis is not per-bone data to
// be looked up -- it is the constant (0,1,0). Measured across the shipped rig:
// all 163 bones, worst L1 error 1.2e-7 against their own normalised direction.
// The first case here re-measures it, because a silent change to that
// convention would leave every signal plausible and wrong.
#include "makehuman/rig/PoseSignal.h"

#include "makehuman/core/ObjReader.h"
#include "makehuman/foundation/Transform.h"
#include "makehuman/rig/PoseUnits.h"
#include "makehuman/rig/Skeleton.h"
#include "makehuman/rig/Skinning.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <cmath>
#include <filesystem>
#include <numbers>
#include <vector>

using namespace mh;
using namespace mh::rig;
using Catch::Matchers::WithinAbs;

namespace {

std::filesystem::path data(const char* rel) {
    return std::filesystem::path(MH_DATA_DIR) / rel;
}

/// The shipped rig, placed against the shipped base mesh.
Skeleton shippedRig() {
    const auto mesh = core::loadObj(data("3dobjs/base.obj"));
    REQUIRE(mesh.has_value());
    auto skel = loadSkeleton(data("rigs/default.mhskel"));
    REQUIRE(skel.has_value());
    REQUIRE(skel->updateJoints(mesh->coord()));
    REQUIRE(skel->buildRestMatrices());
    return std::move(*skel);
}

std::vector<foundation::Mat4> restPose(const Skeleton& s) {
    return std::vector<foundation::Mat4>(s.boneCount(), foundation::Mat4::identity());
}

size_t boneNamed(const Skeleton& s, const char* name) {
    for (size_t i = 0; i < s.bones.size(); ++i) {
        if (s.bones[i].name == name) return i;
    }
    FAIL("no bone named " << name);
    return 0;
}

const core::Driver kArmSwing{"upperarm01.L", core::DriverComponent::Swing};
const core::Driver kArmTwist{"lowerarm01.L", core::DriverComponent::Twist};

}  // namespace

TEST_CASE("in its own frame every bone points along +Y", "[rig][posesignal]") {
    // The convention the twist axis is taken from. If this ever stops holding,
    // every signal stays finite and plausible and is silently about the wrong
    // axis -- so it is asserted here rather than assumed in a comment.
    const Skeleton s = shippedRig();
    size_t checked   = 0;
    for (const Bone& b : s.bones) {
        if (b.length <= 0.0F) continue;  // tip markers have no direction
        const auto d      = b.direction();
        const auto length = std::sqrt(d.x * d.x + d.y * d.y + d.z * d.z);
        CHECK_THAT(static_cast<double>(b.matRestGlobal.m[0][1]),
                   WithinAbs(static_cast<double>(d.x / length), 1e-6));
        CHECK_THAT(static_cast<double>(b.matRestGlobal.m[1][1]),
                   WithinAbs(static_cast<double>(d.y / length), 1e-6));
        CHECK_THAT(static_cast<double>(b.matRestGlobal.m[2][1]),
                   WithinAbs(static_cast<double>(d.z / length), 1e-6));
        ++checked;
    }
    // A floor, or a rig that stopped loading its bones would pass by checking
    // nothing. The shipped rig has 163.
    CHECK(checked > 150);
}

TEST_CASE("a plan resolves driver names once, not every frame", "[rig][posesignal]") {
    const Skeleton s = shippedRig();

    SECTION("names resolve and the dimension adds up") {
        const core::Driver drivers[]{kArmSwing, kArmTwist};
        const auto plan = planPoseSignal(s, drivers);
        REQUIRE(plan.has_value());
        // Swing is three numbers, twist is one -- the same rule the manifest
        // derives its dimension from.
        CHECK(plan->dimension == 4);
        REQUIRE(plan->bones.size() == 2);
        CHECK(plan->bones[0] == boneNamed(s, "upperarm01.L"));
        CHECK(plan->bones[1] == boneNamed(s, "lowerarm01.L"));
    }

    SECTION("an unknown joint is refused, and named") {
        // At PLAN time, which is the point of planning: a corrective bound to a
        // rig that does not have its driving joint must fail when it is bound,
        // not in the middle of an animation.
        const core::Driver drivers[]{kArmSwing, {"nosuchbone.L", core::DriverComponent::Twist}};
        const auto plan = planPoseSignal(s, drivers);
        REQUIRE_FALSE(plan.has_value());
        CHECK(plan.error().kind == PoseSignalErrorKind::UnknownJoint);
        CHECK(plan.error().detail.find("nosuchbone.L") != std::string::npos);
    }

    SECTION("no drivers is not a signal") {
        const auto plan = planPoseSignal(s, {});
        REQUIRE_FALSE(plan.has_value());
        CHECK(plan.error().kind == PoseSignalErrorKind::NoDrivers);
    }
}

TEST_CASE("the rest pose is exactly the origin of signal space", "[rig][posesignal]") {
    // Why `rotationVector` returns the log map rather than a quaternion, and
    // why the RBF's example poses can sit around zero: an unposed character
    // must land exactly on the origin, not near it. Bit-exact, because "near
    // the origin" would still evaluate every corrective slightly on.
    const Skeleton s = shippedRig();
    const core::Driver drivers[]{kArmSwing, kArmTwist};
    const auto plan = planPoseSignal(s, drivers);
    REQUIRE(plan.has_value());

    std::vector<double> signal(plan->dimension, 999.0);
    REQUIRE(evaluatePoseSignal(*plan, restPose(s), signal));
    for (const double v : signal) {
        CHECK(v == 0.0);
    }
}

TEST_CASE("a rotation about the bone's own axis is all twist", "[rig][posesignal]") {
    // The split doing its job on a case whose answer is known exactly. A
    // forearm pronating rotates about its own length: it must show in the one
    // twist number and not at all in the three swing numbers.
    const Skeleton s = shippedRig();
    const core::Driver drivers[]{{"lowerarm01.L", core::DriverComponent::Twist},
                                 {"lowerarm01.L", core::DriverComponent::Swing}};
    const auto plan = planPoseSignal(s, drivers);
    REQUIRE(plan.has_value());
    REQUIRE(plan->dimension == 4);

    const size_t bone = boneNamed(s, "lowerarm01.L");
    for (const double angle : {0.4, -0.9, 2.0}) {
        auto local = restPose(s);
        // A rotation about +Y in the bone's own frame is a rotation about the
        // bone. `rotationMatrix` takes the axis in that frame.
        local[bone] = foundation::rotationMatrix(angle, foundation::Vec3{0.0F, 1.0F, 0.0F});

        std::vector<double> signal(4);
        REQUIRE(evaluatePoseSignal(*plan, local, signal));
        // 1e-6, not 1e-9, and the reason is a real ceiling rather than slack:
        // `Mat4` is FLOAT, so an angle round-tripping through a rotation matrix
        // arrives with float precision however carefully the decomposition is
        // done in double. Measured worst error over these three angles: 7.6e-9.
        // The swing components below are EXACTLY zero, so they keep 1e-9.
        CHECK_THAT(signal[0], WithinAbs(angle, 1e-6));  // twist
        for (size_t i = 1; i < 4; ++i) {
            CHECK_THAT(signal[i], WithinAbs(0.0, 1e-9));  // swing
        }
    }
}

TEST_CASE("a rotation across the bone is all swing", "[rig][posesignal]") {
    // The other half, and the one that catches an implementation that returned
    // the whole rotation for both components: a bone bent sideways has no
    // twist at all.
    const Skeleton s = shippedRig();
    const core::Driver drivers[]{{"upperarm01.L", core::DriverComponent::Swing},
                                 {"upperarm01.L", core::DriverComponent::Twist}};
    const auto plan = planPoseSignal(s, drivers);
    REQUIRE(plan.has_value());

    const size_t bone = boneNamed(s, "upperarm01.L");
    auto local        = restPose(s);
    local[bone]       = foundation::rotationMatrix(0.6, foundation::Vec3{0.0F, 0.0F, 1.0F});

    std::vector<double> signal(4);
    REQUIRE(evaluatePoseSignal(*plan, local, signal));
    CHECK_THAT(signal[0], WithinAbs(0.0, 1e-9));  // swing X
    CHECK_THAT(signal[1], WithinAbs(0.0, 1e-9));  // swing Y -- always, see below
    CHECK_THAT(signal[2], WithinAbs(0.6, 1e-6));  // swing Z: the whole rotation
    CHECK_THAT(signal[3], WithinAbs(0.0, 1e-9));  // twist
}

TEST_CASE("the shipped T-pose gives the signal it actually gives", "[rig][posesignal]") {
    // A real pose off disk through the real loader, pinned to measured values.
    // These are what the shipped T-pose produces today (measured this session):
    //
    //     upperarm01.L  swing (0.0307, 0, 0.5003)   twist -1.718 deg
    //     lowerarm01.L  swing (-0.6744, 0, 0.1246)  twist  2.051 deg
    //
    // Not round numbers, and that is the point: they are what the data says,
    // and a change to the pose loader, the rest matrices or the decomposition
    // moves them.
    const Skeleton s = shippedRig();
    const auto pose  = loadBodyPose(data("poses/tpose.bvh"), s);
    REQUIRE(pose.has_value());
    const auto local = poseToBoneLocal(s, *pose);
    REQUIRE(local.size() == s.boneCount());

    const core::Driver drivers[]{{"upperarm01.L", core::DriverComponent::Swing},
                                 {"upperarm01.L", core::DriverComponent::Twist},
                                 {"lowerarm01.L", core::DriverComponent::Swing},
                                 {"lowerarm01.L", core::DriverComponent::Twist}};
    const auto plan = planPoseSignal(s, drivers);
    REQUIRE(plan.has_value());
    REQUIRE(plan->dimension == 8);

    std::vector<double> signal(8);
    REQUIRE(evaluatePoseSignal(*plan, local, signal));

    constexpr double kDeg = std::numbers::pi / 180.0;
    CHECK_THAT(signal[0], WithinAbs(0.0307, 1e-3));
    CHECK_THAT(signal[1], WithinAbs(0.0, 1e-9));
    CHECK_THAT(signal[2], WithinAbs(0.5003, 1e-3));
    CHECK_THAT(signal[3], WithinAbs(-1.718 * kDeg, 1e-3));
    CHECK_THAT(signal[4], WithinAbs(-0.6744, 1e-3));
    CHECK_THAT(signal[5], WithinAbs(0.0, 1e-9));
    CHECK_THAT(signal[6], WithinAbs(0.1246, 1e-3));
    CHECK_THAT(signal[7], WithinAbs(2.051 * kDeg, 1e-3));

    // And the arm is genuinely posed, so this is not passing on a rig that
    // failed to load its pose: half a radian is about 29 degrees.
    CHECK(std::abs(signal[2]) > 0.4);
}

TEST_CASE("a swing never has a component along the bone", "[rig][posesignal]") {
    // What makes the split a swing-TWIST split rather than any other
    // factorisation, checked over EVERY bone of a real pose rather than the
    // two the other cases name. Measured: exactly zero for all of them.
    const Skeleton s = shippedRig();
    const auto pose  = loadBodyPose(data("poses/tpose.bvh"), s);
    REQUIRE(pose.has_value());
    const auto local = poseToBoneLocal(s, *pose);

    size_t checked = 0;
    for (const Bone& b : s.bones) {
        const core::Driver one[]{{b.name, core::DriverComponent::Swing}};
        const auto plan = planPoseSignal(s, one);
        REQUIRE(plan.has_value());
        std::vector<double> signal(3);
        REQUIRE(evaluatePoseSignal(*plan, local, signal));
        CHECK_THAT(signal[1], WithinAbs(0.0, 1e-9));
        ++checked;
    }
    CHECK(checked > 150);
}

TEST_CASE("driver order is the signal's order", "[rig][posesignal]") {
    // The manifest's poses list their signal values in driver order, so the
    // evaluator must lay them out the same way. Swapping two drivers must swap
    // their blocks -- and with a swing and a twist those blocks are different
    // LENGTHS, so an implementation that assumed a fixed stride is caught.
    const Skeleton s = shippedRig();
    const auto pose  = loadBodyPose(data("poses/tpose.bvh"), s);
    REQUIRE(pose.has_value());
    const auto local = poseToBoneLocal(s, *pose);

    const core::Driver forward[]{kArmSwing, kArmTwist};
    const core::Driver reverse[]{kArmTwist, kArmSwing};
    const auto a = planPoseSignal(s, forward);
    const auto b = planPoseSignal(s, reverse);
    REQUIRE(a.has_value());
    REQUIRE(b.has_value());

    std::vector<double> first(4);
    std::vector<double> second(4);
    REQUIRE(evaluatePoseSignal(*a, local, first));
    REQUIRE(evaluatePoseSignal(*b, local, second));

    // forward is [swingX swingY swingZ twist]; reverse is [twist swingX swingY swingZ].
    CHECK_THAT(second[0], WithinAbs(first[3], 1e-12));
    CHECK_THAT(second[1], WithinAbs(first[0], 1e-12));
    CHECK_THAT(second[2], WithinAbs(first[1], 1e-12));
    CHECK_THAT(second[3], WithinAbs(first[2], 1e-12));
}

TEST_CASE("the wrong shapes are refused rather than written past", "[rig][posesignal]") {
    const Skeleton s = shippedRig();
    const core::Driver drivers[]{kArmSwing, kArmTwist};
    const auto plan = planPoseSignal(s, drivers);
    REQUIRE(plan.has_value());

    SECTION("an output span too small for the signal") {
        std::vector<double> tooSmall(3);
        CHECK_FALSE(evaluatePoseSignal(*plan, restPose(s), tooSmall));
    }

    SECTION("a pose that is not one matrix per bone") {
        // One short and one long. Checking only that the DRIVER bones are in
        // range would accept both: the drivers here are arm bones, nowhere near
        // the end of the list, so a pose array from a different skeleton would
        // be read happily with every index meaning a different bone.
        std::vector<double> signal(4);
        auto shortPose = restPose(s);
        shortPose.pop_back();
        CHECK_FALSE(evaluatePoseSignal(*plan, shortPose, signal));

        auto longPose = restPose(s);
        longPose.push_back(foundation::Mat4::identity());
        CHECK_FALSE(evaluatePoseSignal(*plan, longPose, signal));
    }

    SECTION("the right shapes still work") {
        std::vector<double> signal(4);
        CHECK(evaluatePoseSignal(*plan, restPose(s), signal));
        // A larger output span is fine -- only `dimension` values are written,
        // and a caller reusing one buffer for several plans is reasonable.
        std::vector<double> bigger(9, -1.0);
        CHECK(evaluatePoseSignal(*plan, restPose(s), bigger));
        CHECK(bigger[8] == -1.0);
    }
}
