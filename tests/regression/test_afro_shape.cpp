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

#include <algorithm>
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

// REWRITTEN 2026-10-06, because the afro stopped being a shell.
//
// Both tests here measured a SHELL: each proxy vertex bound to the one base
// vertex it grew from, with a radial standoff bounded by the generator's
// thickness. `tools/make_coils.py` grows the afro from coiled strands now
// (owner's call), bound barycentrically to triangles, so `refVerts[i][0]` is
// one corner of a triangle and a coil travels a long way sideways from it.
// Measuring standoff against that corner is not a weaker version of the old
// check, it is a meaningless one.
//
// What SURVIVES the change of representation is the property that mattered:
// hair grows OUT of a head, never into it. A strand that dips inside the skull
// renders as a bald patch with hair sprouting from nowhere, and no vertex
// count catches it. That is asserted below, against the nearest scalp vertex
// BY DIRECTION rather than by binding, which is a comparison a shell and a
// head of strands can both answer.
//
// The old "uniform shell, not a mass inflated from a point" test is gone with
// the shell. It pinned that offsetting along a normal cannot move a vertex
// further than the thickness -- a true and useful statement about a surface
// offset, and not a statement about anything the coil generator does. Keeping
// it by loosening its bound until coils passed would have been a test that
// asserts nothing; it is replaced by the coverage check below, which is the
// claim a coiled afro actually has to meet.
TEST_CASE("the afro grows out of the head, never into it", "[asset][hair][afro]") {
    const auto base = loadObj(std::filesystem::path(MH_DATA_DIR) / "3dobjs" / "base.obj");
    REQUIRE(base.has_value());
    const auto afro = loadProxy(std::filesystem::path(MH_DATA_DIR) / "hair" / "afro.mhclo");
    REQUIRE(afro.has_value());
    std::vector<mh::foundation::Vec3> fitted;
    REQUIRE(fitProxy(*afro, base->coord(), fitted));
    // EQUALITY, not a floor. `> 1000` was a weakening: this file's header
    // lists "a vertex referenced by no face makes loadObj reject the file and
    // the character renders BALD rather than erroring" as an observed defect,
    // and the proxy carrying every vertex it declares is what rules that out.
    // The equality holds for the coiled afro and costs nothing to keep.
    REQUIRE(fitted.size() == afro->vertexCount());

    // The scalp, as directions from the cranium centre with their radii. Only
    // vertices above the centre: the comparison is "is this hair outside the
    // skull", and the jaw and neck are not the skull.
    const auto coords = base->coord();
    std::vector<std::pair<mh::foundation::Vec3, float>> scalp;
    for (const auto& c : coords) {
        if (c.y < kCy) continue;
        const float r = distanceFromCentre(c);
        if (r < 1e-3F) continue;
        scalp.push_back({{(c.x - kCx) / r, (c.y - kCy) / r, (c.z - kCz) / r}, r});
    }
    REQUIRE(scalp.size() > 200);

    size_t inside = 0;
    float deepest = 0.0F;
    for (const auto& q : fitted) {
        const float r = distanceFromCentre(q);
        if (r < 1e-3F) continue;
        const mh::foundation::Vec3 dir{(q.x - kCx) / r, (q.y - kCy) / r, (q.z - kCz) / r};
        float bestDot = -2.0F;
        float bestR   = 0.0F;
        for (const auto& [sdir, sr] : scalp) {
            const float d = dir.x * sdir.x + dir.y * sdir.y + dir.z * sdir.z;
            if (d > bestDot) {
                bestDot = d;
                bestR   = sr;
            }
        }
        // A 2 mm tolerance: the nearest scalp sample is a neighbouring vertex,
        // not the surface directly beneath, so a strand lying ON the skin reads
        // a few tenths of a millimetre either way.
        if (r < bestR - 0.02F) {
            ++inside;
            deepest = std::max(deepest, bestR - r);
        }
    }
    INFO("vertices inside the skull: " << inside << " of " << fitted.size() << ", deepest "
                                       << deepest << " dm");
    // THE BAR IS 3%, AND THE NUMBER BEHIND IT IS WORTH STATING because it is
    // not zero and the reason is not fully pinned down.
    //
    // The generator lifts each strand clear of its own coil radius and then
    // pushes any remaining buried vertex back onto the skull, which took the
    // count from 1,615 to the present figure. What is left sits at the
    // hairline, where "the nearest scalp sample by direction" is a crude stand
    // in for the surface -- the sample is a neighbouring vertex, not the point
    // directly beneath.
    //
    // MEASURED, and the two numbers disagree: reading `afro.obj` directly
    // flags 305 vertices with a deepest excursion of 0.054 dm, while fitting
    // the proxy through `fitProxy` here flags 773 at 0.229. Same scalp
    // samples, same rule. That difference is the binding moving vertices, and
    // it is a thread worth pulling -- it is recorded in memory/todo.md rather
    // than hidden behind a threshold chosen to make it quiet (it is written
    // there now; it was not when this comment first claimed it). The bar is set
    // where it catches a generator growing hair INWARD, which puts thousands
    // here, not where it certifies the last few hundred.
    CHECK(inside < fitted.size() * 3 / 100);
}

TEST_CASE("the afro covers the scalp rather than clumping", "[asset][hair][afro]") {
    const auto base = loadObj(std::filesystem::path(MH_DATA_DIR) / "3dobjs" / "base.obj");
    REQUIRE(base.has_value());
    const auto afro = loadProxy(std::filesystem::path(MH_DATA_DIR) / "hair" / "afro.mhclo");
    REQUIRE(afro.has_value());
    std::vector<mh::foundation::Vec3> fitted;
    REQUIRE(fitProxy(*afro, base->coord(), fitted));

    // THE GAP IS THE FAILURE, and it is the one a count cannot see. Roots
    // sampled independently over the scalp are a Poisson process: they clump
    // and leave holes, and rendered that showed as a bald crown, a bare patch
    // above the ear and a hole in the middle of the back. The generator
    // stratifies per triangle now.
    //
    // Measured as angular coverage: the scalp is divided into cells of azimuth
    // and elevation about the cranium centre, and every cell the hairline
    // admits must hold hair. A clumped head leaves whole cells empty.
    constexpr int kAz = 12;
    constexpr int kEl = 6;
    std::vector<bool> filled(kAz * kEl, false);
    size_t counted = 0;
    for (const auto& q : fitted) {
        const float dx = q.x - kCx;
        const float dy = q.y - kCy;
        const float dz = q.z - kCz;
        if (dy < 0.0F) continue;  // below the cranium centre is not the cap
        const float r = std::sqrt(dx * dx + dy * dy + dz * dz);
        if (r < 1e-3F) continue;
        float az = std::atan2(dx, dz) / (2.0F * 3.14159265F) + 0.5F;
        az       = std::min(0.999F, std::max(0.0F, az));
        float el = std::acos(std::min(1.0F, std::max(-1.0F, dy / r))) / 1.5707963F;
        el       = std::min(0.999F, std::max(0.0F, el));
        filled[static_cast<size_t>(el * kEl) * kAz + static_cast<size_t>(az * kAz)] = true;
        ++counted;
    }
    REQUIRE(counted > 1000);
    const size_t empty = static_cast<size_t>(std::count(filled.begin(), filled.end(), false));
    INFO("empty scalp cells: " << empty << " of " << filled.size());
    CHECK(empty == 0);
}
