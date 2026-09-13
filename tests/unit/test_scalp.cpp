// SPDX-License-Identifier: AGPL-3.0-or-later
//
// The hair-bearing scalp, against the measurements that defined it.
#include "makehuman/core/Scalp.h"

#include "makehuman/core/Mesh.h"
#include "makehuman/core/ObjReader.h"
#include "makehuman/core/SurfaceWalk.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <numbers>
#include <vector>

using Catch::Matchers::WithinAbs;
using namespace mh::core;

namespace {

/// Elevation and azimuth of a vertex about the cranium centre, in degrees.
struct Polar {
    float elevation;
    float azimuth;
};

Polar polar(const mh::foundation::Vec3& v) {
    constexpr float kDeg = 180.0F / std::numbers::pi_v<float>;
    const float dx       = v.x;
    const float dy       = v.y - kCraniumY;
    const float dz       = v.z - kCraniumZ;
    return {std::atan2(dy, std::hypot(dx, dz)) * kDeg, std::atan2(dx, dz) * kDeg};
}

const Mesh& baseMesh() {
    static const auto mesh = loadObj(std::filesystem::path(MH_DATA_DIR) / "3dobjs" / "base.obj");
    REQUIRE(mesh.has_value());
    return *mesh;
}

}  // namespace

TEST_CASE("the hairline sits where it was measured on the face", "[core][scalp]") {
    // The four landmarks the measurement recorded, in the cranium frame.
    CHECK_THAT(hairlineElevation(0.0F), WithinAbs(12.0, 1e-4));     // front, above the forehead
    CHECK_THAT(hairlineElevation(180.0F), WithinAbs(-50.0, 1e-4));  // nape
    CHECK_THAT(hairlineElevation(90.0F), WithinAbs(-19.0, 1e-4));   // ear
    // Symmetric in azimuth: a head has two matching sides.
    CHECK_THAT(hairlineElevation(-35.0F),
               WithinAbs(static_cast<double>(hairlineElevation(35.0F)), 1e-5));
}

TEST_CASE("the hair-bearing scalp is the region below the hairline, not the cap", "[core][scalp]") {
    const Mesh& mesh  = baseMesh();
    const auto coords = mesh.coord();
    const auto scalp  = hairBearingScalp(mesh);

    // MEASURED on the shipped base mesh. The cranium cap is 157; a region that
    // still reads 157 is the cap under a new name.
    INFO("hair-bearing vertices: " << scalp.size());
    CHECK(scalp.size() == 237);

    // A cross-check that this test's own polar math agrees with the region's:
    // both sides call hairlineElevation, so it is NOT a test of the region.
    for (const uint32_t v : scalp) {
        const Polar p = polar(coords[v]);
        INFO("vertex " << v << " elevation " << p.elevation << " azimuth " << p.azimuth);
        REQUIRE(p.elevation >= hairlineElevation(p.azimuth) - 1e-3F);
    }

    // THE assertion this chunk exists for: the region reaches the NAPE.
    // Rendering cornrows routed over the cap showed every row stopping 52 to 57
    // degrees short of it, leaving the occiput bare. MEASURED: the nape-most
    // body vertex above the hairline sits at elevation -45.15 degrees.
    const auto lowest = std::min_element(scalp.begin(), scalp.end(), [&](uint32_t a, uint32_t b) {
        return polar(coords[a]).elevation < polar(coords[b]).elevation;
    });
    REQUIRE(lowest != scalp.end());
    INFO("lowest elevation in the region: " << polar(coords[*lowest]).elevation);
    CHECK(polar(coords[*lowest]).elevation < -40.0F);

    // ...and it is NOT merely the cap plus a nape. The front hairline stands at
    // +12 degrees, so 29 cap vertices on the FOREHEAD are excluded -- the very
    // vertices the cap handed the cornrow rows as their starting points.
    const auto onForehead = std::count_if(scalp.begin(), scalp.end(), [&](uint32_t v) {
        const Polar p = polar(coords[v]);
        return std::abs(p.azimuth) < 5.0F && p.elevation < 10.0F;
    });
    CHECK(onForehead == 0);
}

TEST_CASE("the hair-bearing scalp is one walkable island", "[core][scalp]") {
    // Disconnection is the failure this project has already shipped once:
    // height alone selects 303 vertices in EIGHTEEN components because the
    // helper cages sit over the cranium too. A region in pieces cannot carry a
    // braid, because `pathOverSurface` returns nothing between two pieces.
    const Mesh& mesh = baseMesh();
    const auto scalp = hairBearingScalp(mesh);
    REQUIRE(scalp.size() > 1);

    const auto reached = surfaceDistance(mesh, scalp, std::vector<uint32_t>{scalp.front()});
    for (const uint32_t v : scalp) {
        INFO("vertex " << v << " is unreachable from " << scalp.front());
        REQUIRE(std::isfinite(reached[v]));
    }
}

TEST_CASE("a row runs from the hairline to the nape without leaving the scalp", "[core][scalp]") {
    // The cornrow route, end to end. Over the cranium cap this path existed but
    // spanned only +1 to +4 degrees of elevation at its back end; over the
    // hair-bearing region it has to cross the crown and come down the occiput.
    const Mesh& mesh  = baseMesh();
    const auto coords = mesh.coord();
    const auto scalp  = hairBearingScalp(mesh);

    const auto frontMost =
        *std::max_element(scalp.begin(), scalp.end(),
                          [&](uint32_t a, uint32_t b) { return coords[a].z < coords[b].z; });
    // The nape is the region's LOWEST point in elevation, not its lowest in z:
    // MEASURED, the minimum-z vertex sits at -19 degrees, above the ear, while
    // the nape-most sits at -45.
    const auto backMost =
        *std::min_element(scalp.begin(), scalp.end(), [&](uint32_t a, uint32_t b) {
            return polar(coords[a]).elevation < polar(coords[b]).elevation;
        });

    const auto path = pathOverSurface(mesh, scalp, frontMost, backMost);
    REQUIRE(path.size() > 3);
    CHECK(path.front() == frontMost);
    CHECK(path.back() == backMost);

    for (const uint32_t v : path) {
        REQUIRE(std::find(scalp.begin(), scalp.end(), v) != scalp.end());
    }

    // It goes OVER the crown: the highest point of the route is well above both
    // of its ends. Without this a path that cut around the side of the head --
    // shorter over the surface, and not a cornrow -- would pass.
    const auto top = *std::max_element(path.begin(), path.end(), [&](uint32_t a, uint32_t b) {
        return coords[a].y < coords[b].y;
    });
    CHECK(coords[top].y > coords[frontMost].y + 0.3F);
    CHECK(coords[top].y > coords[backMost].y + 0.3F);

    // ...and it ENDS on the nape. This is the assertion the cap region fails:
    // over the cap the same construction lands at elevation +6.6 degrees,
    // which is the crown, with the whole occiput left bare.
    CHECK(std::abs(polar(coords[backMost]).azimuth) > 150.0F);
    CHECK(polar(coords[backMost]).elevation < -40.0F);
}
