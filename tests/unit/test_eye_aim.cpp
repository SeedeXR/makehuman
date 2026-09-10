// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Aiming the eyes: the constraint half of owner directive 12.3's "eye and teeth
// rigging are NOT this".
//
// The directive is explicit that eyes are skeleton and constraint work and that
// expressing them as correctives is "a trap". The skeleton was already there --
// `eye.L` and `eye.R` in both shipped rigs, 141 base vertices weighted to each,
// and the shipped eye proxy fitted entirely onto those vertices (measured: all
// 96 base vertices it references carry an eye weight). What was missing was any
// way to AIM them: the only route was hand-authoring a pose file with two
// rotations, and getting the convergence right by hand is exactly what a
// constraint is for.
//
// Every number below about the shipped rig was measured, not assumed:
//   eye.L head (0.3078, 7.2842, 1.2454), tail (0.3174, 7.2949, 1.6182)
//   eye.R head (-0.3078, ...), mirrored
// so the eyes sit 0.6156 dm apart -- 6.2 cm, a real interpupillary distance --
// and the bone points very nearly straight along +Z, which is where the model
// faces.
#include "makehuman/rig/EyeAim.h"

#include "makehuman/core/Mesh.h"
#include "makehuman/core/ObjReader.h"
#include "makehuman/foundation/SwingTwist.h"
#include "makehuman/foundation/Transform.h"
#include "makehuman/rig/Skeleton.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <cmath>
#include <numbers>
#include <vector>

using namespace mh;
using Catch::Matchers::WithinAbs;

namespace {

std::filesystem::path data(const char* rel) {
    return std::filesystem::path(MH_DATA_DIR) / rel;
}

rig::Skeleton shippedRig() {
    const auto mesh = core::loadObj(data("3dobjs/base.obj"));
    REQUIRE(mesh.has_value());
    auto skel = rig::loadSkeleton(data("rigs/default.mhskel"));
    REQUIRE(skel.has_value());
    REQUIRE(skel->updateJoints(mesh->coord()));
    REQUIRE(skel->buildRestMatrices());
    return std::move(*skel);
}

std::vector<foundation::Mat4> restPose(const rig::Skeleton& s) {
    return std::vector<foundation::Mat4>(s.boneCount(), foundation::Mat4::identity());
}

const rig::Bone& boneNamed(const rig::Skeleton& s, std::string_view name) {
    for (const auto& b : s.bones)
        if (b.name == name) return b;
    FAIL("no bone named " << name);
    return s.bones[0];
}

size_t indexOf(const rig::Skeleton& s, std::string_view name) {
    for (size_t i = 0; i < s.bones.size(); ++i)
        if (s.bones[i].name == name) return i;
    FAIL("no bone named " << name);
    return 0;
}

/// The rotation part of @p m applied to @p v. Written out here rather than
/// shared with the implementation, so the check is independent of it.
foundation::Vec3 rotate(const foundation::Mat4& m, foundation::Vec3 v) {
    return foundation::Vec3{m.m[0][0] * v.x + m.m[0][1] * v.y + m.m[0][2] * v.z,
                            m.m[1][0] * v.x + m.m[1][1] * v.y + m.m[1][2] * v.z,
                            m.m[2][0] * v.x + m.m[2][1] * v.y + m.m[2][2] * v.z};
}

foundation::Vec3 normalized(foundation::Vec3 v) {
    const float len = std::sqrt((v.x * v.x) + (v.y * v.y) + (v.z * v.z));
    return foundation::Vec3{v.x / len, v.y / len, v.z / len};
}

/// Where the bone's own axis points in the world, under @p local.
///
/// The bone axis is +Y in the bone's own rest frame -- `buildRestMatrices`
/// writes the normalised bone direction as column 1, re-measured for the eye
/// bones specifically: eye.L's Y column is (0.0257, 0.0289, 0.9992), which is
/// its (tail - head) normalised.
foundation::Vec3 aimedDirection(const rig::Bone& bone, const foundation::Mat4& local) {
    const foundation::Vec3 inBone = rotate(local, foundation::Vec3{0.0F, 1.0F, 0.0F});
    return normalized(rotate(bone.matRestGlobal, inBone));
}

double angleBetween(foundation::Vec3 a, foundation::Vec3 b) {
    const double d = static_cast<double>(a.x) * static_cast<double>(b.x) +
                     static_cast<double>(a.y) * static_cast<double>(b.y) +
                     static_cast<double>(a.z) * static_cast<double>(b.z);
    return std::acos(std::clamp(d, -1.0, 1.0)) * 180.0 / std::numbers::pi;
}

}  // namespace

TEST_CASE("each eye points at the target", "[rig][eyeaim]") {
    // The definition, checked against the definition rather than against a
    // recorded number: after aiming, the bone's own axis in world space must be
    // parallel to (target - that eye's head).
    const auto skel = shippedRig();
    auto pose       = restPose(skel);

    // Well inside the limits, off to the character's own left and slightly up.
    const foundation::Vec3 target{1.5F, 7.8F, 6.0F};
    const auto report = rig::aimEyes(skel, target, pose);
    REQUIRE(report.has_value());
    CHECK_FALSE(report->clamped);

    for (const char* name : {"eye.L", "eye.R"}) {
        const rig::Bone& bone       = boneNamed(skel, name);
        const foundation::Vec3 want = normalized(foundation::Vec3{
            target.x - bone.head.x, target.y - bone.head.y, target.z - bone.head.z});
        const foundation::Vec3 got  = aimedDirection(bone, pose[indexOf(skel, name)]);
        INFO(name << " aimed at (" << got.x << ", " << got.y << ", " << got.z << ") want ("
                  << want.x << ", " << want.y << ", " << want.z << ")");
        CHECK(angleBetween(got, want) < 0.01);
    }
}

TEST_CASE("the eyes converge on a near target and agree on a far one", "[rig][eyeaim]") {
    // THE case. Both eyes share one target but not one position -- they sit
    // 0.6156 dm apart -- so a near target needs two DIFFERENT rotations and a
    // distant one needs almost the same rotation twice.
    //
    // An implementation that computed one rotation from the midpoint between
    // the eyes and used it for both passes every other assertion in this file:
    // the aim is nearly right, the limits work, there is no roll. It fails
    // here, and only here.
    const auto skel = shippedRig();

    auto near = restPose(skel);
    // Two decimetres in front of the face: a fingertip held up to look at.
    REQUIRE(rig::aimEyes(skel, foundation::Vec3{0.0F, 7.28F, 3.25F}, near).has_value());
    const double nearApart =
        angleBetween(aimedDirection(boneNamed(skel, "eye.L"), near[indexOf(skel, "eye.L")]),
                     aimedDirection(boneNamed(skel, "eye.R"), near[indexOf(skel, "eye.R")]));

    auto far = restPose(skel);
    // A hundred decimetres -- ten metres -- straight ahead.
    REQUIRE(rig::aimEyes(skel, foundation::Vec3{0.0F, 7.28F, 101.0F}, far).has_value());
    const double farApart =
        angleBetween(aimedDirection(boneNamed(skel, "eye.L"), far[indexOf(skel, "eye.L")]),
                     aimedDirection(boneNamed(skel, "eye.R"), far[indexOf(skel, "eye.R")]));

    INFO("near convergence " << nearApart << " deg, far " << farApart << " deg");
    // Geometry, not a tuned threshold. Half the eye separation over the
    // distance is atan(0.3078/2.0) = 8.73 degrees PER EYE, so 17.5 between the
    // two gaze directions -- measured 17.4555. At 100 dm it is 0.177 each and
    // 0.35 between them -- measured 0.352593. (An earlier version of this
    // comment called 8.7 the total, which is the per-eye figure; the numbers
    // came out at twice it and the arithmetic said why.)
    CHECK(nearApart > 5.0);
    CHECK(farApart < 1.0);
    CHECK(nearApart > farApart * 5.0);
}

TEST_CASE("a target on an eye's own axis leaves it at rest", "[rig][eyeaim]") {
    // The identity case, and it has to be per-eye: a point on eye.L's rest axis
    // is NOT on eye.R's, so only the left eye should come back unrotated.
    const auto skel             = shippedRig();
    const rig::Bone& left       = boneNamed(skel, "eye.L");
    const foundation::Vec3 axis = normalized(foundation::Vec3{
        left.tail.x - left.head.x, left.tail.y - left.head.y, left.tail.z - left.head.z});
    const foundation::Vec3 target{left.head.x + axis.x * 50.0F, left.head.y + axis.y * 50.0F,
                                  left.head.z + axis.z * 50.0F};

    auto pose = restPose(skel);
    REQUIRE(rig::aimEyes(skel, target, pose).has_value());

    const auto& l = pose[indexOf(skel, "eye.L")];
    for (size_t r = 0; r < 3; ++r)
        for (size_t c = 0; c < 3; ++c)
            CHECK_THAT(static_cast<double>(l.m[r][c]), WithinAbs(r == c ? 1.0 : 0.0, 1e-5));

    // ...and the right eye is NOT at rest, because the target is off to its side.
    const auto& rr = pose[indexOf(skel, "eye.R")];
    CHECK(std::abs(static_cast<double>(rr.m[0][1])) > 1e-3);
}

TEST_CASE("the aim is clamped to what an eye can do", "[rig][eyeaim]") {
    // A human eye reaches roughly 35 degrees horizontally and 25 vertically.
    // Without a limit, a target beside the head aims the eyeballs straight out
    // of the skull -- which the skinning will happily do, because a bone
    // rotation has no idea there is a socket around it.
    const auto skel = shippedRig();
    auto pose       = restPose(skel);

    // Directly to the character's left, level with the eyes: 90 degrees off.
    const auto report = rig::aimEyes(skel, foundation::Vec3{50.0F, 7.2842F, 1.2454F}, pose);
    REQUIRE(report.has_value());
    CHECK(report->clamped);

    // Clamped to the limit, not to something near it.
    CHECK_THAT(report->leftDegrees, WithinAbs(35.0, 0.5));
    CHECK_THAT(report->rightDegrees, WithinAbs(35.0, 0.5));

    // And a target inside the limits is not reported as clamped.
    auto gentle   = restPose(skel);
    const auto ok = rig::aimEyes(skel, foundation::Vec3{1.0F, 7.4F, 6.0F}, gentle);
    REQUIRE(ok.has_value());
    CHECK_FALSE(ok->clamped);
    CHECK(ok->leftDegrees < 35.0);

    // WHICH eye is which, and the report has to say so correctly. The target is
    // off to the character's own left, and eye.L sits at x = +0.3078 against
    // eye.R at -0.3078 -- so the left eye is already closer to it and turns
    // LESS. Without this the two figures are interchangeable: measured, a
    // mutation that filled them the other way round passed all six cases.
    INFO("left " << ok->leftDegrees << " right " << ok->rightDegrees);
    CHECK(ok->leftDegrees < ok->rightDegrees);
}

TEST_CASE("aiming adds no roll", "[rig][eyeaim]") {
    // An eyeball's roll about its own line of sight is not observable on a
    // sphere, so introducing one is invisible in a render and wrong in an
    // export -- it shows up the moment anything else reads the bone, and it is
    // exactly what a look-at built from an up-vector produces.
    //
    // Checked with the same swing/twist split the correctives key on
    // (`foundation::swingTwist`), about the bone's own axis.
    const auto skel = shippedRig();
    auto pose       = restPose(skel);
    REQUIRE(rig::aimEyes(skel, foundation::Vec3{1.2F, 8.4F, 4.0F}, pose).has_value());

    for (const char* name : {"eye.L", "eye.R"}) {
        const auto q       = foundation::quaternionFromMatrix(pose[indexOf(skel, name)]);
        const auto st      = foundation::swingTwist(q, foundation::Vec3{0.0F, 1.0F, 0.0F});
        const double twist = foundation::twistAngle(st.twist, foundation::Vec3{0.0F, 1.0F, 0.0F});
        INFO(name << " twist " << twist << " rad");
        CHECK_THAT(twist, WithinAbs(0.0, 1e-6));
    }
}

TEST_CASE("aiming refuses what it cannot do", "[rig][eyeaim]") {
    const auto skel = shippedRig();

    SECTION("a pose that is not one matrix per bone") {
        std::vector<foundation::Mat4> tooFew(3, foundation::Mat4::identity());
        const auto r = rig::aimEyes(skel, foundation::Vec3{0.0F, 7.3F, 5.0F}, tooFew);
        REQUIRE_FALSE(r.has_value());
        CHECK(r.error().kind == rig::EyeAimErrorKind::PoseSize);
    }

    SECTION("a target sitting on an eye") {
        // The direction is then undefined, and normalising it gives NaN --
        // which propagates into the pose and skins the whole head to nothing.
        const rig::Bone& left = boneNamed(skel, "eye.L");
        auto pose             = restPose(skel);
        const auto r          = rig::aimEyes(skel, left.head, pose);
        REQUIRE_FALSE(r.has_value());
        CHECK(r.error().kind == rig::EyeAimErrorKind::TargetAtEye);
    }

    SECTION("a rig with no eye bones") {
        // Refused by NAME at call time, like a corrective bind: a rig without
        // eye bones is a legitimate rig, and silently aiming nothing is how a
        // character stares straight ahead with no explanation.
        rig::Skeleton bare;
        bare.bones.push_back(rig::Bone{.name = "root"});
        std::vector<foundation::Mat4> pose(1, foundation::Mat4::identity());
        const auto r = rig::aimEyes(bare, foundation::Vec3{0.0F, 1.0F, 1.0F}, pose);
        REQUIRE_FALSE(r.has_value());
        CHECK(r.error().kind == rig::EyeAimErrorKind::NoEyeBones);
    }

    SECTION("a negative limit") {
        auto pose = restPose(skel);
        const auto r =
            rig::aimEyes(skel, foundation::Vec3{0.0F, 7.3F, 5.0F}, pose,
                         rig::EyeAimLimits{.horizontalDegrees = -1.0, .verticalDegrees = 25.0});
        REQUIRE_FALSE(r.has_value());
        CHECK(r.error().kind == rig::EyeAimErrorKind::BadLimits);
    }

    SECTION("a limit beyond what an eye is") {
        // Above 90 degrees the aimed direction leaves the forward hemisphere,
        // and the minimal-rotation construction stops being well defined: at
        // exactly antiparallel the rotation axis is the zero vector, and the
        // function would hand back the IDENTITY -- an eye told to look directly
        // backwards and quietly looking straight ahead.
        //
        // Found by reading the diff, not by a failing test: with the human
        // limits the aimed y never drops below cos(35)*cos(25) = 0.742, so the
        // case is unreachable through the defaults and perfectly reachable
        // through the parameter. Refused, rather than special-cased, because an
        // eye that turns more than 90 degrees is a caller's bug either way.
        auto pose = restPose(skel);
        for (const double bad : {90.5, 200.0}) {
            const auto r =
                rig::aimEyes(skel, foundation::Vec3{0.0F, 7.3F, 5.0F}, pose,
                             rig::EyeAimLimits{.horizontalDegrees = bad, .verticalDegrees = 25.0});
            INFO("horizontal limit " << bad);
            REQUIRE_FALSE(r.has_value());
            CHECK(r.error().kind == rig::EyeAimErrorKind::BadLimits);
        }
        // ...and 90 exactly is still accepted: it is the boundary, where the
        // aimed direction is perpendicular to rest and the axis is still a real
        // vector.
        const auto edge =
            rig::aimEyes(skel, foundation::Vec3{0.0F, 7.3F, 5.0F}, pose,
                         rig::EyeAimLimits{.horizontalDegrees = 90.0, .verticalDegrees = 90.0});
        CHECK(edge.has_value());
    }

    SECTION("a limit that is not a number") {
        // Its own section, because `x < 0` catches the negative and NOT the
        // NaN: every comparison against NaN is false, so a NaN limit would sail
        // through and clamp both angles to NaN, which reaches the pose.
        auto pose    = restPose(skel);
        const auto r = rig::aimEyes(
            skel, foundation::Vec3{0.0F, 7.3F, 5.0F}, pose,
            rig::EyeAimLimits{.horizontalDegrees = std::nan(""), .verticalDegrees = 25.0});
        REQUIRE_FALSE(r.has_value());
        CHECK(r.error().kind == rig::EyeAimErrorKind::BadLimits);
    }
}
