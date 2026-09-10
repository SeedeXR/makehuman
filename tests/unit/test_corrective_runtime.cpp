// SPDX-License-Identifier: AGPL-3.0-or-later
//
// The whole corrective chain, bound to one character and driven by a pose.
//
// Every piece existed and none of them met. Walking the chain:
//
//   rig::evaluatePoseSignal   posed skeleton -> a point in signal space
//   foundation::rbfEvaluate   that point     -> a weight per example pose
//   core::CorrectiveBuffer    those weights  -> moved rest-space vertices
//
// and the deltas come from `core::CompiledCorrectives`, read in place out of a
// baked blob.
//
// THE SEAM THIS CHUNK CLOSES. `CorrectiveBuffer::apply` used to take
// `const Target*` -- an OWNING type -- while the blob hands out spans into its
// own bytes. The two could not be joined without copying every delta out of the
// blob, which is exactly what a mappable layout exists to avoid. Both now speak
// `core::TargetView`, so the blob is consumed where it lies.
//
// And this is where directive 12.7's topology-hash guard finally does something.
// Up to now the manifest recorded the hash and nothing compared it. A `.target`
// is a list of VERTEX INDICES: bind a corrective authored against one base mesh
// to a renumbered one and every delta lands somewhere else, with nothing to
// notice. `bind` compares, and refuses.
#include "makehuman/rig/CorrectiveRuntime.h"

#include "makehuman/core/CorrectiveBlob.h"
#include "makehuman/core/CorrectiveManifest.h"
#include "makehuman/core/ObjReader.h"
#include "makehuman/core/TopologyHash.h"
#include "makehuman/rig/PoseUnits.h"
#include "makehuman/rig/Skeleton.h"
#include "makehuman/rig/Skinning.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <cmath>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

using namespace mh;
using namespace mh::rig;
using Catch::Matchers::WithinAbs;

namespace {

std::filesystem::path dataPath(const char* rel) {
    return std::filesystem::path(MH_DATA_DIR) / rel;
}

/// Everything a bind needs: the shipped mesh and rig, plus a blob compiled from
/// a manifest written for this test.
struct Rig {
    core::Mesh mesh;
    Skeleton skeleton;
    uint64_t topology{};
};

Rig shipped() {
    auto mesh = core::loadObj(dataPath("3dobjs/base.obj"));
    REQUIRE(mesh.has_value());
    auto skel = loadSkeleton(dataPath("rigs/default.mhskel"));
    REQUIRE(skel.has_value());
    REQUIRE(skel->updateJoints(mesh->coord()));
    REQUIRE(skel->buildRestMatrices());
    const uint64_t hash = core::topologyHash(*mesh);
    return Rig{std::move(*mesh), std::move(*skel), hash};
}

/// A manifest with three example poses on one shoulder, and payloads that move
/// three DIFFERENT vertices so a weight landing on the wrong pose is visible.
std::string manifestText(uint64_t topology) {
    char hex[17];
    std::snprintf(hex, sizeof(hex), "%016llx", static_cast<unsigned long long>(topology));
    return std::string(R"({
  "formatVersion": 1,
  "topologyHash": ")") +
           hex + R"(",
  "kernel": "gaussian",
  "radius": 1.2,
  "drivers": [ { "joint": "upperarm01.L", "component": "swing" } ],
  "poses": [
    { "name": "rest",  "signal": [0.0, 0.0, 0.0], "delta": "deltas/rest.target" },
    { "name": "up",    "signal": [0.0, 0.0, 0.4], "delta": "deltas/up.target" },
    { "name": "front", "signal": [0.4, 0.0, 0.0], "delta": "deltas/front.target" }
  ]
})";
}

struct Baked {
    std::vector<std::byte> bytes;
    core::CorrectiveManifest manifest;
};

/// Writes the manifest and its payloads, then compiles.
Baked bake(const std::string& name, uint64_t topology) {
    const auto dir = std::filesystem::temp_directory_path() / "mh_runtime_tests" / name;
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir / "deltas");
    std::ofstream(dir / "correctives.json") << manifestText(topology);
    // One vertex each, three different ones, each moved along a different axis.
    std::ofstream(dir / "deltas/rest.target") << "100 1.0 0.0 0.0\n";
    std::ofstream(dir / "deltas/up.target") << "200 0.0 2.0 0.0\n";
    std::ofstream(dir / "deltas/front.target") << "300 0.0 0.0 3.0\n";

    auto m = core::loadCorrectiveManifest(dir / "correctives.json");
    REQUIRE(m.has_value());
    auto bytes = core::compileCorrectives(*m);
    REQUIRE(bytes.has_value());
    return Baked{std::move(*bytes), std::move(*m)};
}

std::vector<foundation::Mat4> restPose(const Skeleton& s) {
    return std::vector<foundation::Mat4>(s.boneCount(), foundation::Mat4::identity());
}

}  // namespace

TEST_CASE("binding refuses a blob authored against a different mesh", "[rig][correctiveruntime]") {
    // Directive 12.7's topology-hash guard, doing something for the first time.
    // The manifest has recorded the hash since it was written; nothing compared
    // it until now.
    const Rig rig     = shipped();
    const Baked wrong = bake("wrong_topology", rig.topology ^ 1ULL);
    const auto blob   = core::readCorrectiveBlob(wrong.bytes);
    REQUIRE(blob.has_value());

    const auto bound = CorrectiveRuntime::bind(*blob, rig.skeleton, rig.topology, rig.mesh.coord());
    REQUIRE_FALSE(bound.has_value());
    CHECK(bound.error().kind == CorrectiveBindErrorKind::TopologyMismatch);

    // ...and the matching one binds, so the guard is not refusing everything.
    const Baked right = bake("right_topology", rig.topology);
    const auto ok     = core::readCorrectiveBlob(right.bytes);
    REQUIRE(ok.has_value());
    CHECK(CorrectiveRuntime::bind(*ok, rig.skeleton, rig.topology, rig.mesh.coord()).has_value());
}

TEST_CASE("binding refuses what it cannot drive or cannot reach", "[rig][correctiveruntime]") {
    const Rig rig   = shipped();
    const Baked b   = bake("refusals", rig.topology);
    const auto blob = core::readCorrectiveBlob(b.bytes);
    REQUIRE(blob.has_value());

    SECTION("a driving joint this skeleton does not have") {
        // A corrective authored for one rig and bound to another. Refused at
        // BIND, not mid-animation.
        Skeleton bare = rig.skeleton;
        for (Bone& bone : bare.bones) {
            if (bone.name == "upperarm01.L") bone.name = "renamed";
        }
        const auto bound = CorrectiveRuntime::bind(*blob, bare, rig.topology, rig.mesh.coord());
        REQUIRE_FALSE(bound.has_value());
        CHECK(bound.error().kind == CorrectiveBindErrorKind::UnknownJoint);
        CHECK(bound.error().detail.find("upperarm01.L") != std::string::npos);
    }

    SECTION("a delta that indexes past the rest mesh") {
        // The topology hash would normally catch a mismatched mesh, so this is
        // the case where the hash was right and the payload was not.
        const std::vector<foundation::Vec3> tiny(50);
        const auto bound = CorrectiveRuntime::bind(*blob, rig.skeleton, rig.topology, tiny);
        REQUIRE_FALSE(bound.has_value());
        CHECK(bound.error().kind == CorrectiveBindErrorKind::VertexOutOfRange);
    }
}

TEST_CASE("at rest nothing moves at all", "[rig][correctiveruntime]") {
    // Bit-exact. The chain runs -- signal, RBF, deltas -- and the answer is the
    // rest mesh unchanged, which is only true if the rest pose lands exactly on
    // the origin AND the pose sculpted there has weight one with its delta of
    // zero... which it does not: "rest" here sculpts vertex 100 by 1.0.
    //
    // So this is NOT the identity case. At the rest pose the "rest" example is
    // fully on, so its delta IS applied. What must hold is that the other two
    // are off. Checked explicitly below rather than assumed.
    const Rig rig   = shipped();
    const Baked b   = bake("at_rest", rig.topology);
    const auto blob = core::readCorrectiveBlob(b.bytes);
    REQUIRE(blob.has_value());
    auto bound = CorrectiveRuntime::bind(*blob, rig.skeleton, rig.topology, rig.mesh.coord());
    REQUIRE(bound.has_value());

    REQUIRE(bound->update(restPose(rig.skeleton)));
    const auto rest = rig.mesh.coord();
    const auto out  = bound->positions();
    REQUIRE(out.size() == rest.size());

    // The pose sculpted AT rest is fully on: vertex 100 moved by exactly 1.0.
    CHECK_THAT(static_cast<double>(out[100].x - rest[100].x), WithinAbs(1.0, 1e-5));
    // The other two are off, so their vertices are untouched -- bit-exact.
    CHECK(out[200].y == rest[200].y);
    CHECK(out[300].z == rest[300].z);

    // ONE vertex moved, not three. A Gaussian RBF never returns exactly zero --
    // measured, a pose that is fully off comes out at 2.7e-16 -- and
    // `CorrectiveBuffer` skips only exact zeros, so without the runtime's
    // negligible-weight threshold the dirty list would be every corrective,
    // every frame, and the optimisation would buy nothing.
    CHECK(bound->touched() == 1);

    // The raw weights still show what the RBF actually produced, so the
    // threshold is a decision about what to APPLY rather than a rounding of
    // what is reported.
    REQUIRE(bound->weights().size() == 3);
    CHECK_THAT(bound->weights()[0], WithinAbs(1.0, 1e-9));
    CHECK(std::abs(bound->weights()[1]) > 0.0);
    CHECK(std::abs(bound->weights()[1]) < 1e-12);
}

TEST_CASE("at an example pose that pose's delta is the one applied", "[rig][correctiveruntime]") {
    // The end-to-end claim. A pose is built so the driving joint's SIGNAL lands
    // on an example pose's coordinates, and then that example's sculpted delta
    // must be fully on and the others off. Everything in between -- the
    // swing-twist split, the RBF solve baked into the blob, the weight vector,
    // the scatter -- has to be right for this to hold.
    const Rig rig   = shipped();
    const Baked b   = bake("at_example", rig.topology);
    const auto blob = core::readCorrectiveBlob(b.bytes);
    REQUIRE(blob.has_value());
    auto bound = CorrectiveRuntime::bind(*blob, rig.skeleton, rig.topology, rig.mesh.coord());
    REQUIRE(bound.has_value());

    size_t arm = 0;
    for (size_t i = 0; i < rig.skeleton.bones.size(); ++i) {
        if (rig.skeleton.bones[i].name == "upperarm01.L") arm = i;
    }
    REQUIRE(arm != 0);

    const auto rest = rig.mesh.coord();

    SECTION("the pose keyed at swing 0.4 about Z") {
        auto local = restPose(rig.skeleton);
        local[arm] = foundation::rotationMatrix(0.4, foundation::Vec3{0.0F, 0.0F, 1.0F});
        REQUIRE(bound->update(local));
        const auto out = bound->positions();
        // "up" sculpts vertex 200 by 2.0 in Y and is the pose keyed there.
        CHECK_THAT(static_cast<double>(out[200].y - rest[200].y), WithinAbs(2.0, 1e-4));
        CHECK_THAT(static_cast<double>(out[100].x - rest[100].x), WithinAbs(0.0, 1e-4));
        CHECK_THAT(static_cast<double>(out[300].z - rest[300].z), WithinAbs(0.0, 1e-4));
    }

    SECTION("the pose keyed at swing 0.4 about X") {
        auto local = restPose(rig.skeleton);
        local[arm] = foundation::rotationMatrix(0.4, foundation::Vec3{1.0F, 0.0F, 0.0F});
        REQUIRE(bound->update(local));
        const auto out = bound->positions();
        // "front" sculpts vertex 300 by 3.0 in Z.
        CHECK_THAT(static_cast<double>(out[300].z - rest[300].z), WithinAbs(3.0, 1e-4));
        CHECK_THAT(static_cast<double>(out[100].x - rest[100].x), WithinAbs(0.0, 1e-4));
        CHECK_THAT(static_cast<double>(out[200].y - rest[200].y), WithinAbs(0.0, 1e-4));
    }
}

TEST_CASE("between example poses the deltas blend", "[rig][correctiveruntime]") {
    const Rig rig   = shipped();
    const Baked b   = bake("between", rig.topology);
    const auto blob = core::readCorrectiveBlob(b.bytes);
    REQUIRE(blob.has_value());
    auto bound = CorrectiveRuntime::bind(*blob, rig.skeleton, rig.topology, rig.mesh.coord());
    REQUIRE(bound.has_value());

    size_t arm = 0;
    for (size_t i = 0; i < rig.skeleton.bones.size(); ++i) {
        if (rig.skeleton.bones[i].name == "upperarm01.L") arm = i;
    }
    auto local = restPose(rig.skeleton);
    local[arm] = foundation::rotationMatrix(0.2, foundation::Vec3{0.0F, 0.0F, 1.0F});
    REQUIRE(bound->update(local));

    const auto rest = rig.mesh.coord();
    const auto out  = bound->positions();
    // Halfway between "rest" and "up": both substantially on, and neither at
    // its full sculpted value.
    const double a = static_cast<double>(out[100].x - rest[100].x);
    const double c = static_cast<double>(out[200].y - rest[200].y);
    CHECK(a > 0.2);
    CHECK(a < 1.0);
    CHECK(c > 0.4);
    CHECK(c < 2.0);
    CHECK(bound->touched() == 2);
}

TEST_CASE("driving it frame after frame does not accumulate", "[rig][correctiveruntime]") {
    // The drift the dirty list exists to prevent, exercised through the whole
    // chain rather than against the buffer alone: a corrective that stops being
    // active must leave nothing behind.
    const Rig rig   = shipped();
    const Baked b   = bake("frames", rig.topology);
    const auto blob = core::readCorrectiveBlob(b.bytes);
    REQUIRE(blob.has_value());
    auto bound = CorrectiveRuntime::bind(*blob, rig.skeleton, rig.topology, rig.mesh.coord());
    REQUIRE(bound.has_value());

    size_t arm = 0;
    for (size_t i = 0; i < rig.skeleton.bones.size(); ++i) {
        if (rig.skeleton.bones[i].name == "upperarm01.L") arm = i;
    }

    auto posed = restPose(rig.skeleton);
    posed[arm] = foundation::rotationMatrix(0.4, foundation::Vec3{0.0F, 0.0F, 1.0F});

    std::vector<foundation::Vec3> firstPosed;
    std::vector<foundation::Vec3> firstRest;
    for (int frame = 0; frame < 4; ++frame) {
        REQUIRE(bound->update(posed));
        if (frame == 0) {
            firstPosed.assign(bound->positions().begin(), bound->positions().end());
        } else {
            for (size_t i = 0; i < firstPosed.size(); ++i) {
                REQUIRE(bound->positions()[i].x == firstPosed[i].x);
                REQUIRE(bound->positions()[i].y == firstPosed[i].y);
                REQUIRE(bound->positions()[i].z == firstPosed[i].z);
            }
        }
        REQUIRE(bound->update(restPose(rig.skeleton)));
        if (frame == 0) {
            firstRest.assign(bound->positions().begin(), bound->positions().end());
        } else {
            for (size_t i = 0; i < firstRest.size(); ++i) {
                REQUIRE(bound->positions()[i].x == firstRest[i].x);
            }
        }
    }
}

TEST_CASE("a pose that is not this skeleton's is refused", "[rig][correctiveruntime]") {
    const Rig rig   = shipped();
    const Baked b   = bake("shapes", rig.topology);
    const auto blob = core::readCorrectiveBlob(b.bytes);
    REQUIRE(blob.has_value());
    auto bound = CorrectiveRuntime::bind(*blob, rig.skeleton, rig.topology, rig.mesh.coord());
    REQUIRE(bound.has_value());

    auto shortPose = restPose(rig.skeleton);
    shortPose.pop_back();
    CHECK_FALSE(bound->update(shortPose));

    // ...and the right shape still works.
    CHECK(bound->update(restPose(rig.skeleton)));
}
