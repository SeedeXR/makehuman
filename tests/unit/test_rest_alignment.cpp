// SPDX-License-Identifier: AGPL-3.0-or-later
//
// The rest-offset compensation, pinned bone by bone.
//
// The no-tear gate cannot see this one. Removing the correction leaves the
// mesh perfectly whole and merely puts every bone in the wrong place -- worst
// visible stretch 3.814x against 3.065x with it, both comfortably under that
// gate's 5.0 bar. "Intact but wrong" needs its own assertions, and this file
// is them.
//
// Every number here was MEASURED, and each one is a version of this code that
// was built and rejected:
//   * the arms at 42-53 degrees are the real T-pose/A-pose difference, and are
//     what the correction exists to close;
//   * `hips` must be identity -- taking the first child of a BRANCHING joint
//     put it 150.7 degrees out and tore the pelvis open (3.8x -> 7.4x);
//   * `neck01` must be small -- comparing this bone's own direction against
//     the source's whole neck put it 22.2 degrees out, when the file itself
//     holds the head 5.3 degrees from vertical.
#include "makehuman/core/Mesh.h"
#include "makehuman/core/ObjReader.h"
#include "makehuman/foundation/Types.h"
#include "makehuman/io/BvhReader.h"
#include "makehuman/rig/PoseUnits.h"
#include "makehuman/rig/RetargetMap.h"
#include "makehuman/rig/Skeleton.h"
#include "makehuman/rig/Skinning.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <string>
#include <vector>

using namespace mh;
using Catch::Matchers::WithinAbs;
namespace fs = std::filesystem;

namespace {

fs::path dataDir() {
    return fs::path(MH_DATA_DIR);
}

/// How far @p m turns, in degrees. The correction is a pure rotation, so the
/// trace gives its angle without caring about the axis.
double turnDegrees(const foundation::Mat4& m) {
    const double trace = static_cast<double>(m.m[0][0]) + static_cast<double>(m.m[1][1]) +
                         static_cast<double>(m.m[2][2]);
    return std::acos(std::clamp((trace - 1.0) / 2.0, -1.0, 1.0)) * 57.29577951308232;
}

struct Aligned {
    rig::Skeleton skeleton;
    std::vector<foundation::Mat4> rotation;

    [[nodiscard]] double of(const std::string& bone) const {
        const auto at = std::ranges::find_if(skeleton.bones,
                                             [&](const rig::Bone& b) { return b.name == bone; });
        REQUIRE(at != skeleton.bones.end());
        return turnDegrees(rotation[static_cast<size_t>(at - skeleton.bones.begin())]);
    }
};

/// walk1 retargeted onto @p rig, which is the state `loadBodyPoseFrame` works
/// in: joints already renamed, so a joint and the bone it drives share a name.
Aligned alignedWalk(const std::string& rigName) {
    auto skel = rig::loadSkeleton(dataDir() / "rigs" / (rigName + ".mhskel"));
    REQUIRE(skel.has_value());
    auto mesh = core::loadObj(dataDir() / "3dobjs" / "base.obj");
    REQUIRE(mesh.has_value());
    REQUIRE(skel->updateJoints(mesh->coord()));
    REQUIRE(skel->buildRestMatrices());

    auto bvh = io::readBvh(dataDir() / "animations" / "walks" / "walk1.bvh");
    REQUIRE(bvh.has_value());
    const auto table = rig::loadRetargetMap(dataDir() / "rigs" / "makehuman1_retarget.json");
    REQUIRE(table.has_value());
    rig::retargetJoints(*bvh, *table);

    // Computed BEFORE the move: a braced init evaluates left to right, so
    // moving the skeleton first hands restAlignment a moved-from one.
    auto rotation = rig::restAlignment(*bvh, *skel);
    return Aligned{std::move(*skel), std::move(rotation)};
}

}  // namespace

TEST_CASE("the arms carry the T-pose to A-pose difference", "[rig][retarget][restalign]") {
    const Aligned a = alignedWalk("mixamo_superset");

    // THE correction. walk1 rests in a T-pose -- its `UpArm_L` offset is 0.2
    // degrees below horizontal -- and this rig rests in an A-pose at 28.6,
    // both read from the reference's own parser. What the file calls "swing
    // the arm down 81 degrees" has to start from where the file's arm was.
    CHECK_THAT(a.of("upperarm01.L"), WithinAbs(45.0, 3.0));
    CHECK_THAT(a.of("upperarm01.R"), WithinAbs(45.0, 3.0));
    CHECK_THAT(a.of("lowerarm01.L"), WithinAbs(52.9, 3.0));
    CHECK_THAT(a.of("lowerarm01.R"), WithinAbs(52.9, 3.0));
}

TEST_CASE("a branching source joint is left alone", "[rig][retarget][restalign]") {
    const Aligned a = alignedWalk("mixamo_superset");

    // `Hips` carries the spine and both legs. There is no "bone below" it, and
    // picking whichever the file lists first is picking by file order: it put
    // `hips` 150.7 degrees out, which rendered as the pelvis torn open and was
    // MEASURED as the worst edge in the whole mesh.
    //
    // The root is in the same position for the same reason, and neither may
    // drift back above a degree or two.
    //
    // HONEST LIMIT: these two assertions do not, by themselves, gate the
    // branching rule. Measured -- with the rule removed they still pass,
    // because comparing head to head rather than following a child's offset
    // is robust enough that even the wrong child gives `hips` a plausible
    // direction. What catches it is `test_animation_no_tear.cpp`: without the
    // rule, 90 of its 551 assertions fail. Recorded here so the next reader
    // does not mistake these for the control they are not.
    CHECK(a.of("hips") < 2.0);
    CHECK(a.of("root") < 2.0);
}

TEST_CASE("the neck is compared against the whole neck", "[rig][retarget][restalign]") {
    const Aligned a = alignedWalk("mixamo_superset");

    // This rig splits the neck into three bones where walk1 has one, so
    // comparing `neck01`'s own direction against the source's `Neck`->`Head`
    // span measured a third of our neck against the whole of theirs and came
    // out 22.2 degrees, tipping the head back. Spanning the same anatomy in
    // both skeletons brings it to 4.5. The same fix takes `clavicle.L` from
    // 24.0 degrees to 2.5.
    //
    // Not asserted as "small because small is nice": the file holds the head
    // 5.3 degrees from vertical, so anything like 22 degrees at the neck is
    // this port inventing a pose the animation does not contain.
    CHECK(a.of("neck01") < 8.0);
    CHECK(a.of("clavicle.L") < 8.0);
    CHECK(a.of("clavicle.R") < 8.0);
}

TEST_CASE("every rig gets the correction, not just the default one", "[rig][retarget][restalign]") {
    // The tear was present on BOTH shipped rigs and so is this; a fix gated on
    // one of them would leave the other quietly wrong.
    const Aligned def = alignedWalk("default");
    CHECK_THAT(def.of("upperarm01.L"), WithinAbs(45.0, 5.0));
    CHECK(def.of("neck01") < 8.0);
}

TEST_CASE("an undriven bone inherits its parent rather than snapping back",
          "[rig][retarget][restalign]") {
    const Aligned a = alignedWalk("mixamo_superset");

    // `upperarm02` is a twist bone; MakeHuman 1.x has no counterpart, so the
    // file never names it. Identity here would mean it kept OUR rest while its
    // parent turned 45 degrees to the source's -- the child refusing to follow
    // the parent, which tears the mesh at the very joints this fix exists to
    // straighten. It must match its parent instead.
    CHECK_THAT(a.of("upperarm02.L"), WithinAbs(a.of("upperarm01.L"), 0.01));
    CHECK_THAT(a.of("lowerarm02.L"), WithinAbs(a.of("lowerarm01.L"), 0.01));
}

// The one above pin what `restAlignment` COMPUTES. This pins that the pose
// loader actually applies it -- deleting the call left every assertion above
// green, because they ask the function directly. A gate has to read the
// artefact the program really builds.
TEST_CASE("the loaded pose hangs the arm where the walk puts it", "[rig][retarget][restalign]") {
    auto skel = rig::loadSkeleton(dataDir() / "rigs" / "mixamo_superset.mhskel");
    REQUIRE(skel.has_value());
    auto mesh = core::loadObj(dataDir() / "3dobjs" / "base.obj");
    REQUIRE(mesh.has_value());
    REQUIRE(skel->updateJoints(mesh->coord()));
    REQUIRE(skel->buildRestMatrices());
    const auto table = rig::loadRetargetMap(dataDir() / "rigs" / "makehuman1_retarget.json");
    REQUIRE(table.has_value());

    const auto model =
        rig::loadBodyPoseFrame(dataDir() / "animations" / "walks" / "walk1.bvh", *skel, 0, &*table);
    REQUIRE(model.has_value());
    const auto local    = rig::poseToBoneLocal(*skel, *model);
    const auto skinning = rig::computeSkinningMatrices(*skel, local);

    const auto at = std::ranges::find_if(
        skel->bones, [](const rig::Bone& b) { return b.name == "upperarm01.L"; });
    REQUIRE(at != skel->bones.end());
    // `skinning` is what maps a REST MODEL-space vertex to its posed place,
    // which is the matrix a model-space direction wants. `global` (that times
    // matRestGlobal) maps BONE-local coordinates and turned the arm 136
    // degrees the wrong way when used here.
    const size_t i           = static_cast<size_t>(at - skel->bones.begin());
    const foundation::Mat4 g = skinning[i];

    const foundation::Vec3 d = at->direction();
    const float l            = std::sqrt(foundation::dot(d, d));
    const foundation::Vec3 u{d.x / l, d.y / l, d.z / l};
    const foundation::Vec3 w{g.m[0][0] * u.x + g.m[0][1] * u.y + g.m[0][2] * u.z,
                             g.m[1][0] * u.x + g.m[1][1] * u.y + g.m[1][2] * u.z,
                             g.m[2][0] * u.x + g.m[2][1] * u.y + g.m[2][2] * u.z};
    const float wl = std::sqrt(foundation::dot(w, w));
    // Degrees from straight DOWN. A walking figure's upper arm hangs.
    const double fromDown =
        static_cast<double>(std::acos(std::clamp(-w.y / wl, -1.0F, 1.0F))) * 57.29577951308232;
    INFO("upper arm is " << fromDown << " degrees from straight down");
    CHECK(fromDown < 30.0);
}
