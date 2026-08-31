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
#include <set>
#include <span>
#include <string>
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

// --- Deformation quality of the generated ball bones ------------------------
//
// `tools/build_mixamo_superset.py` invents weights for a bone MakeHuman does
// not have, so there is no reference to compare against. CI gated the file for
// STALENESS -- is it current with its generator? -- but never for whether the
// rig deforms acceptably. That is the same gap that let 13 zero-length bones
// ship in a rig that could not skin at all.
//
// The honest measure is comparative: under the same mesh, the same linear blend
// skinning and the same bend angle, our generated crease must be no worse than
// a joint MakeHuman itself ships. Measured at 45 degrees:
//
//     ball.L/R        792 faces   min area ratio 0.364   7 flips (0.88%)
//     lowerarm01.L/R 1152 faces   min area ratio 0.023  22 flips (1.91%)
//
// Some collapse at a sharp bend is inherent to LBS -- the elbow shows more of
// it. Pinning the comparison rather than the absolute number keeps the test
// self-calibrating.
namespace {

using mh::foundation::Vec3;

struct Deformation {
    size_t faces{};
    size_t flips{};
    float minAreaRatio{1.0F};
};

Vec3 newellNormal(std::span<const Vec3> v, std::span<const uint32_t> face, float& area) {
    Vec3 n{};
    for (size_t i = 0; i < face.size(); ++i) {
        const Vec3& a = v[face[i]];
        const Vec3& b = v[face[(i + 1) % face.size()]];
        n.x += (a.y - b.y) * (a.z + b.z);
        n.y += (a.z - b.z) * (a.x + b.x);
        n.z += (a.x - b.x) * (a.y + b.y);
    }
    const float m = std::sqrt(n.x * n.x + n.y * n.y + n.z * n.z);
    area          = m * 0.5F;
    return (m > 1e-12F) ? Vec3{n.x / m, n.y / m, n.z / m} : Vec3{};
}

/// Rolls every bone whose name starts with @p prefix by @p degrees about X and
/// reports how the faces touching those bones deform.
Deformation rollAndMeasure(const std::filesystem::path& rigStem, const std::string& prefix,
                           float degrees, const core::Mesh& mesh) {
    Deformation d{};
    auto skel = rig::loadSkeleton(rigStem.string() + ".mhskel");
    REQUIRE(skel.has_value());
    REQUIRE(skel->updateJoints(mesh.coord()));
    REQUIRE(skel->buildRestMatrices());
    auto w = rig::loadWeights(rigStem.string() + "_weights.mhw", mesh.vertexCount());
    REQUIRE(w.has_value());
    const auto compiled = w->compile(*skel, 4);

    const float r = degrees * 3.14159265358979F / 180.0F;
    std::vector<foundation::Mat4> local(skel->boneCount(), foundation::Mat4::identity());
    std::set<size_t> rolled;
    for (size_t i = 0; i < skel->boneCount(); ++i) {
        if (skel->bones[i].name.rfind(prefix, 0) != 0) continue;
        auto& m   = local[i];
        m.m[1][1] = std::cos(r);
        m.m[1][2] = -std::sin(r);
        m.m[2][1] = std::sin(r);
        m.m[2][2] = std::cos(r);
        rolled.insert(i);
    }
    REQUIRE_FALSE(rolled.empty());

    std::vector<Vec3> posed;
    REQUIRE(rig::skinPositions(mesh.coord(), compiled, rig::computeSkinningMatrices(*skel, local),
                               posed));

    // Vertices the rolled bones actually influence.
    std::vector<uint8_t> touched(mesh.vertexCount(), 0U);
    const size_t per = compiled.influences;
    for (size_t v = 0; v < mesh.vertexCount(); ++v)
        for (size_t k = 0; k < per; ++k)
            if (compiled.weight[v * per + k] > 0.0F &&
                rolled.contains(compiled.boneIndex[v * per + k]))
                touched[v] = 1U;

    const auto fv    = mesh.fvert();
    const size_t vpp = mesh.vertsPerPrimitive();
    for (size_t f = 0; f + vpp <= fv.size(); f += vpp) {
        const std::span<const uint32_t> face{fv.data() + f, vpp};
        if (std::none_of(face.begin(), face.end(), [&](uint32_t i) { return touched[i] != 0U; }))
            continue;
        ++d.faces;
        float a0      = 0.0F;
        float a1      = 0.0F;
        const Vec3 n0 = newellNormal(mesh.coord(), face, a0);
        const Vec3 n1 = newellNormal(posed, face, a1);
        if (a0 < 1e-9F) continue;
        d.minAreaRatio = std::min(d.minAreaRatio, a1 / a0);
        if (n0.x * n1.x + n0.y * n1.y + n0.z * n1.z < 0.0F) ++d.flips;
    }
    return d;
}

}  // namespace

TEST_CASE("the generated ball crease is no worse than a shipped joint", "[rig][mixamo][crease]") {
    const std::filesystem::path rigs = std::filesystem::path(MH_DATA_DIR) / "rigs";
    if (!std::filesystem::exists(rigs / "mixamo_superset.mhskel")) return;

    auto mesh = core::loadObj(std::filesystem::path(MH_DATA_DIR) / "3dobjs" / "base.obj");
    REQUIRE(mesh.has_value());

    // A walking toe-off. Nothing may invert here -- this is the common case.
    const auto walk = rollAndMeasure(rigs / "mixamo_superset", "ball", 25.0F, *mesh);
    INFO("ball at 25 deg: " << walk.faces << " faces, " << walk.flips << " flips, min area ratio "
                            << walk.minAreaRatio);
    CHECK(walk.flips == 0);
    CHECK(walk.minAreaRatio > 0.5F);

    // A hard roll, against MakeHuman's own elbow under identical conditions.
    const auto ball       = rollAndMeasure(rigs / "mixamo_superset", "ball", 45.0F, *mesh);
    const auto elbow      = rollAndMeasure(rigs / "default", "lowerarm01", 45.0F, *mesh);
    const float ballRate  = static_cast<float>(ball.flips) / static_cast<float>(ball.faces);
    const float elbowRate = static_cast<float>(elbow.flips) / static_cast<float>(elbow.faces);
    INFO("ball " << ball.flips << "/" << ball.faces << " (" << ballRate << "), elbow "
                 << elbow.flips << "/" << elbow.faces << " (" << elbowRate << ")");
    CHECK(ballRate <= elbowRate);
    CHECK(ball.minAreaRatio > elbow.minAreaRatio);
}
