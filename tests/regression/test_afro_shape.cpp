// SPDX-License-Identifier: AGPL-3.0-or-later
//
// The afro (the owner's word is `bambucha`) as a SHAPE, not merely as a file
// that loads.
//
// The 2026-09-11 attempt produced silhouettes "too crude to carry those names"
// and shipped nothing, and a green suite would not have caught it -- the assets
// loaded fine, they just did not look like hair. So these assertions are about
// the geometry a render would show, and each one is a defect that was actually
// observed and fixed on 2026-09-13:
//
//   1. A shell that grows only at its deepest point renders as a flat-top:
//      mean offset 0.086 dm, 18 px of added height. -> the MEAN standoff is
//      pinned, not just the maximum.
//   2. Growth RADIALLY from the cranium centre makes a wide-brimmed mushroom,
//      because radial growth yields a ball only if the source is already a
//      sphere. -> the shell must not be wider at the hairline than at the
//      crown.
//   3. A vertex referenced by no face makes `loadObj` reject the file and the
//      character renders BALD rather than erroring (ObjReader.cpp:235-243).
//      -> the proxy must actually load and carry every vertex.
//   4. Offsets must point OUT of the scalp. An inward shell is invisible and
//      still passes a count check.

#include "makehuman/core/Proxy.h"

#include "makehuman/core/Mesh.h"
#include "makehuman/core/ObjReader.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <cmath>
#include <filesystem>
#include <vector>

using namespace mh::core;

namespace {

// The cranium centre, measured on the base mesh and recorded in memory/todo.md.
constexpr float kCx = 0.0F;
constexpr float kCy = 7.75F;
constexpr float kCz = 0.50F;

float distanceFromCentre(const mh::foundation::Vec3& p) {
    const float dx = p.x - kCx;
    const float dy = p.y - kCy;
    const float dz = p.z - kCz;
    return std::sqrt(dx * dx + dy * dy + dz * dz);
}

}  // namespace

TEST_CASE("the afro stands off the scalp everywhere it grows", "[asset][hair][afro]") {
    const auto base = loadObj(std::filesystem::path(MH_DATA_DIR) / "3dobjs" / "base.obj");
    REQUIRE(base.has_value());
    const auto afro = loadProxy(std::filesystem::path(MH_DATA_DIR) / "hair" / "afro.mhclo");
    REQUIRE(afro.has_value());

    std::vector<mh::foundation::Vec3> fitted;
    REQUIRE(fitProxy(*afro, base->coord(), fitted));
    REQUIRE(fitted.size() == afro->vertexCount());

    // Not one vertex may sink INTO the head. An inward shell is invisible and
    // would still satisfy a vertex count.
    const auto coords = base->coord();
    size_t grown      = 0;
    float total       = 0.0F;
    float worst       = 0.0F;
    for (size_t i = 0; i < fitted.size(); ++i) {
        const uint32_t b  = afro->refVerts[i][0];
        const float stand = distanceFromCentre(fitted[i]) - distanceFromCentre(coords[b]);
        INFO("proxy vertex " << i << " bound to base " << b << " stands off " << stand);
        CHECK(stand > -1e-4F);
        if (stand > 1e-3F) ++grown;
        total += std::max(0.0F, stand);
        worst = std::max(worst, stand);
    }

    // MEASURED on the shipped asset: 475 vertices, max standoff 0.78 dm, mean
    // 0.290. The MEAN is the assertion that matters -- the flat-top failure had
    // a perfectly good maximum (0.58) and a mean of 0.086.
    const float mean = total / static_cast<float>(fitted.size());
    INFO("grown " << grown << " of " << fitted.size() << ", mean " << mean << ", max " << worst);
    CHECK(worst > 0.6F);
    CHECK(mean > 0.20F);
    CHECK(grown > fitted.size() / 3);
}

TEST_CASE("the afro is a uniform shell, not a mass inflated from a point", "[asset][hair][afro]") {
    const auto base = loadObj(std::filesystem::path(MH_DATA_DIR) / "3dobjs" / "base.obj");
    REQUIRE(base.has_value());
    const auto afro = loadProxy(std::filesystem::path(MH_DATA_DIR) / "hair" / "afro.mhclo");
    REQUIRE(afro.has_value());
    std::vector<mh::foundation::Vec3> fitted;
    REQUIRE(fitProxy(*afro, base->coord(), fitted));

    // The mushroom failure, and the assertion that actually catches it.
    //
    // The first version of this test compared the shell's half-width below the
    // cranium centre against its half-width above, and a mutation proved that
    // decorative: growing every vertex radially from the centre widens the TOP
    // as well, so the comparison held and the mushroom passed.
    //
    // What genuinely separates the two is UNIFORM THICKNESS. Offsetting along
    // the surface normal cannot move a vertex further than `thickness`; growing
    // radially toward a target radius moves each vertex by whatever it takes to
    // get there, so the standoff runs away. Measured on the shipped asset:
    // max 0.78 dm, which is the generator's thickness exactly.
    const auto coords = base->coord();
    float worst       = 0.0F;
    for (size_t i = 0; i < fitted.size(); ++i) {
        const uint32_t b = afro->refVerts[i][0];
        const float dx   = fitted[i].x - coords[b].x;
        const float dy   = fitted[i].y - coords[b].y;
        const float dz   = fitted[i].z - coords[b].z;
        worst            = std::max(worst, std::sqrt(dx * dx + dy * dy + dz * dz));
    }
    INFO("largest standoff from the scalp: " << worst << " dm");
    CHECK(worst > 0.6F);
    CHECK(worst < 0.95F);
}
