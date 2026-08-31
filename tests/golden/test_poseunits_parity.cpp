// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Face pose units and the weighted blend, against the reference.
//
// Regenerate with:
//     ./.venv-mh/bin/python tools/capture_fixture.py poseunits

#include "makehuman/core/ObjReader.h"
#include "makehuman/io/BvhReader.h"
#include "makehuman/rig/PoseUnits.h"
#include "makehuman/rig/Skeleton.h"

#include <nlohmann/json.hpp>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

using namespace mh;

namespace {

constexpr size_t kUnits = 60;
constexpr size_t kBones = 163;

std::filesystem::path fixtureDir() {
    return std::filesystem::path(MH_GOLDEN_DIR) / "poseunits";
}

std::vector<float> readFloats(const std::filesystem::path& p) {
    std::ifstream in(p, std::ios::binary | std::ios::ate);
    if (!in) return {};
    const auto bytes = static_cast<size_t>(in.tellg());
    in.seekg(0);
    std::vector<float> out(bytes / sizeof(float));
    in.read(reinterpret_cast<char*>(out.data()), static_cast<std::streamsize>(bytes));
    return out;
}

nlohmann::json cases() {
    std::ifstream in(fixtureDir() / "cases.json");
    REQUIRE(in);
    return nlohmann::json::parse(in);
}

rig::PoseUnits build() {
    const auto mesh = core::loadObj(std::filesystem::path(MH_DATA_DIR) / "3dobjs" / "base.obj");
    REQUIRE(mesh.has_value());
    auto skel = rig::loadSkeleton(std::filesystem::path(MH_DATA_DIR) / "rigs" / "default.mhskel");
    REQUIRE(skel.has_value());
    REQUIRE(skel->updateJoints(mesh->coord()));
    REQUIRE(skel->buildRestMatrices());

    // The reference loads this BVH with translation disabled.
    io::BvhReadOptions opt;
    opt.translation = io::TranslationPolicy::None;
    auto bvh =
        io::readBvh(std::filesystem::path(MH_DATA_DIR) / "poseunits" / "face-poseunits.bvh", opt);
    REQUIRE(bvh.has_value());

    auto names = rig::loadPoseUnitNames(std::filesystem::path(MH_DATA_DIR) / "poseunits" /
                                        "face-poseunits.json");
    REQUIRE(names.has_value());

    auto units = rig::makePoseUnits(*bvh, *skel, std::move(*names));
    REQUIRE(units.has_value());
    return std::move(*units);
}

constexpr float kTol = 1e-5F;

}  // namespace

TEST_CASE("the 60 pose unit names load in order", "[poseunits][golden][parity]") {
    const auto spec  = cases();
    const auto units = build();

    REQUIRE(units.unitCount() == kUnits);
    REQUIRE(units.boneCount == kBones);

    const auto want = spec["names"];
    REQUIRE(want.size() == kUnits);
    for (size_t i = 0; i < kUnits; ++i) {
        CAPTURE(i);
        CHECK(units.names[i] == want[i].get<std::string>());
    }
    CHECK(units.indexOf("Rest") == 0);
    CHECK(units.indexOf("NoseWrinkler").has_value());
    CHECK_FALSE(units.indexOf("NotAPoseUnit").has_value());
}

// The BVH has 212 joints, the rig 163, and they are not the same set. Mapping
// by walking the BVH instead of the rig would silently reorder every bone, so
// this compares all 9,780 unit-bone transforms.
TEST_CASE("unit transforms match the reference", "[poseunits][golden][parity]") {
    const auto units = build();
    const auto want  = readFloats(fixtureDir() / "unit_data.bin");
    REQUIRE(want.size() == kUnits * kBones * 12);

    float worst     = 0.0F;
    size_t compared = 0;
    for (size_t u = 0; u < kUnits; ++u) {
        const auto frame = units.unit(u);
        for (size_t b = 0; b < kBones; ++b) {
            const float* w = &want[(u * kBones + b) * 12];
            for (size_t r = 0; r < 3; ++r) {
                for (size_t c = 0; c < 4; ++c)
                    worst = std::max(worst, std::abs(frame[b].m[r][c] - w[r * 4 + c]));
            }
            ++compared;
        }
    }
    INFO("worst unit delta " << worst << " over " << compared << " unit-bones");
    CHECK(worst < kTol);
    CHECK(compared == kUnits * kBones);
}

TEST_CASE("a weighted blend matches the reference", "[poseunits][golden][parity]") {
    const auto spec  = cases();
    const auto units = build();

    std::vector<size_t> idx;
    std::vector<float> weights;
    for (const auto& n : spec["blend_names"]) {
        const auto i = units.indexOf(n.get<std::string>());
        REQUIRE(i.has_value());
        idx.push_back(*i);
    }
    for (const auto& w : spec["blend_weights"])
        weights.push_back(w.get<float>());
    REQUIRE(idx.size() == weights.size());
    REQUIRE(idx.size() == 5);

    const auto got  = units.blend(idx, weights);
    const auto want = readFloats(fixtureDir() / "blended.bin");
    REQUIRE(got.size() == kBones);
    REQUIRE(want.size() == kBones * 12);

    float worst = 0.0F;
    for (size_t b = 0; b < kBones; ++b) {
        const float* w = &want[b * 12];
        for (size_t r = 0; r < 3; ++r) {
            for (size_t c = 0; c < 4; ++c)
                worst = std::max(worst, std::abs(got[b].m[r][c] - w[r * 4 + c]));
        }
    }
    INFO("worst blend delta " << worst);
    CHECK(worst < kTol);
}

// The blend composes by quaternion multiplication, which does not commute, so
// the input order changes the result. This is replicated deliberately -- the
// reference behaves this way and expression files are authored against it.
//
// The test asserts BOTH halves: that reversing matches the reference's own
// reversed result, and that the two differ measurably. Without the second, a
// symmetrising implementation would pass the first by accident.
TEST_CASE("blend order changes the result, and matches the reference both ways",
          "[poseunits][golden][parity]") {
    const auto spec  = cases();
    const auto units = build();

    std::vector<size_t> idx;
    std::vector<float> weights;
    for (const auto& n : spec["blend_names"]) {
        idx.push_back(*units.indexOf(n.get<std::string>()));
    }
    for (const auto& w : spec["blend_weights"])
        weights.push_back(w.get<float>());

    std::vector<size_t> revIdx(idx.rbegin(), idx.rend());
    std::vector<float> revWeights(weights.rbegin(), weights.rend());

    const auto forward  = units.blend(idx, weights);
    const auto backward = units.blend(revIdx, revWeights);
    const auto wantRev  = readFloats(fixtureDir() / "blended_reversed.bin");
    REQUIRE(wantRev.size() == kBones * 12);

    float worstRev   = 0.0F;
    float difference = 0.0F;
    for (size_t b = 0; b < kBones; ++b) {
        const float* w = &wantRev[b * 12];
        for (size_t r = 0; r < 3; ++r) {
            for (size_t c = 0; c < 4; ++c) {
                worstRev = std::max(worstRev, std::abs(backward[b].m[r][c] - w[r * 4 + c]));
                difference =
                    std::max(difference, std::abs(forward[b].m[r][c] - backward[b].m[r][c]));
            }
        }
    }
    INFO("worst reversed delta " << worstRev << ", forward-vs-backward " << difference);
    CHECK(worstRev < kTol);
    // The reference measured 0.0342 between the two orders.
    CHECK(difference > 0.03F);
}

// Weights are NOT normalised: the blend is additive.
TEST_CASE("a single unit at weight zero is the rest pose", "[poseunits][parity]") {
    const auto units = build();
    const auto idx   = units.indexOf("NoseWrinkler");
    REQUIRE(idx.has_value());

    const std::vector<size_t> one{*idx};
    const std::vector<float> zero{0.0F};
    const auto out = units.blend(one, zero);
    REQUIRE(out.size() == kBones);

    float worst = 0.0F;
    for (const auto& m : out) {
        for (size_t r = 0; r < 3; ++r) {
            for (size_t c = 0; c < 4; ++c) {
                const float want = (r == c) ? 1.0F : 0.0F;
                worst            = std::max(worst, std::abs(m.m[r][c] - want));
            }
        }
    }
    CHECK(worst < 1e-5F);
}

TEST_CASE("mismatched or out-of-range blend inputs are refused", "[poseunits]") {
    const auto units = build();
    const std::vector<size_t> idx{0, 1};
    const std::vector<float> oneWeight{1.0F};
    CHECK(units.blend(idx, oneWeight).empty());

    const std::vector<size_t> bad{999};
    const std::vector<float> w{1.0F};
    CHECK(units.blend(bad, w).empty());

    CHECK(units.blend({}, {}).empty());
}

TEST_CASE("a name count that disagrees with the frame count is refused", "[poseunits]") {
    const auto mesh = core::loadObj(std::filesystem::path(MH_DATA_DIR) / "3dobjs" / "base.obj");
    REQUIRE(mesh.has_value());
    auto skel = rig::loadSkeleton(std::filesystem::path(MH_DATA_DIR) / "rigs" / "default.mhskel");
    REQUIRE(skel.has_value());
    REQUIRE(skel->updateJoints(mesh->coord()));
    REQUIRE(skel->buildRestMatrices());

    auto bvh = io::readBvh(std::filesystem::path(MH_DATA_DIR) / "poseunits" / "face-poseunits.bvh");
    REQUIRE(bvh.has_value());

    const auto r = rig::makePoseUnits(*bvh, *skel, {"OnlyOneName"});
    REQUIRE_FALSE(r.has_value());
    CHECK(r.error().kind == rig::PoseUnitsErrorKind::FrameCountMismatch);
}

using mh::foundation::Mat4;

// --- mixPoses: layering a face pose over a body pose ------------------------
//
// The reference (`shared/animation.py:449-467`) copies pose1 and replaces the
// listed bone indices with pose2's:
//
//     data = pose1.getAtFramePos(0).copy()
//     data[bonesList] = pose2.getAtFramePos(0)[[bonesList]]
//
// **No parity fixture, deliberately.** This is index replacement with no
// numerical content -- there is nothing for float32 to round differently, and a
// captured .bin would only re-assert that a copy copies. The properties below
// pin the behaviour that can actually break: which bones move, which do not,
// and that mismatched inputs are refused rather than silently truncated.
//
// The reference's `[[bonesList]]` double bracket is a numpy indexing quirk that
// happens to broadcast; it is not replicated as such.
TEST_CASE("mixPoses replaces exactly the listed bones", "[rig][poseunits][mix]") {
    std::vector<Mat4> base(5, Mat4::identity());
    std::vector<Mat4> overlay(5, Mat4::identity());
    for (size_t i = 0; i < 5; ++i) {
        base[i].m[0][3]    = static_cast<float>(i) + 1.0F;     // 1..5
        overlay[i].m[0][3] = -(static_cast<float>(i) + 1.0F);  // -1..-5
    }

    const std::array<size_t, 2> pick{1, 3};
    const auto mixed = rig::mixPoses(base, overlay, pick);
    REQUIRE(mixed.has_value());
    REQUIRE(mixed->size() == 5);
    CHECK((*mixed)[0].m[0][3] == 1.0F);   // untouched
    CHECK((*mixed)[1].m[0][3] == -2.0F);  // replaced
    CHECK((*mixed)[2].m[0][3] == 3.0F);   // untouched
    CHECK((*mixed)[3].m[0][3] == -4.0F);  // replaced
    CHECK((*mixed)[4].m[0][3] == 5.0F);   // untouched
}

TEST_CASE("mixPoses with no bones is the base pose", "[rig][poseunits][mix]") {
    std::vector<Mat4> base(3, Mat4::identity());
    base[1].m[1][3] = 7.0F;
    std::vector<Mat4> overlay(3, Mat4::identity());
    overlay[1].m[1][3] = -7.0F;

    const auto mixed = rig::mixPoses(base, overlay, {});
    REQUIRE(mixed.has_value());
    CHECK((*mixed)[1].m[1][3] == 7.0F);
}

TEST_CASE("mixPoses refuses poses for different skeletons", "[rig][poseunits][mix]") {
    // The reference raises here. Silently mixing what it can would produce a
    // plausible pose built from two different rigs.
    const std::vector<Mat4> base(5, Mat4::identity());
    const std::vector<Mat4> overlay(4, Mat4::identity());
    const std::array<size_t, 1> pick{0};
    const auto mixed = rig::mixPoses(base, overlay, pick);
    REQUIRE_FALSE(mixed.has_value());
    CHECK(mixed.error().kind == rig::PoseUnitsErrorKind::FrameCountMismatch);
}

TEST_CASE("mixPoses refuses a bone index it cannot address", "[rig][poseunits][mix]") {
    const std::vector<Mat4> base(3, Mat4::identity());
    const std::vector<Mat4> overlay(3, Mat4::identity());
    const std::array<size_t, 2> pick{1, 9};
    const auto mixed = rig::mixPoses(base, overlay, pick);
    REQUIRE_FALSE(mixed.has_value());
    CHECK(mixed.error().kind == rig::PoseUnitsErrorKind::Malformed);
}

// The case it exists for: a face expression layered onto a whole-body pose.
TEST_CASE("a face expression layers onto a body pose", "[rig][poseunits][mix][golden]") {
    const auto rigPath = std::filesystem::path(MH_DATA_DIR) / "rigs" / "default.mhskel";
    const auto bvhPath = std::filesystem::path(MH_DATA_DIR) / "poseunits" / "face-poseunits.bvh";
    if (!std::filesystem::exists(rigPath) || !std::filesystem::exists(bvhPath)) return;

    auto mesh = mh::core::loadObj(std::filesystem::path(MH_DATA_DIR) / "3dobjs" / "base.obj");
    REQUIRE(mesh.has_value());
    auto skel = rig::loadSkeleton(rigPath);
    REQUIRE(skel.has_value());
    REQUIRE(skel->updateJoints(mesh->coord()));
    REQUIRE(skel->buildRestMatrices());

    auto bvh = io::readBvh(bvhPath);
    REQUIRE(bvh.has_value());
    auto names = rig::loadPoseUnitNames(std::filesystem::path(MH_DATA_DIR) / "poseunits" /
                                        "face-poseunits.json");
    REQUIRE(names.has_value());
    auto units = rig::makePoseUnits(*bvh, *skel, std::move(*names));
    REQUIRE(units.has_value());

    const auto body =
        rig::loadBodyPose(std::filesystem::path(MH_DATA_DIR) / "poses" / "tpose.bvh", *skel);
    REQUIRE(body.has_value());

    // Every bone the expression actually moves.
    const auto face = units->unit(0);
    std::vector<size_t> faceBones;
    for (size_t i = 0; i < face.size(); ++i) {
        bool identity = true;
        for (size_t r = 0; r < 4 && identity; ++r)
            for (size_t c = 0; c < 4; ++c)
                if (std::abs(face[i].m[r][c] - Mat4::identity().m[r][c]) > 1e-6F) {
                    identity = false;
                    break;
                }
        if (!identity) faceBones.push_back(i);
    }
    REQUIRE_FALSE(faceBones.empty());

    const auto mixed = rig::mixPoses(*body, face, faceBones);
    REQUIRE(mixed.has_value());
    REQUIRE(mixed->size() == body->size());

    // The face bones took the expression; every other bone kept the T-pose.
    for (size_t i = 0; i < mixed->size(); ++i) {
        const bool isFace = std::ranges::find(faceBones, i) != faceBones.end();
        const auto& want  = isFace ? face[i] : (*body)[i];
        INFO("bone " << i << (isFace ? " (face)" : " (body)"));
        for (size_t r = 0; r < 4; ++r)
            for (size_t c = 0; c < 4; ++c)
                CHECK((*mixed)[i].m[r][c] == want.m[r][c]);
    }
}
