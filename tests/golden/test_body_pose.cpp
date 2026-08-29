// SPDX-License-Identifier: AGPL-3.0-or-later
//
// A pose that loads without error and changes nothing is the failure that reads
// as success: makePoseUnits leaves identity wherever the BVH has no matching
// joint, so a name mismatch yields a complete, valid, entirely unposed rig.
// These tests measure the mesh instead of trusting the call.
#include "makehuman/core/Mesh.h"
#include "makehuman/core/ObjReader.h"
#include "makehuman/rig/PoseUnits.h"
#include "makehuman/rig/Skeleton.h"
#include "makehuman/rig/Skinning.h"
#include "makehuman/rig/VertexWeights.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <span>
#include <vector>

using namespace mh;
using Catch::Matchers::WithinAbs;

namespace {

std::filesystem::path data(const char* rel) {
    return std::filesystem::path(MH_DATA_DIR) / rel;
}

struct Extent {
    float minX{}, maxX{}, minY{}, maxY{};

    [[nodiscard]] float spanX() const { return maxX - minX; }

    [[nodiscard]] float spanY() const { return maxY - minY; }
};

/// Bounds over the body only. base.obj carries 138 helper groups whose geometry
/// (clothes volumes, a box around the head) reaches well past the skin, so a
/// bound over every vertex measures the helpers, not the figure.
Extent bodyExtent(const core::Mesh& mesh, std::span<const foundation::Vec3> verts) {
    // staticFaceMask is the same helper/joint-group rule the renderer uses, so
    // this measures exactly the figure that gets drawn and exported.
    const std::vector<uint8_t> visible = mesh.staticFaceMask();
    const size_t vpf                   = mesh.vertsPerPrimitive();
    const std::span<const uint32_t> fv = mesh.fvert();

    std::vector<bool> body(verts.size(), false);
    for (size_t f = 0; f < mesh.faceCount(); ++f) {
        if (visible[f] == 0) continue;
        for (size_t c = 0; c < vpf; ++c)
            body[fv[f * vpf + c]] = true;
    }
    Extent e{1e9F, -1e9F, 1e9F, -1e9F};
    for (size_t i = 0; i < verts.size(); ++i) {
        if (!body[i]) continue;
        e.minX = std::min(e.minX, verts[i].x);
        e.maxX = std::max(e.maxX, verts[i].x);
        e.minY = std::min(e.minY, verts[i].y);
        e.maxY = std::max(e.maxY, verts[i].y);
    }
    return e;
}

/// Reads a (n, 3) float32 blob captured from the reference.
std::vector<foundation::Vec3> readVec3Blob(const std::filesystem::path& p) {
    std::ifstream in(p, std::ios::binary | std::ios::ate);
    REQUIRE(in);
    const auto bytes = static_cast<size_t>(in.tellg());
    in.seekg(0);
    std::vector<float> raw(bytes / sizeof(float));
    in.read(reinterpret_cast<char*>(raw.data()), static_cast<std::streamsize>(bytes));
    REQUIRE(raw.size() % 3 == 0);
    std::vector<foundation::Vec3> out(raw.size() / 3);
    for (size_t i = 0; i < out.size(); ++i)
        out[i] = {raw[i * 3], raw[i * 3 + 1], raw[i * 3 + 2]};
    return out;
}

struct Rig {
    core::Mesh mesh;
    rig::Skeleton skel;
    rig::CompiledWeights weights;
};

Rig loadRig() {
    auto mesh = core::loadObj(data("3dobjs/base.obj"));
    REQUIRE(mesh.has_value());

    auto skel = rig::loadSkeleton(data("rigs/default.mhskel"));
    REQUIRE(skel.has_value());
    REQUIRE(skel->updateJoints(mesh->coord()));
    REQUIRE(skel->buildRestMatrices());

    auto w = rig::loadWeights(data("rigs/default_weights.mhw"), mesh->vertexCount());
    REQUIRE(w.has_value());
    auto compiled = w->compile(*skel, 4);
    return Rig{std::move(*mesh), std::move(*skel), std::move(compiled)};
}

}  // namespace

TEST_CASE("the shipped T-pose maps onto every bone of the default rig", "[pose]") {
    const Rig r     = loadRig();
    const auto pose = rig::loadBodyPose(data("poses/tpose.bvh"), r.skel);
    REQUIRE(pose.has_value());
    REQUIRE(pose->size() == r.skel.boneCount());

    // If the joint names did not match, every entry would be identity and the
    // pose would silently do nothing.
    const auto isIdentity = [](const foundation::Mat4& m) {
        for (size_t r = 0; r < 4; ++r) {
            for (size_t c = 0; c < 4; ++c) {
                const float want = (r == c) ? 1.0F : 0.0F;
                if (std::abs(m.m[r][c] - want) > 1e-5F) return false;
            }
        }
        return true;
    };
    const size_t moved = static_cast<size_t>(std::count_if(
        pose->begin(), pose->end(), [&](const foundation::Mat4& m) { return !isIdentity(m); }));
    INFO("bones the T-pose actually rotates: " << moved << " of " << pose->size());
    CHECK(moved > 20);
}

TEST_CASE("the T-pose raises the arms and the A-pose rest does not", "[pose]") {
    const Rig r = loadRig();

    // The rest pose IS the A-pose for this base mesh -- no file, no skinning.
    const Extent aPose = bodyExtent(r.mesh, r.mesh.coord());

    const auto pose = rig::loadBodyPose(data("poses/tpose.bvh"), r.skel);
    REQUIRE(pose.has_value());
    const auto local = rig::poseToBoneLocal(r.skel, *pose);
    REQUIRE(local.size() == r.skel.boneCount());
    const auto skinning = rig::computeSkinningMatrices(r.skel, local);
    std::vector<foundation::Vec3> posed;
    REQUIRE(rig::skinPositions(r.mesh.coord(), r.weights, skinning, posed));
    REQUIRE(posed.size() == r.mesh.vertexCount());

    const Extent tPose = bodyExtent(r.mesh, posed);

    INFO("A-pose span x=" << aPose.spanX() << " y=" << aPose.spanY());
    INFO("T-pose span x=" << tPose.spanX() << " y=" << tPose.spanY());

    // Arms out horizontally reach far further sideways. Height barely moves --
    // measured, not assumed: the figure is very slightly TALLER T-posed
    // (16.71 vs 16.66 dm), because the standing height is set by head-to-foot
    // either way and the raised shoulders add a little.
    CHECK(tPose.spanX() > aPose.spanX() + 5.0F);
    CHECK(std::abs(tPose.spanY() - aPose.spanY()) < 0.5F);

    // The reference's own answer for this exact file, over all vertices:
    // rest span x = 9.9464 dm, posed span x = 15.9893 dm (tests/golden/body_pose).
    // A T-pose reaches about as wide as the figure is tall.
    CHECK(tPose.spanX() > 15.0F);
}

TEST_CASE("the T-posed mesh matches the reference vertex for vertex", "[pose][golden][parity]") {
    // Regenerate with:
    //     ./.venv-mh/bin/python tools/capture_fixture.py body_pose
    const Rig r     = loadRig();
    const auto pose = rig::loadBodyPose(data("poses/tpose.bvh"), r.skel);
    REQUIRE(pose.has_value());

    const auto local    = rig::poseToBoneLocal(r.skel, *pose);
    const auto skinning = rig::computeSkinningMatrices(r.skel, local);
    std::vector<foundation::Vec3> posed;
    REQUIRE(rig::skinPositions(r.mesh.coord(), r.weights, skinning, posed));

    const auto expected =
        readVec3Blob(std::filesystem::path(MH_GOLDEN_DIR) / "body_pose" / "skinned.bin");
    REQUIRE(expected.size() == posed.size());

    float worst      = 0.0F;
    size_t worstVert = 0;
    for (size_t i = 0; i < posed.size(); ++i) {
        const float e =
            std::max({std::abs(posed[i].x - expected[i].x), std::abs(posed[i].y - expected[i].y),
                      std::abs(posed[i].z - expected[i].z)});
        if (e > worst) {
            worst     = e;
            worstVert = i;
        }
    }
    INFO("worst vertex " << worstVert << " differs by " << worst << " dm");
    // The whole figure moves up to 6.21 dm, so this is a parity check, not a
    // tolerance that anything would pass.
    CHECK(worst < 1e-3F);
}

TEST_CASE("skinning the rest pose leaves the mesh exactly where it was", "[pose]") {
    const Rig r = loadRig();
    // Identity pose through the whole skinning path: this is what makes the
    // A-pose comparison above meaningful rather than a comparison of two
    // different code paths.
    const auto skinning = rig::computeSkinningMatrices(r.skel, {});
    std::vector<foundation::Vec3> posed;
    REQUIRE(rig::skinPositions(r.mesh.coord(), r.weights, skinning, posed));

    float worst = 0.0F;
    for (size_t i = 0; i < posed.size(); ++i) {
        const auto& a = r.mesh.coord()[i];
        const auto& b = posed[i];
        worst = std::max({worst, std::abs(a.x - b.x), std::abs(a.y - b.y), std::abs(a.z - b.z)});
    }
    INFO("worst rest-pose round-trip error: " << worst << " dm");
    CHECK(worst < 1e-4F);
}

TEST_CASE("a multi-frame animation is rejected as a body pose", "[pose]") {
    const Rig r = loadRig();
    // The face pose-unit library is 60 frames. Silently taking its first frame
    // would load without complaint and freeze the figure into one expression.
    // (data/poses/benchmark.bvh will NOT do here -- it is a single frame too.)
    const auto pose = rig::loadBodyPose(data("poseunits/face-poseunits.bvh"), r.skel);
    REQUIRE_FALSE(pose.has_value());
    CHECK(pose.error().kind == rig::PoseUnitsErrorKind::FrameCountMismatch);
}

TEST_CASE("a missing pose file is reported, not ignored", "[pose]") {
    const Rig r     = loadRig();
    const auto pose = rig::loadBodyPose(data("poses/does-not-exist.bvh"), r.skel);
    REQUIRE_FALSE(pose.has_value());
    CHECK(pose.error().kind == rig::PoseUnitsErrorKind::NotFound);
}

TEST_CASE("a pose must be fitted to the rest mesh, never to a posed one", "[pose]") {
    // The trap this pins: loadBodyPose/poseToBoneLocal conjugate by the
    // skeleton's rest matrices, and those come from updateJoints(mesh). Fit the
    // rig to an already-posed mesh and the conversion happens in the previous
    // pose's frame. It does not fail -- it returns a complete, smooth, wrong
    // body. Switching pose to pose in the UI did exactly this.
    Rig r = loadRig();
    const std::vector<foundation::Vec3> rest(r.mesh.coord().begin(), r.mesh.coord().end());

    const auto poseOnce = [&](std::span<const foundation::Vec3> from) {
        rig::Skeleton skel = *rig::loadSkeleton(data("rigs/default.mhskel"));
        REQUIRE(skel.updateJoints(from));
        REQUIRE(skel.buildRestMatrices());
        const auto bvh = rig::loadBodyPose(data("poses/tpose.bvh"), skel);
        REQUIRE(bvh.has_value());
        const auto local    = rig::poseToBoneLocal(skel, *bvh);
        const auto skinning = rig::computeSkinningMatrices(skel, local);
        std::vector<foundation::Vec3> out;
        REQUIRE(rig::skinPositions(rest, r.weights, skinning, out));
        return out;
    };

    const std::vector<foundation::Vec3> correct = poseOnce(rest);

    // Now fit the SAME pose to the already-T-posed body, as the buggy path did.
    const std::vector<foundation::Vec3> wrong = poseOnce(correct);

    float worst = 0.0F;
    for (size_t i = 0; i < correct.size(); ++i) {
        worst =
            std::max({worst, std::abs(correct[i].x - wrong[i].x),
                      std::abs(correct[i].y - wrong[i].y), std::abs(correct[i].z - wrong[i].z)});
    }
    INFO("fitting to a posed mesh moves vertices by up to " << worst << " dm");
    // Decimetres: this is a gross, visible difference, not numerical drift --
    // which is exactly why "it looked fine" was not evidence.
    CHECK(worst > 1.0F);

    // And the correct path is the one that matches the reference.
    const auto expected =
        readVec3Blob(std::filesystem::path(MH_GOLDEN_DIR) / "body_pose" / "skinned.bin");
    REQUIRE(expected.size() == correct.size());
    float parity = 0.0F;
    for (size_t i = 0; i < correct.size(); ++i) {
        parity = std::max({parity, std::abs(correct[i].x - expected[i].x),
                           std::abs(correct[i].y - expected[i].y),
                           std::abs(correct[i].z - expected[i].z)});
    }
    CHECK(parity < 1e-3F);
}
