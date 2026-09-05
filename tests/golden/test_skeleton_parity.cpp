// SPDX-License-Identifier: AGPL-3.0-or-later
//
// .mhskel parsing, checked against the reference's own bone list.
//
// Regenerate the fixture with:
//     ./.venv-mh/bin/python tools/capture_fixture.py skeleton

#include "makehuman/core/ObjReader.h"
#include "makehuman/rig/Skeleton.h"

#include <nlohmann/json.hpp>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <map>
#include <set>
#include <string>
#include <vector>

using namespace mh;

namespace {

std::filesystem::path rigPath() {
    return std::filesystem::path(MH_DATA_DIR) / "rigs" / "default.mhskel";
}

nlohmann::json boneOrderFixture() {
    std::ifstream in(std::filesystem::path(MH_GOLDEN_DIR) / "skeleton" / "bone_order.json");
    REQUIRE(in);
    return nlohmann::json::parse(in);
}

}  // namespace

// The order is load-bearing: it is the order every exporter writes bones in,
// and the row order of the rest-matrix arrays. It is NOT a breadth-first
// traversal -- see loadSkeleton()'s comment -- so an implementation that used
// a real BFS would produce a plausible but different list and fail here.
TEST_CASE("bone order matches the reference exactly", "[skeleton][golden][parity]") {
    const auto skel = rig::loadSkeleton(rigPath());
    REQUIRE(skel.has_value());

    const auto expected = boneOrderFixture();
    REQUIRE(expected.is_array());
    REQUIRE(expected.size() == 163);
    REQUIRE(skel->boneCount() == expected.size());

    size_t nameMismatch   = 0;
    size_t parentMismatch = 0;
    for (size_t i = 0; i < expected.size(); ++i) {
        CAPTURE(i);
        const auto& want     = expected[i];
        const rig::Bone& got = skel->bones[i];

        if (want["name"].get<std::string>() != got.name) ++nameMismatch;

        const bool wantRoot = want["parent"].is_null();
        if (wantRoot) {
            if (got.parent != -1) ++parentMismatch;
        } else {
            if (got.parent < 0 || skel->bones[static_cast<size_t>(got.parent)].name !=
                                      want["parent"].get<std::string>()) {
                ++parentMismatch;
            }
        }
    }
    CHECK(nameMismatch == 0);
    CHECK(parentMismatch == 0);
}

// Ordering invariant the rest-matrix pass depends on: a bone's parent must
// already have been processed, so parent index < own index, always.
TEST_CASE("every parent precedes its child", "[skeleton][parity]") {
    const auto skel = rig::loadSkeleton(rigPath());
    REQUIRE(skel.has_value());

    size_t roots = 0;
    for (size_t i = 0; i < skel->bones.size(); ++i) {
        CAPTURE(i, skel->bones[i].name);
        const int32_t p = skel->bones[i].parent;
        if (p < 0) {
            ++roots;
        } else {
            REQUIRE(static_cast<size_t>(p) < i);
        }
    }
    CHECK(roots == 1);  // "root"
}

TEST_CASE("joints and planes are read", "[skeleton][parity]") {
    const auto skel = rig::loadSkeleton(rigPath());
    REQUIRE(skel.has_value());

    // Counts captured from the reference: 326 joints, 163 planes.
    CHECK(skel->jointVerts.size() == 326);
    CHECK(skel->planes.size() == 163);
    CHECK(skel->version == 110);  // the value in default.mhskel
    CHECK_FALSE(skel->weightsFile.empty());

    // Counts alone cannot tell a correctly parsed triple from a shuffled one,
    // so pin one plane's contents against the file (default.mhskel "planes").
    const auto plane = skel->planes.find("breast.L____plane");
    REQUIRE(plane != skel->planes.end());
    CHECK(plane->second ==
          std::array<std::string, 3>{"special01____tail", "spine01____head", "spine01____tail"});

    // Same for a joint: 326 entries says nothing about which verts each holds.
    const auto joint = skel->jointVerts.find(skel->bones.front().headJoint);
    REQUIRE(joint != skel->jointVerts.end());
    CHECK_FALSE(joint->second.empty());

    // Every bone's joints must resolve, or updateJoints cannot place it.
    size_t unresolved = 0;
    for (const auto& b : skel->bones) {
        if (!skel->jointVerts.contains(b.headJoint)) ++unresolved;
        if (!skel->jointVerts.contains(b.tailJoint)) ++unresolved;
    }
    CHECK(unresolved == 0);
}

TEST_CASE("joint positions place the rig inside the body", "[skeleton][parity]") {
    const auto mesh = core::loadObj(std::filesystem::path(MH_DATA_DIR) / "3dobjs" / "base.obj");
    REQUIRE(mesh.has_value());
    auto skel = rig::loadSkeleton(rigPath());
    REQUIRE(skel.has_value());

    REQUIRE(skel->updateJoints(mesh->coord()));

    const auto bb = mesh->boundingBox();
    REQUIRE(bb.has_value());
    const auto [lo, hi] = *bb;

    // A joint is the MEAN of a vertex cloud, so it is inside the convex hull
    // and therefore inside the bounding box. Anything outside means the wrong
    // vertices, or indices read against the wrong mesh.
    size_t outside    = 0;
    size_t degenerate = 0;
    for (const auto& b : skel->bones) {
        CAPTURE(b.name);
        for (const auto& p : {b.head, b.tail}) {
            REQUIRE(std::isfinite(p.x));
            REQUIRE(std::isfinite(p.y));
            REQUIRE(std::isfinite(p.z));
            if (p.x < lo.x || p.x > hi.x || p.y < lo.y || p.y > hi.y || p.z < lo.z || p.z > hi.z) {
                ++outside;
            }
        }
        const auto d = b.direction();
        if (d.x == 0.0F && d.y == 0.0F && d.z == 0.0F) ++degenerate;
    }
    CHECK(outside == 0);
    CHECK(degenerate == 0);  // a zero-length bone has no definable orientation
}

TEST_CASE("a joint index past the end of the mesh is refused", "[skeleton]") {
    auto skel = rig::loadSkeleton(rigPath());
    REQUIRE(skel.has_value());

    // The rig indexes a 19,158-vertex body; a smaller mesh must not be read
    // past its end. The reference indexes unguarded here.
    const std::vector<foundation::Vec3> tiny(10, foundation::Vec3{});
    CHECK_FALSE(skel->updateJoints(tiny));
}

TEST_CASE("a bone with a missing parent is an error, not a silent drop", "[skeleton]") {
    const auto p = std::filesystem::temp_directory_path() / "mh_orphan.mhskel";
    {
        std::ofstream out(p);
        out << R"({"name":"t","bones":{)"
            << R"("root":{"head":"a","tail":"b","parent":null},)"
            << R"("orphan":{"head":"a","tail":"b","parent":"nonexistent"}},)"
            << R"("joints":{"a":[0],"b":[1]}})";
    }
    const auto skel = rig::loadSkeleton(p);
    REQUIRE_FALSE(skel.has_value());
    CHECK(skel.error().kind == rig::SkeletonErrorKind::UnreachableBones);
    CHECK(skel.error().detail.find("orphan") != std::string::npos);

    std::error_code ec;
    std::filesystem::remove(p, ec);
}

// A parent cycle makes a relaxation pass add nothing. Without the reference's
// anti-deadlock guard this loops forever; with it, the cycle is reported.
TEST_CASE("a parent cycle terminates", "[skeleton]") {
    const auto p = std::filesystem::temp_directory_path() / "mh_cycle.mhskel";
    {
        std::ofstream out(p);
        out << R"({"name":"t","bones":{)"
            << R"("a":{"head":"j","tail":"k","parent":"b"},)"
            << R"("b":{"head":"j","tail":"k","parent":"a"}},)"
            << R"("joints":{"j":[0],"k":[1]}})";
    }
    const auto skel = rig::loadSkeleton(p);  // must not hang
    REQUIRE_FALSE(skel.has_value());
    CHECK(skel.error().kind == rig::SkeletonErrorKind::UnreachableBones);

    std::error_code ec;
    std::filesystem::remove(p, ec);
}

TEST_CASE("malformed JSON is reported, not crashed on", "[skeleton]") {
    const auto p = std::filesystem::temp_directory_path() / "mh_bad.mhskel";
    {
        std::ofstream out(p);
        out << R"({"name":"t","bones":{"a":)";  // truncated
    }
    const auto skel = rig::loadSkeleton(p);
    REQUIRE_FALSE(skel.has_value());
    CHECK(skel.error().kind == rig::SkeletonErrorKind::Malformed);

    std::error_code ec;
    std::filesystem::remove(p, ec);
}

namespace {

/// Reads a little-endian float32 blob captured by tools/capture_fixture.py.
std::vector<float> readFloats(const std::filesystem::path& p) {
    std::ifstream in(p, std::ios::binary | std::ios::ate);
    if (!in) return {};
    const auto bytes = static_cast<size_t>(in.tellg());
    in.seekg(0);
    std::vector<float> out(bytes / sizeof(float));
    in.read(reinterpret_cast<char*>(out.data()), static_cast<std::streamsize>(bytes));
    return out;
}

/// The rig loaded and fully built against the base mesh in rest pose.
rig::Skeleton builtRig() {
    const auto mesh = core::loadObj(std::filesystem::path(MH_DATA_DIR) / "3dobjs" / "base.obj");
    REQUIRE(mesh.has_value());
    auto skel = rig::loadSkeleton(rigPath());
    REQUIRE(skel.has_value());
    REQUIRE(skel->updateJoints(mesh->coord()));
    REQUIRE(skel->buildRestMatrices());
    return std::move(*skel);
}

}  // namespace

// The real test of the rest-matrix maths: every element of all 163 matrices,
// against what the reference itself produced.
//
// The fixture stores them as numpy wrote them -- row-major (163, 4, 4) -- and
// our Mat4 is row-major too, so element [b][r][c] maps directly. If either the
// axis-as-column convention or the row-major storage were wrong, this fails
// immediately and loudly rather than producing a subtly mis-rolled rig.
TEST_CASE("rest matrices match the reference", "[skeleton][golden][parity]") {
    const rig::Skeleton skel = builtRig();

    const auto dir       = std::filesystem::path(MH_GOLDEN_DIR) / "skeleton";
    const auto expGlobal = readFloats(dir / "rest_global.bin");
    const auto expRel    = readFloats(dir / "rest_relative.bin");

    REQUIRE(expGlobal.size() == 163 * 16);
    REQUIRE(expRel.size() == 163 * 16);
    REQUIRE(skel.boneCount() == 163);

    // Positions are decimetres accumulated through float32 means; 1e-4 is the
    // tolerance memory/test.md section 3.3 states for derived transforms.
    constexpr float kTol = 1e-4F;

    size_t globalBad  = 0;
    size_t relBad     = 0;
    float worstGlobal = 0.0F;
    float worstRel    = 0.0F;

    for (size_t b = 0; b < 163; ++b) {
        for (size_t r = 0; r < 4; ++r) {
            for (size_t c = 0; c < 4; ++c) {
                const size_t i = (b * 4 + r) * 4 + c;

                const float dg = std::abs(skel.bones[b].matRestGlobal.m[r][c] - expGlobal[i]);
                worstGlobal    = std::max(worstGlobal, dg);
                if (dg > kTol) {
                    if (globalBad == 0) {
                        INFO("first global mismatch: bone "
                             << b << " (" << skel.bones[b].name << ") [" << r << "][" << c
                             << "] got " << skel.bones[b].matRestGlobal.m[r][c] << " want "
                             << expGlobal[i]);
                        CHECK(dg <= kTol);
                    }
                    ++globalBad;
                }

                const float dr = std::abs(skel.bones[b].matRestRelative.m[r][c] - expRel[i]);
                worstRel       = std::max(worstRel, dr);
                if (dr > kTol) {
                    if (relBad == 0) {
                        INFO("first relative mismatch: bone " << b << " (" << skel.bones[b].name
                                                              << ") [" << r << "][" << c << "]");
                        CHECK(dr <= kTol);
                    }
                    ++relBad;
                }
            }
        }
    }
    INFO("worst global delta " << worstGlobal << ", worst relative delta " << worstRel);
    CHECK(globalBad == 0);
    CHECK(relBad == 0);
}

// Property the parity test cannot express: the basis must be orthonormal for
// every bone, which is what makes rigidInverse exact.
TEST_CASE("every rest basis is orthonormal", "[skeleton][parity]") {
    const rig::Skeleton skel = builtRig();

    for (const auto& b : skel.bones) {
        CAPTURE(b.name);
        const auto x = b.matRestGlobal.axis(0);
        const auto y = b.matRestGlobal.axis(1);
        const auto z = b.matRestGlobal.axis(2);

        CHECK(std::abs(foundation::dot(x, x) - 1.0F) < 1e-4F);
        CHECK(std::abs(foundation::dot(y, y) - 1.0F) < 1e-4F);
        CHECK(std::abs(foundation::dot(z, z) - 1.0F) < 1e-4F);
        CHECK(std::abs(foundation::dot(x, y)) < 1e-4F);
        CHECK(std::abs(foundation::dot(y, z)) < 1e-4F);
        CHECK(std::abs(foundation::dot(x, z)) < 1e-4F);

        // Y is the bone's own direction, by construction.
        foundation::Vec3 d = b.direction();
        const float n      = std::sqrt(foundation::dot(d, d));
        REQUIRE(n > 0.0F);
        d = d * (1.0F / n);
        CHECK(std::abs(foundation::dot(y, d) - 1.0F) < 1e-4F);
    }
}

// matRestRelative composes back to matRestGlobal through the parent chain.
// This is the invariant every skinning path depends on.
TEST_CASE("relative matrices compose back to global", "[skeleton][parity]") {
    const rig::Skeleton skel = builtRig();

    float worst = 0.0F;
    for (size_t i = 0; i < skel.bones.size(); ++i) {
        const auto& b = skel.bones[i];
        CAPTURE(b.name);
        const foundation::Mat4 composed =
            (b.parent < 0)
                ? b.matRestRelative
                : skel.bones[static_cast<size_t>(b.parent)].matRestGlobal * b.matRestRelative;

        for (size_t r = 0; r < 4; ++r) {
            for (size_t c = 0; c < 4; ++c)
                worst = std::max(worst, std::abs(composed.m[r][c] - b.matRestGlobal.m[r][c]));
        }
    }
    INFO("worst composition error " << worst);
    CHECK(worst < 1e-4F);
}

// The Mixamo superset must load through the SAME loader as the default rig --
// if it needs special handling it is not a drop-in replacement, and the whole
// point is that a rigged export can use it without the engine caring.
TEST_CASE("the Mixamo superset skeleton loads", "[skeleton][mixamo]") {
    const auto path = std::filesystem::path(MH_DATA_DIR) / "rigs" / "mixamo_superset.mhskel";
    if (!std::filesystem::exists(path)) SKIP("superset not generated");

    const auto superset = rig::loadSkeleton(path);
    if (!superset) {
        UNSCOPED_INFO("load failed: " << superset.error().message());
    }
    REQUIRE(superset.has_value());

    // 163 MakeHuman bones + the 16 Mixamo needs and MakeHuman lacks.
    CHECK(superset->boneCount() == 179);

    // NOT "every parent precedes its child" -- loadSkeleton reorders
    // breadth-first, so that assertion can never fail and proves nothing.
    // What is worth checking is that the reparenting actually happened: the
    // superset only earns its name if `hips` really sits between `root` and the
    // legs, and `ball` really sits between the foot and the toes.
    std::map<std::string, std::string> parentOf;
    for (const auto& b : superset->bones) {
        parentOf[b.name] = b.parent >= 0 ? superset->bones[static_cast<size_t>(b.parent)].name
                                         : std::string{"<root>"};
    }
    CHECK(parentOf.at("hips") == "root");
    CHECK(parentOf.at("pelvis.L") == "hips");
    CHECK(parentOf.at("pelvis.R") == "hips");
    CHECK(parentOf.at("spine05") == "hips");
    for (const char* side : {".L", ".R"}) {
        INFO(side);
        CHECK(parentOf.at(std::string("ball") + side) == std::string("foot") + side);
        // All five toes must hang off the ball, not the foot -- that is what
        // makes a foot roll expressible at all.
        for (int toe = 1; toe <= 5; ++toe) {
            CHECK(parentOf.at("toe" + std::to_string(toe) + "-1" + side) ==
                  std::string("ball") + side);
        }
    }

    // The default rig must still load unchanged beside it -- the superset is an
    // addition, not a migration.
    const auto base =
        rig::loadSkeleton(std::filesystem::path(MH_DATA_DIR) / "rigs" / "default.mhskel");
    REQUIRE(base.has_value());
    CHECK(base->boneCount() == 163);
}

// The bones Mixamo drives must all be present under the names the mapping
// promises. If one is missing, a retargeted clip silently loses that joint.
TEST_CASE("every mapped Mixamo target exists in the superset", "[skeleton][mixamo]") {
    const auto path = std::filesystem::path(MH_DATA_DIR) / "rigs" / "mixamo_superset.mhskel";
    if (!std::filesystem::exists(path)) SKIP("superset not generated");
    const auto superset = rig::loadSkeleton(path);
    REQUIRE(superset.has_value());

    std::set<std::string> present;
    for (const auto& b : superset->bones)
        present.insert(b.name);

    // The three additions that are real, weight-bearing bones rather than tip
    // markers -- the hip pivot and the two ball-of-foot joints.
    for (const char* name : {"hips", "ball.L", "ball.R"}) {
        INFO(name);
        CHECK(present.count(name) == 1);
    }
    // A fingertip per finger per hand, so Mixamo's 4th joint has a home.
    for (const char* side : {".L", ".R"}) {
        for (int finger = 1; finger <= 5; ++finger) {
            const std::string tip = "finger" + std::to_string(finger) + "-4" + side;
            INFO(tip);
            CHECK(present.count(tip) == 1);
        }
    }
}

// --- The Mixamo superset rig must actually be usable ------------------------
//
// The superset rig had a CI staleness gate (is the file current with its
// generator?) but nothing ever checked it could SKIN. It could not: 13 of its
// 179 bones are tip markers whose head and tail resolve to the same joint, and
// one zero-length bone made buildRestMatrices reject the entire skeleton. The
// rig shipped, was gated, and was unusable.
//
// The 13 are legitimate Mixamo counterparts, not junk to delete: Mixamo's own
// 65 bones include HeadTop_End, Left/RightToe_End, and a 4th segment on every
// finger (LeftHandIndex4 and friends). They sit AT the tip of their parent and
// deform nothing -- 0 weighted vertices on all 13, verified.
TEST_CASE("the Mixamo superset rig builds rest matrices", "[rig][mixamo][superset]") {
    const auto rig = std::filesystem::path(MH_DATA_DIR) / "rigs" / "mixamo_superset.mhskel";
    if (!std::filesystem::exists(rig)) return;

    auto mesh = mh::core::loadObj(std::filesystem::path(MH_DATA_DIR) / "3dobjs" / "base.obj");
    REQUIRE(mesh.has_value());

    auto skel = mh::rig::loadSkeleton(rig);
    REQUIRE(skel.has_value());
    REQUIRE(skel->boneCount() == 179);
    REQUIRE(skel->updateJoints(mesh->coord()));

    // This is the assertion that was missing.
    REQUIRE(skel->buildRestMatrices());

    // Every bone must end up with an orthonormal basis -- a tip marker that
    // silently produced a zero or NaN matrix would pass the line above and
    // corrupt every child.
    size_t tips = 0;
    for (const auto& b : skel->bones) {
        INFO("bone " << b.name);
        const auto& g = b.matRestGlobal;
        for (size_t axis = 0; axis < 3; ++axis) {
            const auto a   = g.axis(axis);
            const float sq = a.x * a.x + a.y * a.y + a.z * a.z;
            CHECK(std::isfinite(sq));
            CHECK(std::abs(sq - 1.0F) < 1e-4F);
        }
        if (b.length < 1e-9F) ++tips;
    }
    CHECK(tips == 13);
}

// A tip marker has no direction of its own, so it takes its parent's basis --
// the same convention Blender applies to leaf bones. Pinning it explicitly
// because "it built" would also be true of a wrong-but-orthonormal answer.
TEST_CASE("a tip marker inherits its parent's rest basis", "[rig][mixamo][superset]") {
    const auto rig = std::filesystem::path(MH_DATA_DIR) / "rigs" / "mixamo_superset.mhskel";
    if (!std::filesystem::exists(rig)) return;

    auto mesh = mh::core::loadObj(std::filesystem::path(MH_DATA_DIR) / "3dobjs" / "base.obj");
    auto skel = mh::rig::loadSkeleton(rig);
    REQUIRE(skel.has_value());
    REQUIRE(skel->updateJoints(mesh->coord()));
    REQUIRE(skel->buildRestMatrices());

    size_t checked = 0;
    for (const auto& b : skel->bones) {
        if (b.length >= 1e-9F || b.parent < 0) continue;
        const auto& p = skel->bones[static_cast<size_t>(b.parent)];
        INFO(b.name << " under " << p.name);
        for (size_t axis = 0; axis < 3; ++axis) {
            const auto tip = b.matRestGlobal.axis(axis);
            const auto par = p.matRestGlobal.axis(axis);
            CHECK(std::abs(tip.x - par.x) < 1e-5F);
            CHECK(std::abs(tip.y - par.y) < 1e-5F);
            CHECK(std::abs(tip.z - par.z) < 1e-5F);
        }
        // Its position is still its own: the tip, not the parent's head.
        CHECK(std::abs(b.matRestGlobal.at(0, 3) - b.head.x) < 1e-6F);
        CHECK(std::abs(b.matRestGlobal.at(1, 3) - b.head.y) < 1e-6F);
        CHECK(std::abs(b.matRestGlobal.at(2, 3) - b.head.z) < 1e-6F);
        ++checked;
    }
    CHECK(checked == 13);
}

// Is fitting the skeleton twice to the SAME mesh idempotent?
//
// This is not academic. `poseInPlace` (main.cpp) re-fits before skinning, and
// the rig has already been fitted once at load. If the second fit moves
// anything, every posed export carries a different skeleton from an unposed one
// for a reason that has nothing to do with the pose.
TEST_CASE("fitting the rig twice to one mesh changes nothing", "[rig][skeleton]") {
    const auto rigPath = std::filesystem::path(MH_DATA_DIR) / "rigs" / "default.mhskel";
    if (!std::filesystem::exists(rigPath)) return;

    const auto mesh = core::loadObj(std::filesystem::path(MH_DATA_DIR) / "3dobjs" / "base.obj");
    REQUIRE(mesh.has_value());
    auto skel = rig::loadSkeleton(rigPath);
    REQUIRE(skel.has_value());

    REQUIRE(skel->updateJoints(mesh->coord()));
    REQUIRE(skel->buildRestMatrices());
    std::vector<foundation::Mat4> first;
    for (const auto& b : skel->bones)
        first.push_back(b.matRestGlobal);

    // Exactly what poseInPlace does before it skins.
    REQUIRE(skel->updateJoints(mesh->coord()));
    REQUIRE(skel->buildRestMatrices());

    double worst = 0.0;
    size_t bone  = 0;
    for (size_t i = 0; i < skel->bones.size(); ++i) {
        for (size_t r = 0; r < 4; ++r) {
            for (size_t c = 0; c < 4; ++c) {
                const double d =
                    std::abs(static_cast<double>(skel->bones[i].matRestGlobal.m[r][c]) -
                             static_cast<double>(first[i].m[r][c]));
                if (d > worst) {
                    worst = d;
                    bone  = i;
                }
            }
        }
    }
    INFO("largest change " << worst << " on bone " << skel->bones[bone].name);
    CHECK(worst < 1e-6);
}
