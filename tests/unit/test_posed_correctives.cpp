// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Correctives inside `rig::poseMesh` -- the last joint in the pipeline, and the
// one place the ORDER of operations has to be right.
//
// `poseMesh` already does three things in sequence: re-fit the skeleton to the
// morphed mesh, capture the rest vertices for a live-rig export, then skin. A
// corrective belongs between the capture and the skinning (directive 12.2:
// pre-skin, in rest space), and NOT before the re-fit:
//
//   RE-FIT FIRST, on the UNCORRECTED mesh. `updateJoints` makes the skeleton
//       follow the body. A corrective is a pose-driven bulge, not body shape,
//       so fitting the rig to it would move the joints the corrective is
//       driven BY -- a feedback loop where a shoulder bulge shifts the
//       shoulder, which changes the signal, which changes the bulge.
//   REST CAPTURE STAYS UNCORRECTED. `restCoords` is what a live-rig export
//       ships for a consumer to pose themselves. Baking a pose-specific bulge
//       into something labelled "rest" would carry a raised-arm deltoid into a
//       lowered arm on the consumer's side. The consequence is stated rather
//       than hidden: a live-rig export does not carry correctives at all,
//       because a consumer without a PSD runtime cannot evaluate them.
//
// The application cannot do this for itself by pre-deforming the mesh: it would
// have to hand `poseMesh` the corrected vertices, and then the re-fit would run
// on them. That is why the runtime is passed IN rather than applied outside.
#include "makehuman/core/CorrectiveBlob.h"
#include "makehuman/core/CorrectiveCache.h"
#include "makehuman/core/ObjReader.h"
#include "makehuman/core/TopologyHash.h"
#include "makehuman/rig/CorrectiveRuntime.h"
#include "makehuman/rig/PoseUnits.h"
#include "makehuman/rig/PosedMesh.h"
#include "makehuman/rig/Skeleton.h"
#include "makehuman/rig/Skinning.h"
#include "makehuman/rig/VertexWeights.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

using namespace mh;
using Catch::Matchers::WithinAbs;

namespace {

std::filesystem::path dataPath(const char* rel) {
    return std::filesystem::path(MH_DATA_DIR) / rel;
}

struct Loaded {
    core::Mesh mesh;
    rig::PoseRig rig;
    uint64_t topology{};
};

Loaded shipped() {
    Loaded l;
    auto mesh = core::loadObj(dataPath("3dobjs/base.obj"));
    REQUIRE(mesh.has_value());
    l.mesh     = std::move(*mesh);
    l.topology = core::topologyHash(l.mesh);

    auto skel = rig::loadSkeleton(dataPath("rigs/default.mhskel"));
    REQUIRE(skel.has_value());
    REQUIRE(skel->updateJoints(l.mesh.coord()));
    REQUIRE(skel->buildRestMatrices());

    auto vw = rig::loadWeights(dataPath("rigs/default_weights.mhw"), l.mesh.vertexCount());
    REQUIRE(vw.has_value());
    l.rig.weights = vw->compile(*skel, 4);

    const auto pose = rig::loadBodyPose(dataPath("poses/tpose.bvh"), *skel);
    REQUIRE(pose.has_value());
    l.rig.localPose = rig::poseToBoneLocal(*skel, *pose);
    l.rig.skeleton  = std::move(*skel);
    return l;
}

/// A corrective that moves ONE vertex a long way, so where it ends up is
/// unambiguous, keyed at the T-pose's own signal so it is fully on.
struct Baked {
    std::vector<std::byte> bytes;
    std::filesystem::path dir;
};

Baked bakeOneVertex(const std::string& name, uint64_t topology, uint32_t vertex) {
    Baked b;
    b.dir = std::filesystem::temp_directory_path() / "mh_posed_corr" / name;
    std::filesystem::remove_all(b.dir);
    std::filesystem::create_directories(b.dir / "deltas");

    char hex[17];
    std::snprintf(hex, sizeof(hex), "%016llx", static_cast<unsigned long long>(topology));
    // The T-pose signal for upperarm01.L swing, measured in the pose-signal
    // chunk: (0.0307, 0, 0.5003). Keyed there, so the T-pose lands on it.
    std::ofstream(b.dir / "correctives.json")
        << "{\n  \"formatVersion\": 1,\n  \"topologyHash\": \"" << hex
        << "\",\n  \"kernel\": \"gaussian\",\n  \"radius\": 0.3,\n"
        << "  \"drivers\": [ { \"joint\": \"upperarm01.L\", \"component\": \"swing\" } ],\n"
        << "  \"poses\": [\n"
        << "    { \"name\": \"rest\", \"signal\": [0.0, 0.0, 0.0], "
           "\"delta\": \"deltas/rest.target\" },\n"
        << "    { \"name\": \"tpose\", \"signal\": [0.0307, 0.0, 0.5003], "
           "\"delta\": \"deltas/tpose.target\" }\n  ]\n}\n";
    std::ofstream(b.dir / "deltas/rest.target") << "1 0.000001 0.0 0.0\n";
    std::ofstream(b.dir / "deltas/tpose.target") << vertex << " 0.0 5.0 0.0\n";

    auto cache = core::loadOrCompileCorrectives(b.dir / "correctives.json");
    REQUIRE(cache.has_value());
    b.bytes = std::move(cache->bytes);
    return b;
}

constexpr uint32_t kMoved = 4000;

}  // namespace

TEST_CASE("a corrective changes the posed mesh and nothing else does", "[rig][posedcorrectives]") {
    Loaded plain = shipped();
    REQUIRE(rig::poseMesh(plain.mesh, plain.rig, {.method = rig::SkinningMethod::DualQuaternion})
                .has_value());

    Loaded with       = shipped();
    const Baked baked = bakeOneVertex("applies", with.topology, kMoved);
    const auto blob   = core::readCorrectiveBlob(baked.bytes);
    REQUIRE(blob.has_value());
    auto runtime =
        rig::CorrectiveRuntime::bind(*blob, with.rig.skeleton, with.topology, with.mesh.coord());
    REQUIRE(runtime.has_value());

    REQUIRE(rig::poseMesh(with.mesh, with.rig,
                          {.method = rig::SkinningMethod::DualQuaternion, .correctives = &*runtime})
                .has_value());

    // The corrective moved its vertex, and 5 dm is far past any float noise.
    const auto a = plain.mesh.coord();
    const auto b = with.mesh.coord();
    REQUIRE(a.size() == b.size());
    const double moved = std::abs(static_cast<double>(b[kMoved].y - a[kMoved].y));
    CHECK(moved > 1.0);

    // ...and NOTHING else moved. Bit-exact, over all 19,158 vertices: a
    // corrective that leaked into its neighbours through the skinning, or a
    // buffer that was not reset, shows up here and in no other assertion.
    size_t differing = 0;
    for (size_t i = 0; i < a.size(); ++i) {
        if (a[i].x != b[i].x || a[i].y != b[i].y || a[i].z != b[i].z) ++differing;
    }
    CHECK(differing == 1);
}

TEST_CASE("the rig is fitted to the body, not to the corrective", "[rig][posedcorrectives]") {
    // The feedback loop this ordering exists to prevent. `updateJoints` moves
    // the skeleton to follow the mesh it is given; if the corrective were
    // applied before the re-fit, a shoulder bulge would move the shoulder, and
    // the moved shoulder would change the signal that produced the bulge.
    //
    // Checked by putting the corrective on a vertex the rig reads -- and
    // asserting the joints land where they do WITHOUT it.
    Loaded plain = shipped();
    REQUIRE(rig::poseMesh(plain.mesh, plain.rig, {}).has_value());
    std::vector<foundation::Vec3> plainHeads;
    for (const rig::Bone& bone : plain.rig.skeleton.bones)
        plainHeads.push_back(bone.head);

    Loaded with       = shipped();
    const Baked baked = bakeOneVertex("refit", with.topology, kMoved);
    const auto blob   = core::readCorrectiveBlob(baked.bytes);
    REQUIRE(blob.has_value());
    auto runtime =
        rig::CorrectiveRuntime::bind(*blob, with.rig.skeleton, with.topology, with.mesh.coord());
    REQUIRE(runtime.has_value());
    REQUIRE(rig::poseMesh(with.mesh, with.rig, {.correctives = &*runtime}).has_value());

    REQUIRE(with.rig.skeleton.bones.size() == plainHeads.size());
    for (size_t i = 0; i < plainHeads.size(); ++i) {
        INFO(with.rig.skeleton.bones[i].name);
        CHECK(with.rig.skeleton.bones[i].head.x == plainHeads[i].x);
        CHECK(with.rig.skeleton.bones[i].head.y == plainHeads[i].y);
        CHECK(with.rig.skeleton.bones[i].head.z == plainHeads[i].z);
    }
}

TEST_CASE("a live-rig export ships the true rest, without the corrective",
          "[rig][posedcorrectives]") {
    // `restCoords` is what a live-rig export hands a consumer to pose
    // themselves. A corrective is pose-driven, and a consumer with no PSD
    // runtime cannot evaluate it -- so baking one into something labelled
    // "rest" would carry a raised-arm deltoid into a lowered arm.
    //
    // The consequence, stated rather than hidden: a live-rig export does not
    // carry correctives. A baked export does.
    Loaded with = shipped();
    const auto before =
        std::vector<foundation::Vec3>(with.mesh.coord().begin(), with.mesh.coord().end());
    const Baked baked = bakeOneVertex("liverig", with.topology, kMoved);
    const auto blob   = core::readCorrectiveBlob(baked.bytes);
    REQUIRE(blob.has_value());
    auto runtime =
        rig::CorrectiveRuntime::bind(*blob, with.rig.skeleton, with.topology, with.mesh.coord());
    REQUIRE(runtime.has_value());
    REQUIRE(rig::poseMesh(with.mesh, with.rig, {.correctives = &*runtime}).has_value());

    REQUIRE(with.rig.restCoords.size() == before.size());
    for (size_t i = 0; i < before.size(); ++i) {
        REQUIRE(with.rig.restCoords[i].x == before[i].x);
        REQUIRE(with.rig.restCoords[i].y == before[i].y);
        REQUIRE(with.rig.restCoords[i].z == before[i].z);
    }
}

TEST_CASE("no runtime is the same as before correctives existed", "[rig][posedcorrectives]") {
    // The default has to be bit-identical, or every existing export moves.
    Loaded a = shipped();
    Loaded b = shipped();
    REQUIRE(rig::poseMesh(a.mesh, a.rig, {}).has_value());
    REQUIRE(rig::poseMesh(b.mesh, b.rig, {.correctives = nullptr}).has_value());
    const auto x = a.mesh.coord();
    const auto y = b.mesh.coord();
    REQUIRE(x.size() == y.size());
    for (size_t i = 0; i < x.size(); ++i) {
        REQUIRE(x[i].x == y[i].x);
        REQUIRE(x[i].y == y[i].y);
        REQUIRE(x[i].z == y[i].z);
    }
}

TEST_CASE("a mesh with a different vertex count is refused", "[rig][posedcorrectives]") {
    // An API contract, not a path the application takes. I first wrote that
    // `--subdivide` reaches this and then ran it: it does not. The application
    // poses the BASE mesh and subdivides afterwards, so the vertex count never
    // changes under a bound runtime -- and correctives propagate through the
    // subdivision, measured, 1,239 of 54,578 subdivided vertices differing with
    // the flag on.
    //
    // The guard is still worth having: a caller that binds to one mesh and
    // poses another gets a refusal instead of deltas landing on whatever
    // vertices happen to share those indices.
    Loaded with       = shipped();
    const Baked baked = bakeOneVertex("count", with.topology, kMoved);
    const auto blob   = core::readCorrectiveBlob(baked.bytes);
    REQUIRE(blob.has_value());
    auto runtime =
        rig::CorrectiveRuntime::bind(*blob, with.rig.skeleton, with.topology, with.mesh.coord());
    REQUIRE(runtime.has_value());

    const std::vector<foundation::Vec3> shorter(with.mesh.vertexCount() - 1);
    CHECK_FALSE(runtime->setRest(shorter));
    const std::vector<foundation::Vec3> longer(with.mesh.vertexCount() + 1);
    CHECK_FALSE(runtime->setRest(longer));

    // The right length still works, so the guard is not refusing everything.
    CHECK(runtime->setRest(with.mesh.coord()));
}

TEST_CASE("the corrective follows a morphed body", "[rig][posedcorrectives]") {
    // The character-static path: a slider moves, the shaped rest mesh is
    // rebuilt, and the runtime has to deform the NEW shape. Binding once at
    // startup and never updating would apply the deltas to the positions the
    // body had when the application opened.
    Loaded with       = shipped();
    const Baked baked = bakeOneVertex("morphed", with.topology, kMoved);
    const auto blob   = core::readCorrectiveBlob(baked.bytes);
    REQUIRE(blob.has_value());
    auto runtime =
        rig::CorrectiveRuntime::bind(*blob, with.rig.skeleton, with.topology, with.mesh.coord());
    REQUIRE(runtime.has_value());

    // Stand in for a morph: shift the whole body. Topology is unchanged, so the
    // corrective still applies -- it is the POSITIONS that moved.
    std::vector<foundation::Vec3> shifted(with.mesh.coord().begin(), with.mesh.coord().end());
    for (foundation::Vec3& v : shifted)
        v.x += 3.0F;

    Loaded plain = shipped();
    REQUIRE(plain.mesh.changeCoords(shifted));
    REQUIRE(with.mesh.changeCoords(shifted));
    REQUIRE(rig::poseMesh(plain.mesh, plain.rig, {}).has_value());
    REQUIRE(rig::poseMesh(with.mesh, with.rig, {.correctives = &*runtime}).has_value());

    // EXACTLY ONE vertex differs from the uncorrected run on the same morphed
    // body -- the one the corrective moves.
    //
    // Counting is what makes this test the one it claims to be. Asserting only
    // that the corrective's own vertex moved passes even when the runtime is
    // still deforming the body it saw at BIND time: the delta is applied either
    // way, and every OTHER vertex is then skinned from pre-morph positions and
    // lands 3 dm out. The first version of this case asserted exactly that and
    // a mutation removing `setRest` survived it.
    const auto a = plain.mesh.coord();
    const auto b = with.mesh.coord();
    REQUIRE(a.size() == b.size());
    size_t differing = 0;
    for (size_t i = 0; i < a.size(); ++i) {
        if (a[i].x != b[i].x || a[i].y != b[i].y || a[i].z != b[i].z) ++differing;
    }
    CHECK(differing == 1);
    CHECK(std::abs(static_cast<double>(b[kMoved].y - a[kMoved].y)) > 1.0);
}
