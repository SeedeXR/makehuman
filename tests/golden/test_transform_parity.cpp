// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Euler and quaternion conversions, over all 24 axis conventions, against
// legacy/python/core/transformations.py.
//
// Regenerate with:
//     ./.venv-mh/bin/python tools/capture_fixture.py transform

#include "makehuman/foundation/Transform.h"

#include <nlohmann/json.hpp>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

using namespace mh::foundation;

namespace {

std::filesystem::path fixtureDir() {
    return std::filesystem::path(MH_GOLDEN_DIR) / "transform";
}

std::vector<double> readDoubles(const std::filesystem::path& p) {
    std::ifstream in(p, std::ios::binary | std::ios::ate);
    if (!in) return {};
    const auto bytes = static_cast<size_t>(in.tellg());
    in.seekg(0);
    std::vector<double> out(bytes / sizeof(double));
    in.read(reinterpret_cast<char*>(out.data()), static_cast<std::streamsize>(bytes));
    return out;
}

nlohmann::json cases() {
    std::ifstream in(fixtureDir() / "cases.json");
    REQUIRE(in);
    return nlohmann::json::parse(in);
}

/// Matrices are built in double and stored in float, and compared against a
/// float64 fixture, so the floor is float32 epsilon rather than double.
constexpr double kMatTol = 2e-6;
constexpr double kAngTol = 1e-5;

double maxMatDiff(const Mat4& got, const double* want) {
    double worst = 0.0;
    for (size_t r = 0; r < 4; ++r) {
        for (size_t c = 0; c < 4; ++c) {
            worst = std::max(worst, std::abs(static_cast<double>(got.m[r][c]) - want[r * 4 + c]));
        }
    }
    return worst;
}

}  // namespace

// All 24 conventions must be present and round-trip through their own names.
// The 24 differ only by three flags, so a typo in the table silently maps one
// convention onto another's behaviour.
TEST_CASE("all 24 Euler conventions are known", "[transform][parity]") {
    const auto spec  = cases();
    const auto names = spec["conventions"];
    REQUIRE(names.size() == 24);

    for (const auto& n : names) {
        const auto s = n.get<std::string>();
        CAPTURE(s);
        const auto order = eulerOrderFromString(s);
        REQUIRE(order.has_value());
        CHECK(eulerOrderName(*order) == s);
    }

    // Every one distinct: 24 names, 24 distinct encodings.
    const auto all = eulerOrderNames();
    std::vector<std::string> sorted;
    for (const auto& n : all)
        sorted.emplace_back(n);
    std::ranges::sort(sorted);
    CHECK(std::ranges::adjacent_find(sorted) == sorted.end());
}

TEST_CASE("euler matrices match the reference for every convention",
          "[transform][golden][parity]") {
    const auto spec   = cases();
    const auto names  = spec["conventions"];
    const auto angles = spec["angle_sets"];
    const auto want   = readDoubles(fixtureDir() / "euler_matrices.bin");

    const size_t n = names.size() * angles.size();
    REQUIRE(want.size() == n * 16);

    size_t idx   = 0;
    double worst = 0.0;
    size_t bad   = 0;
    for (const auto& nm : names) {
        const auto order = eulerOrderFromString(nm.get<std::string>());
        REQUIRE(order.has_value());
        for (const auto& a : angles) {
            const Mat4 got =
                eulerMatrix(a[0].get<double>(), a[1].get<double>(), a[2].get<double>(), *order);
            const double d = maxMatDiff(got, &want[idx * 16]);
            worst          = std::max(worst, d);
            if (d > kMatTol) {
                if (bad == 0) {
                    INFO("first mismatch: " << nm.get<std::string>() << " angles " << a);
                    CHECK(d <= kMatTol);
                }
                ++bad;
            }
            ++idx;
        }
    }
    INFO("worst euler-matrix delta " << worst << " over " << idx << " cases");
    CHECK(idx == n);
    CHECK(bad == 0);
}

// At gimbal lock the decomposition is not unique, so the recovered angles need
// not equal the originals. What must hold is that they rebuild the same matrix
// -- which is the property anyone actually depends on.
TEST_CASE("euler angles recovered from a matrix rebuild it", "[transform][golden][parity]") {
    const auto spec       = cases();
    const auto names      = spec["conventions"];
    const auto angles     = spec["angle_sets"];
    const auto wantAngles = readDoubles(fixtureDir() / "euler_back.bin");

    size_t idx          = 0;
    double worstRebuild = 0.0;
    double worstAngle   = 0.0;
    size_t lockCases    = 0;
    for (const auto& nm : names) {
        const auto order = eulerOrderFromString(nm.get<std::string>());
        REQUIRE(order.has_value());
        for (const auto& a : angles) {
            CAPTURE(nm.get<std::string>());
            const Mat4 m =
                eulerMatrix(a[0].get<double>(), a[1].get<double>(), a[2].get<double>(), *order);
            const auto got = eulerFromMatrix(m, *order);

            // Against the reference's own recovered angles.
            double angleDiff = 0.0;
            for (size_t c = 0; c < 3; ++c)
                angleDiff = std::max(angleDiff, std::abs(got[c] - wantAngles[idx * 3 + c]));
            worstAngle = std::max(worstAngle, angleDiff);

            // And the property: rebuilding must give the same matrix.
            const Mat4 rebuilt = eulerMatrix(got[0], got[1], got[2], *order);
            double rebuildDiff = 0.0;
            for (size_t r = 0; r < 4; ++r) {
                for (size_t c = 0; c < 4; ++c) {
                    rebuildDiff =
                        std::max(rebuildDiff, std::abs(static_cast<double>(rebuilt.m[r][c]) -
                                                       static_cast<double>(m.m[r][c])));
                }
            }
            worstRebuild = std::max(worstRebuild, rebuildDiff);
            CHECK(rebuildDiff <= kMatTol);

            // The singular branch pins the third angle to exactly zero. Count
            // the reference's own recovered angles that show that signature,
            // so the branch is proven reached rather than assumed.
            if (wantAngles[idx * 3 + 2] == 0.0) ++lockCases;
            ++idx;
        }
    }
    INFO("worst rebuild delta " << worstRebuild << ", worst angle delta " << worstAngle
                                << ", singular cases " << lockCases);
    CHECK(worstRebuild <= kMatTol);
    // Our recovered angles match the reference's everywhere, including inside
    // the singular branch, because the same tie-break is replicated.
    CHECK(worstAngle <= kAngTol);

    // And that branch really is reached. The fixture's last two angle sets use
    // math.pi/2 EXACTLY: with a truncated 1.5707963 the guard value is ~1e-8
    // against an _EPS of 8.9e-16, so the singular path is never taken and this
    // test would silently cover nothing. Measured: 24 of 120.
    CHECK(lockCases == 24);
}

// q and -q are the same rotation, and the trace method disagrees with the
// reference's eigenvector method on sign for 18 of the 120 cases. Compare up to
// sign, then pin the rotation by rebuilding the matrix.
TEST_CASE("quaternions match the reference up to sign, and rebuild the matrix",
          "[transform][golden][parity]") {
    const auto spec   = cases();
    const auto names  = spec["conventions"];
    const auto angles = spec["angle_sets"];
    const auto wantQ  = readDoubles(fixtureDir() / "quaternions.bin");
    const auto wantM  = readDoubles(fixtureDir() / "euler_matrices.bin");

    size_t idx          = 0;
    size_t flipped      = 0;
    double worstQ       = 0.0;
    double worstRebuild = 0.0;
    for (const auto& nm : names) {
        const auto order = eulerOrderFromString(nm.get<std::string>());
        REQUIRE(order.has_value());
        for (const auto& a : angles) {
            CAPTURE(nm.get<std::string>());
            const Mat4 m =
                eulerMatrix(a[0].get<double>(), a[1].get<double>(), a[2].get<double>(), *order);
            const Quat q = quaternionFromMatrix(m);

            const double* w   = &wantQ[idx * 4];
            const double same = std::max({std::abs(q.w - w[0]), std::abs(q.x - w[1]),
                                          std::abs(q.y - w[2]), std::abs(q.z - w[3])});
            const double opp  = std::max({std::abs(q.w + w[0]), std::abs(q.x + w[1]),
                                          std::abs(q.y + w[2]), std::abs(q.z + w[3])});
            if (opp < same) ++flipped;
            worstQ = std::max(worstQ, std::min(same, opp));
            CHECK(std::min(same, opp) <= kMatTol);

            // The rotation itself must be exact, whichever sign was chosen.
            const double d = maxMatDiff(quaternionMatrix(q), &wantM[idx * 16]);
            worstRebuild   = std::max(worstRebuild, d);
            CHECK(d <= kMatTol);
            ++idx;
        }
    }
    INFO("worst |q| delta " << worstQ << ", worst rebuild " << worstRebuild << ", sign flips "
                            << flipped);
    CHECK(idx == 120);
}

TEST_CASE("quaternion slerp matches the reference", "[transform][golden][parity]") {
    const auto spec = cases();
    const auto f    = spec["slerp_fractions"];
    const auto q0j  = spec["slerp_q0"];
    const auto q1j  = spec["slerp_q1"];
    const auto want = readDoubles(fixtureDir() / "slerps.bin");
    REQUIRE(want.size() == f.size() * 4);

    const Quat q0{q0j[0].get<double>(), q0j[1].get<double>(), q0j[2].get<double>(),
                  q0j[3].get<double>()};
    const Quat q1{q1j[0].get<double>(), q1j[1].get<double>(), q1j[2].get<double>(),
                  q1j[3].get<double>()};

    double worst = 0.0;
    for (size_t i = 0; i < f.size(); ++i) {
        CAPTURE(f[i].get<double>());
        const Quat g    = quaternionSlerp(q0, q1, f[i].get<double>());
        const double* w = &want[i * 4];
        const double d = std::max({std::abs(g.w - w[0]), std::abs(g.x - w[1]), std::abs(g.y - w[2]),
                                   std::abs(g.z - w[3])});
        worst          = std::max(worst, d);
        CHECK(d <= 1e-9);  // pure double maths on both sides
    }
    INFO("worst slerp delta " << worst);
}

TEST_CASE("quaternion multiply matches the reference", "[transform][golden][parity]") {
    const auto spec = cases();
    const auto q0j  = spec["slerp_q0"];
    const auto q1j  = spec["slerp_q1"];
    const auto want = readDoubles(fixtureDir() / "product.bin");
    REQUIRE(want.size() == 4);

    const Quat q0{q0j[0].get<double>(), q0j[1].get<double>(), q0j[2].get<double>(),
                  q0j[3].get<double>()};
    const Quat q1{q1j[0].get<double>(), q1j[1].get<double>(), q1j[2].get<double>(),
                  q1j[3].get<double>()};
    const Quat p = quaternionMultiply(q0, q1);

    CHECK(std::abs(p.w - want[0]) <= 1e-12);
    CHECK(std::abs(p.x - want[1]) <= 1e-12);
    CHECK(std::abs(p.y - want[2]) <= 1e-12);
    CHECK(std::abs(p.z - want[3]) <= 1e-12);
}

TEST_CASE("rotation about an arbitrary axis matches the reference", "[transform][golden][parity]") {
    const auto spec    = cases();
    const auto axisj   = spec["rotation_axis"];
    const auto anglesj = spec["rotation_angles"];
    const auto want    = readDoubles(fixtureDir() / "rotations.bin");
    REQUIRE(want.size() == anglesj.size() * 16);

    const Vec3 axis{static_cast<float>(axisj[0].get<double>()),
                    static_cast<float>(axisj[1].get<double>()),
                    static_cast<float>(axisj[2].get<double>())};

    double worst = 0.0;
    for (size_t i = 0; i < anglesj.size(); ++i) {
        CAPTURE(anglesj[i].get<double>());
        const double d = maxMatDiff(rotationMatrix(anglesj[i].get<double>(), axis), &want[i * 16]);
        worst          = std::max(worst, d);
        CHECK(d <= kMatTol);
    }
    INFO("worst rotation delta " << worst);
}

// Scalar-first is the project-wide convention. Eigen's .coeffs() is [x,y,z,w];
// a swap there still yields a unit quaternion that still rotates, just wrongly.
TEST_CASE("the quaternion layout is scalar-first", "[transform]") {
    // A 90-degree rotation about +X: w = cos(45), x = sin(45), y = z = 0.
    const Mat4 m = rotationMatrix(1.5707963267948966, Vec3{1, 0, 0});
    const Quat q = quaternionFromMatrix(m);

    CHECK(std::abs(std::abs(q.w) - 0.7071067811865476) < 1e-6);
    CHECK(std::abs(std::abs(q.x) - 0.7071067811865476) < 1e-6);
    CHECK(std::abs(q.y) < 1e-6);
    CHECK(std::abs(q.z) < 1e-6);
}
