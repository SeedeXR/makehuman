// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Linear blend skinning, against the reference's own posed result.
//
// Regenerate with:
//     ./.venv-mh/bin/python tools/capture_fixture.py skinning

#include "makehuman/core/ObjReader.h"
#include "makehuman/rig/Skeleton.h"
#include "makehuman/rig/Skinning.h"
#include "makehuman/rig/VertexWeights.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <vector>

using namespace mh;

namespace {

constexpr size_t kVerts = 19158;
constexpr size_t kBones = 163;

std::filesystem::path fixtureDir() {
    return std::filesystem::path(MH_GOLDEN_DIR) / "skinning";
}

template <typename T>
std::vector<T> readBlob(const std::filesystem::path& p) {
    std::ifstream in(p, std::ios::binary | std::ios::ate);
    if (!in) return {};
    const auto bytes = static_cast<size_t>(in.tellg());
    in.seekg(0);
    std::vector<T> out(bytes / sizeof(T));
    in.read(reinterpret_cast<char*>(out.data()), static_cast<std::streamsize>(bytes));
    return out;
}

/// Reads a (count, 4, 4) float32 blob into Mat4s. Both sides are row-major, so
/// this is a straight copy.
std::vector<foundation::Mat4> readMatrices(const std::filesystem::path& p, size_t count) {
    const auto raw = readBlob<float>(p);
    REQUIRE(raw.size() == count * 16);
    std::vector<foundation::Mat4> out(count);
    for (size_t b = 0; b < count; ++b) {
        for (size_t r = 0; r < 4; ++r) {
            for (size_t c = 0; c < 4; ++c)
                out[b].m[r][c] = raw[(b * 4 + r) * 4 + c];
        }
    }
    return out;
}

struct Rig {
    core::Mesh mesh;
    rig::Skeleton skel;
    rig::CompiledWeights weights;
};

Rig loadRig() {
    auto mesh = core::loadObj(std::filesystem::path(MH_DATA_DIR) / "3dobjs" / "base.obj");
    REQUIRE(mesh.has_value());
    auto skel = rig::loadSkeleton(std::filesystem::path(MH_DATA_DIR) / "rigs" / "default.mhskel");
    REQUIRE(skel.has_value());
    REQUIRE(skel->updateJoints(mesh->coord()));
    REQUIRE(skel->buildRestMatrices());
    auto w = rig::loadWeights(std::filesystem::path(MH_DATA_DIR) / "rigs" / "default_weights.mhw",
                              mesh->vertexCount());
    REQUIRE(w.has_value());
    auto compiled = w->compile(*skel, 4);
    return Rig{std::move(*mesh), std::move(*skel), std::move(compiled)};
}

}  // namespace

// matPoseVerts is where the rest matrices, the parent chain and the inverse all
// have to agree. Compared element-wise against the reference under the same
// pose, so a wrong multiply order or a non-exact inverse shows up here rather
// than as a subtly wrong silhouette.
TEST_CASE("pose matrices match the reference", "[skinning][golden][parity]") {
    Rig r = loadRig();

    const auto matPose  = readMatrices(fixtureDir() / "mat_pose.bin", kBones);
    const auto expected = readMatrices(fixtureDir() / "pose_verts.bin", kBones);
    REQUIRE(r.skel.boneCount() == kBones);

    const auto pose = rig::computeSkinningMatrices(r.skel, matPose);
    REQUIRE(pose.size() == kBones);

    size_t bad  = 0;
    float worst = 0.0F;
    for (size_t b = 0; b < kBones; ++b) {
        for (size_t i = 0; i < 4; ++i) {
            for (size_t j = 0; j < 4; ++j) {
                const float d = std::abs(pose[b].m[i][j] - expected[b].m[i][j]);
                worst         = std::max(worst, d);
                if (d > 1e-4F) {
                    if (bad == 0) {
                        INFO("first mismatch at bone " << b << " (" << r.skel.bones[b].name << ") ["
                                                       << i << "][" << j << "]");
                        CHECK(d <= 1e-4F);
                    }
                    ++bad;
                }
            }
        }
    }
    INFO("worst pose-matrix delta " << worst);
    CHECK(bad == 0);
}

// The deformed mesh itself, vertex by vertex.
TEST_CASE("skinned vertices match the reference", "[skinning][golden][parity]") {
    Rig r = loadRig();

    const auto matPose = readMatrices(fixtureDir() / "mat_pose.bin", kBones);
    const auto pose    = rig::computeSkinningMatrices(r.skel, matPose);

    const auto expected = readBlob<float>(fixtureDir() / "skinned.bin");
    REQUIRE(expected.size() == kVerts * 3);

    std::vector<foundation::Vec3> out;
    REQUIRE(rig::skinPositions(r.mesh.coord(), r.weights, pose, out));
    REQUIRE(out.size() == kVerts);

    // Positions are decimetres through a float32 blend; 1e-4 dm is 10 microns.
    constexpr float kTol = 1e-4F;
    size_t bad           = 0;
    float worst          = 0.0F;
    for (size_t v = 0; v < kVerts; ++v) {
        const float dx = std::abs(out[v].x - expected[v * 3 + 0]);
        const float dy = std::abs(out[v].y - expected[v * 3 + 1]);
        const float dz = std::abs(out[v].z - expected[v * 3 + 2]);
        const float d  = std::max({dx, dy, dz});
        worst          = std::max(worst, d);
        if (d > kTol) {
            if (bad == 0) {
                INFO("first mismatch at vertex " << v);
                CHECK(d <= kTol);
            }
            ++bad;
        }
    }
    INFO("worst vertex delta " << worst);
    CHECK(bad == 0);
}

// Guards the fixture itself: if the pose ever became trivial, the parity test
// above would pass while testing nothing.
TEST_CASE("the captured pose actually deforms the mesh", "[skinning][parity]") {
    Rig r              = loadRig();
    const auto matPose = readMatrices(fixtureDir() / "mat_pose.bin", kBones);
    const auto pose    = rig::computeSkinningMatrices(r.skel, matPose);

    std::vector<foundation::Vec3> out;
    REQUIRE(rig::skinPositions(r.mesh.coord(), r.weights, pose, out));

    size_t moved  = 0;
    float maxDisp = 0.0F;
    for (size_t v = 0; v < kVerts; ++v) {
        const auto& a = r.mesh.coord()[v];
        const float d =
            std::sqrt((out[v].x - a.x) * (out[v].x - a.x) + (out[v].y - a.y) * (out[v].y - a.y) +
                      (out[v].z - a.z) * (out[v].z - a.z));
        if (d > 1e-6F) ++moved;
        maxDisp = std::max(maxDisp, d);
    }
    INFO("max displacement " << maxDisp);
    CHECK(moved > 18000);   // the reference moved 18,069
    CHECK(maxDisp > 3.0F);  // ~32 cm, so this is a real pose
}

// The identity pose must be a no-op. This is the property that catches a
// transposed matrix or a wrong inverse even without a fixture: if
// matPoseVerts != identity at rest, every vertex drifts.
TEST_CASE("the rest pose does not move anything", "[skinning][parity]") {
    Rig r = loadRig();

    const auto pose = rig::computeSkinningMatrices(r.skel, {});  // empty span == all identity
    for (size_t b = 0; b < r.skel.boneCount(); ++b) {
        CAPTURE(r.skel.bones[b].name);
        for (size_t i = 0; i < 4; ++i) {
            for (size_t j = 0; j < 4; ++j) {
                const float want = (i == j) ? 1.0F : 0.0F;
                REQUIRE(std::abs(pose[b].m[i][j] - want) < 1e-4F);
            }
        }
    }

    std::vector<foundation::Vec3> out;
    REQUIRE(rig::skinPositions(r.mesh.coord(), r.weights, pose, out));

    float worst = 0.0F;
    for (size_t v = 0; v < kVerts; ++v) {
        const auto& a = r.mesh.coord()[v];
        worst         = std::max(
            {worst, std::abs(out[v].x - a.x), std::abs(out[v].y - a.y), std::abs(out[v].z - a.z)});
    }
    INFO("worst rest-pose drift " << worst);
    CHECK(worst < 1e-4F);
}

TEST_CASE("mismatched inputs are refused", "[skinning]") {
    Rig r           = loadRig();
    const auto pose = rig::computeSkinningMatrices(r.skel, {});
    std::vector<foundation::Vec3> out;

    // Fewer vertices than the weights describe.
    const std::vector<foundation::Vec3> tiny(10, foundation::Vec3{});
    CHECK_FALSE(rig::skinPositions(tiny, r.weights, pose, out));

    // A weight naming a bone the pose does not have.
    CHECK_FALSE(
        rig::skinPositions(r.mesh.coord(), r.weights, std::span<const foundation::Mat4>{}, out));
}
