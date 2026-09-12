// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Posing from one frame of a multi-frame BVH.
//
// `loadBodyPose` refuses anything but a single frame, on the sound grounds that
// "a multi-frame BVH is an animation, and silently taking its first frame would
// turn a wrong file into a plausible wrong pose". The consequence nobody wrote
// down is that the animations shipped under `data/animations/` were unreachable:
// measured, `zombie/zombieWalk1.bvh` holds 31 frames and `walks/walk1.bvh`
// holds 14, and every one of them was refused.
//
// `makePoseUnits` already builds each frame as a unit, so the only thing that
// was missing is which frame to ask for. Saying it out loud -- a frame index --
// keeps `loadBodyPose`'s refusal intact, because the caller who names a frame
// has said they know the file is an animation.
//
// **The shipped walks still cannot be used, and not for that reason.** Measured
// here: `walk1.bvh` and `zombieWalk1.bvh` name 75 joints and **0** of them are
// a bone of either shipped rig -- they are the OLD MakeHuman skeleton
// (`Spine1`, `UpArm_L`, `Clavicle_L`) against this port's `clavicle.L`. Every
// bone therefore stays at identity and every frame is the same. The multi-frame
// file that DOES drive the rig is `face-poseunits.bvh`, 60 frames and 163 of
// 163 joints matched, which is what the tests below use.

#include "makehuman/foundation/DataDir.h"
#include "makehuman/rig/PoseUnits.h"
#include "makehuman/rig/Skeleton.h"

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <string>

using namespace mh::rig;
namespace fs = std::filesystem;

namespace {

fs::path dataDir() {
    return fs::path(MH_DATA_DIR);
}

/// 60 frames, and every one of its joints names a bone of the rig.
fs::path anim() {
    return dataDir() / "poseunits" / "face-poseunits.bvh";
}

/// 75 joints, none of which the rig has. Kept as a fixture because that fact
/// is the reason the shipped animations are unreachable.
fs::path walk() {
    return dataDir() / "animations" / "walks" / "walk1.bvh";
}

fs::path tpose() {
    return dataDir() / "poses" / "tpose.bvh";
}

Skeleton defaultRig() {
    auto skel = loadSkeleton(dataDir() / "rigs" / "default.mhskel");
    REQUIRE(skel.has_value());
    return *skel;
}

/// Measured on the shipped files, not assumed.
constexpr size_t kAnimFrames = 60;
constexpr size_t kWalkFrames = 14;

/// `Mat4` has no `operator==`; comparing the 16 floats exactly is what is
/// wanted here anyway, because both sides come from the same reader and any
/// difference at all means a different frame was returned.
bool same(const mh::foundation::Mat4& a, const mh::foundation::Mat4& b) {
    return a.m == b.m;
}

}  // namespace

TEST_CASE("a frame of an animation loads as a pose", "[rig][poseframe]") {
    const Skeleton skel = defaultRig();

    const auto first = loadBodyPoseFrame(anim(), skel, 0);
    REQUIRE(first.has_value());
    CHECK(first->size() == skel.bones.size());
}

TEST_CASE("different frames give different poses", "[rig][poseframe]") {
    const Skeleton skel = defaultRig();

    const auto first = loadBodyPoseFrame(anim(), skel, 0);
    const auto later = loadBodyPoseFrame(anim(), skel, 7);
    REQUIRE(first.has_value());
    REQUIRE(later.has_value());
    REQUIRE(first->size() == later->size());

    // The premise. Without this, every assertion in this file passes on a
    // loader that returns frame 0 whatever it is asked for -- which is exactly
    // the "plausible wrong pose" loadBodyPose refuses to produce.
    bool anyDifferent = false;
    for (size_t i = 0; i < first->size(); ++i) {
        if (!same((*first)[i], (*later)[i])) {
            anyDifferent = true;
            break;
        }
    }
    CHECK(anyDifferent);
}

TEST_CASE("every frame of a 60-frame file loads", "[rig][poseframe]") {
    const Skeleton skel = defaultRig();
    for (size_t f = 0; f < kAnimFrames; ++f) {
        INFO("frame " << f);
        CHECK(loadBodyPoseFrame(anim(), skel, f).has_value());
    }
}

TEST_CASE("a frame past the end is refused, and the message says how many there are",
          "[rig][poseframe]") {
    const Skeleton skel = defaultRig();

    const auto past = loadBodyPoseFrame(anim(), skel, kAnimFrames);
    REQUIRE(!past.has_value());
    CHECK(past.error().kind == PoseUnitsErrorKind::FrameCountMismatch);
    // A user who guessed at an index needs the real count, not "out of range".
    CHECK(past.error().detail.find(std::to_string(kAnimFrames)) != std::string::npos);
}

TEST_CASE("frame 0 of a single-frame file is that file's pose", "[rig][poseframe]") {
    const Skeleton skel = defaultRig();

    const auto viaFrame = loadBodyPoseFrame(tpose(), skel, 0);
    const auto viaPose  = loadBodyPose(tpose(), skel);
    REQUIRE(viaFrame.has_value());
    REQUIRE(viaPose.has_value());
    // The two paths must not diverge: one is the other with the index spelled
    // out, and a T-pose read two ways that disagreed would be a silent source
    // of two different exports.
    REQUIRE(viaFrame->size() == viaPose->size());
    for (size_t i = 0; i < viaFrame->size(); ++i) {
        INFO("bone " << i);
        CHECK(same((*viaFrame)[i], (*viaPose)[i]));
    }
}

TEST_CASE("the shipped walks name no bone of either rig, so every frame is identity",
          "[rig][poseframe]") {
    const Skeleton skel = defaultRig();

    // This is the measured reason `data/animations/` is unreachable, pinned so
    // that it stops being folklore and so that RETARGETING the content makes
    // this test fail -- which is the moment someone should come back here.
    //
    // walk1.bvh names 75 joints: Root, Spine1, UpArm_L, Clavicle_L ... the old
    // MakeHuman skeleton. This port's rigs use clavicle.L, upperarm01.L. The
    // intersection is EMPTY on both `default` (163 bones) and
    // `mixamo_superset` (179), so makePoseUnits leaves every bone at identity.
    const auto first = loadBodyPoseFrame(walk(), skel, 0);
    const auto later = loadBodyPoseFrame(walk(), skel, kWalkFrames - 1);
    REQUIRE(first.has_value());
    REQUIRE(later.has_value());

    const auto identity = mh::foundation::Mat4::identity();
    for (size_t i = 0; i < first->size(); ++i) {
        INFO("bone " << i);
        CHECK(same((*first)[i], identity));
    }
    // And so the first and last frames of a walk cycle are the same pose, which
    // is what "blocked on content" looks like from the inside.
    for (size_t i = 0; i < first->size(); ++i) {
        CHECK(same((*first)[i], (*later)[i]));
    }
}

TEST_CASE("loadBodyPose STILL refuses an animation", "[rig][poseframe]") {
    const Skeleton skel = defaultRig();

    // Regression. The whole point of naming a frame is that asking for a POSE
    // and getting frame 0 of a walk cycle stays an error.
    const auto pose = loadBodyPose(anim(), skel);
    REQUIRE(!pose.has_value());
    CHECK(pose.error().kind == PoseUnitsErrorKind::FrameCountMismatch);
    CHECK(pose.error().detail.find("is an animation, not a pose") != std::string::npos);
}

TEST_CASE("a missing file is still not found, whichever frame is asked for", "[rig][poseframe]") {
    const Skeleton skel = defaultRig();

    const auto missing = loadBodyPoseFrame(dataDir() / "animations" / "nope.bvh", skel, 3);
    REQUIRE(!missing.has_value());
    CHECK(missing.error().kind == PoseUnitsErrorKind::NotFound);
}
