// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Retargeting on IMPORT: the shipped walks start posing the character.
//
// `data/animations/**/*.bvh` are MakeHuman 1.x exports naming 75 joints of the
// old MakeHuman skeleton. They match 0 bones of either rig this port ships, so
// `--pose walks/walk1.bvh` has always loaded successfully and produced the rest
// pose -- every frame identical, nothing moving. The two retarget tables now
// have a reader and `retargetJoints` is what applies one.
//
// The assertion that matters is NOT "the rename happened". It is that the count
// of driven bones goes 0 -> non-zero AND the frames stop being identical,
// because a rename that produced plausible names while posing nothing would
// pass any check made of strings alone.
#include "makehuman/rig/RetargetMap.h"

#include "makehuman/io/BvhReader.h"
#include "makehuman/rig/PoseUnits.h"
#include "makehuman/rig/Skeleton.h"

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <set>
#include <span>
#include <string>

using namespace mh::rig;
namespace fs = std::filesystem;

namespace {

fs::path dataDir() {
    return fs::path(MH_DATA_DIR);
}

fs::path walk() {
    return dataDir() / "animations" / "walks" / "walk1.bvh";
}

RetargetMap mh1Table() {
    auto map = loadRetargetMap(dataDir() / "rigs" / "makehuman1_retarget.json");
    REQUIRE(map.has_value());
    return *map;
}

/// Exact equality, element by element. `Mat4` has no `operator==` and this is
/// deliberately not the place to add one: these poses are either the SAME
/// identity matrices copied or genuinely different transforms, so a tolerance
/// would only blur the distinction the test is making.
bool samePose(std::span<const mh::foundation::Mat4> a, std::span<const mh::foundation::Mat4> b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i)
        if (a[i].m != b[i].m) return false;
    return true;
}

Skeleton superset() {
    auto skel = loadSkeleton(dataDir() / "rigs" / "mixamo_superset.mhskel");
    REQUIRE(skel.has_value());
    return *skel;
}

}  // namespace

// ---------------------------------------------------------------- unit

TEST_CASE("retargetJoints renames exactly the joints the table names",
          "[rig][retarget][retarget-import]") {
    auto bvh = mh::io::readBvh(walk(), {});
    REQUIRE(bvh.has_value());

    const auto map = mh1Table();

    // MEASURED: walk1 holds 75 non-end-site joints, 59 of which the MakeHuman
    // 1.x table names. The other 16 are the `__`-prefixed connectors, left
    // alone deliberately.
    const size_t renamed = retargetJoints(*bvh, map);
    CHECK(renamed == 59);

    size_t nowBones  = 0;
    size_t untouched = 0;
    for (const auto& joint : bvh->joints) {
        if (joint.endSite) continue;
        if (joint.name.starts_with("__")) {
            ++untouched;
            continue;
        }
        // Every renamed joint must now carry a name the table maps TO, never
        // one it maps FROM -- a half-applied rename is the failure this is for.
        INFO(joint.name << " must be a retarget target, not a source");
        CHECK_FALSE(map.toBone.contains(joint.name));
        ++nowBones;
    }
    CHECK(nowBones == 59);
    CHECK(untouched == 16);
}

TEST_CASE("retargetJoints leaves end sites alone", "[rig][retarget][retarget-import]") {
    // Checked against a table that DELIBERATELY names one. Neither shipped
    // table names a `_end` joint, so running this with `mh1Table()` would pass
    // whether or not the end-site guard existed -- measured: deleting the guard
    // leaves every other test in this file green. A guard no test can kill is
    // decorative, so the table here is built to kill it.
    auto bvh = mh::io::readBvh(walk(), {});
    REQUIRE(bvh.has_value());

    // MEASURED: walk1 has 16 end sites, and `BvhReader` names each `<parent>_end`.
    std::string victim;
    size_t endSites = 0;
    for (const auto& joint : bvh->joints) {
        if (!joint.endSite) continue;
        ++endSites;
        if (victim.empty()) victim = joint.name;
    }
    CHECK(endSites == 16);
    REQUIRE_FALSE(victim.empty());

    RetargetMap names;
    names.toBone.emplace(victim, "head");

    // An end site carries no channels, so renaming one onto a real bone would
    // not pose anything -- it would make the returned count a lie, and could
    // collide the end site with a genuine joint of that name.
    CHECK(retargetJoints(*bvh, names) == 0);
    for (const auto& joint : bvh->joints) {
        if (!joint.endSite) continue;
        INFO("end site " << joint.name << " must keep its name");
        CHECK(joint.name != "head");
    }
}

TEST_CASE("the WRONG table is a near no-op, not a corruption", "[rig][retarget][retarget-import]") {
    // Choosing `mixamo` for a MakeHuman 1.x file must not half-pose the
    // character into something plausible-looking. MEASURED: the two tables
    // share exactly 5 source names -- `Hips`, `Spine1`, `Spine2`, `Neck`,
    // `Head` -- so the Mixamo table renames 5 of walk1's 75 joints and leaves
    // the other 70 alone. Pinned as a number and by name: if a future table
    // edit widens that overlap, the wrong choice starts producing a torso that
    // moves while the limbs do not, which is far harder to notice than nothing
    // moving at all.
    auto bvh = mh::io::readBvh(walk(), {});
    REQUIRE(bvh.has_value());
    const auto mixamo = loadRetargetMap(dataDir() / "rigs" / "mixamo_retarget.json");
    REQUIRE(mixamo.has_value());

    const size_t renamed = retargetJoints(*bvh, *mixamo);
    CHECK(renamed == 5);

    const auto skel  = superset();
    const auto mh1   = mh1Table();
    const auto wrong = bonesDrivenBy(walk(), skel, &*mixamo);
    const auto right = bonesDrivenBy(walk(), skel, &mh1);
    REQUIRE(wrong.has_value());
    REQUIRE(right.has_value());
    CHECK(*wrong == 5);
    CHECK(*right == 59);
}

TEST_CASE("no rename collides with a joint the table leaves alone",
          "[rig][retarget][retarget-import]") {
    // A rename is in-place, so a target that happened to equal some OTHER
    // joint's name would leave the file with two joints of one name, and
    // `makePoseUnits` keeps whichever it met first. MEASURED clean today
    // (walk1's names are `UpArm_L`-style and the targets are `shoulder01.L`-
    // style), and pinned so that a table edit which breaks it says so.
    auto bvh = mh::io::readBvh(walk(), {});
    REQUIRE(bvh.has_value());
    retargetJoints(*bvh, mh1Table());

    std::set<std::string> seen;
    for (const auto& joint : bvh->joints) {
        if (joint.endSite) continue;
        INFO("duplicate joint name after retargeting: " << joint.name);
        CHECK(seen.insert(joint.name).second);
    }
}

// --------------------------------------------------------- integration

TEST_CASE("the shipped walk goes from driving 0 bones to driving 59",
          "[rig][retarget][retarget-import][integration]") {
    const auto skel = superset();
    const auto map  = mh1Table();

    // The state this whole milestone exists to end, pinned so that a change
    // which accidentally "fixes" it elsewhere is visible here.
    const auto before = bonesDrivenBy(walk(), skel);
    REQUIRE(before.has_value());
    CHECK(*before == 0);

    const auto after = bonesDrivenBy(walk(), skel, &map);
    REQUIRE(after.has_value());
    CHECK(*after == 59);
}

TEST_CASE("the frames stop being identical", "[rig][retarget][retarget-import][integration]") {
    // A rename that produced plausible bone names while posing nothing would
    // pass every check above. This is the one that cannot be satisfied by
    // strings: two different frames of a walk cycle must give different
    // transforms once the file actually drives bones.
    const auto skel = superset();
    const auto map  = mh1Table();

    // MEASURED: walk1.bvh holds 14 frames.
    const auto rest0 = loadBodyPoseFrame(walk(), skel, 0);
    const auto rest7 = loadBodyPoseFrame(walk(), skel, 7);
    REQUIRE(rest0.has_value());
    REQUIRE(rest7.has_value());
    // Untargeted, every bone is identity in every frame, so frame 0 and frame 7
    // are the same rest pose.
    CHECK(samePose(*rest0, *rest7));

    const auto posed0 = loadBodyPoseFrame(walk(), skel, 0, &map);
    const auto posed7 = loadBodyPoseFrame(walk(), skel, 7, &map);
    REQUIRE(posed0.has_value());
    REQUIRE(posed7.has_value());
    CHECK_FALSE(samePose(*posed0, *posed7));

    // And posed is not merely different from itself -- it must differ from the
    // rest pose too, or the "pose" is still nothing happening.
    CHECK_FALSE(samePose(*posed0, *rest0));

    // Bone count is unchanged: retargeting renames joints, it never resizes the
    // rig.
    CHECK(posed0->size() == skel.boneCount());
    CHECK(posed7->size() == skel.boneCount());
}

// ---------------------------------------------------------- regression

TEST_CASE("omitting the table is exactly the old behaviour",
          "[rig][retarget][retarget-import][regression]") {
    // Every existing caller passes no table. The defaulted parameter must not
    // have changed what they get.
    const auto skel = superset();

    const auto driven = bonesDrivenBy(walk(), skel);
    REQUIRE(driven.has_value());
    CHECK(*driven == 0);

    const auto explicitNull = bonesDrivenBy(walk(), skel, nullptr);
    REQUIRE(explicitNull.has_value());
    CHECK(*explicitNull == *driven);

    const auto a = loadBodyPoseFrame(walk(), skel, 3);
    const auto b = loadBodyPoseFrame(walk(), skel, 3, nullptr);
    REQUIRE(a.has_value());
    REQUIRE(b.has_value());
    CHECK(samePose(*a, *b));
}

TEST_CASE("a retargeted load still refuses a multi-frame file as a POSE",
          "[rig][retarget][retarget-import][regression]") {
    // `loadBodyPose` refuses an animation outright, and retargeting must not
    // become a way around that -- a walk cycle silently becoming "the pose" is
    // the exact danger the refusal exists for.
    const auto skel = superset();
    const auto map  = mh1Table();
    const auto pose = loadBodyPose(walk(), skel, &map);
    CHECK_FALSE(pose.has_value());
}
