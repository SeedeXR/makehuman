// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Walking a mesh surface, instead of raycasting at it.
//
// This exists because of a measured failure. The hair-style attempt of
// 2026-09-11 placed roots by casting a ray from the head centre onto the scalp,
// and the scalp is not a closed dome -- mapped over a direction grid it misses
// straight up, dead front and dead back, where the body carries seams. The
// nearest-direction FALLBACK for a missed ray looked robust and was the trap:
// it silently collapses every miss onto the scalp rim, so a wide sweep piles up
// at the front edge instead of spreading. Two rounds of retuning changed
// nothing, because no parameter was wrong.
//
// `memory/todo.md` records the conclusion: "placement must WALK THE SCALP
// SURFACE (geodesic paths over the mesh), not raycast from a centre. That is
// the next attempt's first task." This is that task, and nothing more -- the
// styles are a separate chunk, and they need this to exist first.
//
// Distance here is along mesh EDGES (Dijkstra over the edge graph), not true
// geodesic distance across face interiors. That overestimates by up to the
// usual ~4% on a regular triangulation, and it is the right trade: placement
// wants "spread out over the surface, never leaving it", which edge distance
// gives exactly, and an exact geodesic solver is a great deal of code for a
// tighter number nobody here needs.

#include "makehuman/core/SurfaceWalk.h"

#include "makehuman/core/Mesh.h"
#include "makehuman/core/ObjReader.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <numeric>
#include <vector>

using namespace mh::core;
using Catch::Matchers::WithinAbs;

namespace {

/// An n x n grid of quads in the XZ plane, one unit apart.
///
/// Flat and regular, so every expected distance is arithmetic a reader can
/// check: the edge path from one corner to the far one is exactly 2*(n-1).
Mesh makeGrid(uint32_t n) {
    Mesh m("grid", 4);
    std::vector<mh::foundation::Vec3> coords;
    std::vector<mh::foundation::Vec2> uvs;
    for (uint32_t z = 0; z < n; ++z) {
        for (uint32_t x = 0; x < n; ++x) {
            coords.push_back({static_cast<float>(x), 0.0F, static_cast<float>(z)});
            uvs.push_back({static_cast<float>(x), static_cast<float>(z)});
        }
    }
    std::vector<uint32_t> fv;
    for (uint32_t z = 0; z + 1 < n; ++z) {
        for (uint32_t x = 0; x + 1 < n; ++x) {
            const uint32_t a = z * n + x;
            fv.insert(fv.end(), {a, a + 1, a + n + 1, a + n});
        }
    }
    REQUIRE(m.setCoords(coords).has_value());
    REQUIRE(m.setUVs(uvs).has_value());
    m.addFaceGroup("g");
    std::vector<uint16_t> groups(fv.size() / 4, 0);
    REQUIRE(m.setFaces(fv, fv, groups).has_value());
    m.buildAdjacency();
    return m;
}

std::vector<uint32_t> allVertices(const Mesh& m) {
    std::vector<uint32_t> all(m.vertexCount());
    std::iota(all.begin(), all.end(), 0U);
    return all;
}

/// The BODY scalp: body-group vertices above the cranium centre's height.
///
/// The group restriction IS the region's correctness, not a refinement.
/// Measured on the shipped base mesh: height alone (y > 7.75) selects 303
/// vertices in EIGHTEEN disconnected components, because `helper-hair` (138
/// vertices) and `joint-head-2` (8) also sit above the cranium. Restricted to
/// the body it is 157 vertices in ONE component -- and `memory/todo.md` is
/// explicit that helper-hair is the wrong source, "a long-hair envelope
/// carrying ribbons down over the face".
std::vector<uint32_t> bodyScalp(const Mesh& m) {
    const auto body = m.findFaceGroup("body");
    REQUIRE(body.has_value());
    const auto fvert    = m.fvert();
    const auto fgroup   = m.group();
    const size_t stride = m.vertsPerPrimitive();
    std::vector<uint8_t> onBody(m.vertexCount(), 0U);
    for (size_t f = 0; f < fgroup.size(); ++f) {
        if (fgroup[f] != *body) continue;
        for (size_t c = 0; c < stride; ++c)
            onBody[fvert[f * stride + c]] = 1U;
    }
    std::vector<uint32_t> scalp;
    const auto coords = m.coord();
    for (uint32_t v = 0; v < coords.size(); ++v) {
        if (onBody[v] != 0U && coords[v].y > 7.75F) scalp.push_back(v);
    }
    return scalp;
}

}  // namespace

TEST_CASE("surface distance grows along the edges, not through space", "[core][surfacewalk]") {
    const Mesh m      = makeGrid(5);
    const auto region = allVertices(m);

    const auto d = surfaceDistance(m, region, std::vector<uint32_t>{0});
    REQUIRE(d.size() == m.vertexCount());

    CHECK(d[0] == 0.0F);
    CHECK_THAT(d[1], WithinAbs(1.0, 1e-5));  // one edge east
    CHECK_THAT(d[5], WithinAbs(1.0, 1e-5));  // one edge south
    // The far corner of a 5x5 grid: four east and four south, and NOT the
    // straight-line 5.657. Walking the surface is the whole point.
    CHECK_THAT(d[24], WithinAbs(8.0, 1e-5));
    // The diagonal neighbour shares a quad but no edge, so it costs two steps.
    CHECK_THAT(d[6], WithinAbs(2.0, 1e-5));
}

TEST_CASE("a vertex outside the region is never reached", "[core][surfacewalk]") {
    const Mesh m = makeGrid(5);
    // The left column only. Everything east of it is out of the region, so a
    // walk from the top-left corner can only go south.
    std::vector<uint32_t> column;
    for (uint32_t z = 0; z < 5; ++z)
        column.push_back(z * 5);

    const auto d = surfaceDistance(m, column, std::vector<uint32_t>{0});
    CHECK_THAT(d[20], WithinAbs(4.0, 1e-5));
    // Not merely far: unreachable. A region is a boundary the walk respects,
    // which is what keeps a scalp walk off the face.
    CHECK(std::isinf(d[1]));
    CHECK(std::isinf(d[24]));
}

TEST_CASE("several sources give the distance to the NEAREST of them", "[core][surfacewalk]") {
    const Mesh m      = makeGrid(5);
    const auto region = allVertices(m);

    const auto d = surfaceDistance(m, region, std::vector<uint32_t>{0, 24});
    CHECK(d[0] == 0.0F);
    CHECK(d[24] == 0.0F);
    // The centre is four steps from either corner.
    CHECK_THAT(d[12], WithinAbs(4.0, 1e-5));
}

TEST_CASE("spreading picks distinct vertices, as many as asked for", "[core][surfacewalk]") {
    const Mesh m      = makeGrid(5);
    const auto region = allVertices(m);

    const auto picks = spreadOverSurface(m, region, 6);
    REQUIRE(picks.size() == 6);
    auto sorted = picks;
    std::sort(sorted.begin(), sorted.end());
    CHECK(std::adjacent_find(sorted.begin(), sorted.end()) == sorted.end());
    for (const uint32_t p : picks)
        CHECK(p < m.vertexCount());
}

TEST_CASE("spreading does NOT collapse onto one corner", "[core][surfacewalk]") {
    const Mesh m      = makeGrid(5);
    const auto region = allVertices(m);

    // The regression this file exists for, in miniature. The old raycast placed
    // every missed ray at the rim; the equivalent failure here is picks that
    // cluster. Four picks on a 5x5 grid should sit far apart -- measured on the
    // farthest-point rule, no two closer than 4 edges.
    const auto picks = spreadOverSurface(m, region, 4);
    REQUIRE(picks.size() == 4);

    float worst = 1e9F;
    for (size_t i = 0; i < picks.size(); ++i) {
        const auto d = surfaceDistance(m, region, std::vector<uint32_t>{picks[i]});
        for (size_t j = 0; j < picks.size(); ++j) {
            if (i != j) worst = std::min(worst, d[picks[j]]);
        }
    }
    INFO("closest pair is " << worst << " edges apart");
    CHECK(worst >= 4.0F);
}

TEST_CASE("asking for more than the region holds returns the region", "[core][surfacewalk]") {
    const Mesh m = makeGrid(3);
    std::vector<uint32_t> region{0, 1, 3, 4};

    const auto picks = spreadOverSurface(m, region, 99);
    // Not an error and not a repeat: a caller asking for eighty roots on a
    // patch with nine vertices gets the nine, once each.
    CHECK(picks.size() == region.size());
    auto sorted = picks;
    std::sort(sorted.begin(), sorted.end());
    CHECK(std::adjacent_find(sorted.begin(), sorted.end()) == sorted.end());
}

TEST_CASE("an empty region or a zero count yields nothing", "[core][surfacewalk]") {
    const Mesh m = makeGrid(3);
    CHECK(spreadOverSurface(m, std::vector<uint32_t>{}, 4).empty());
    CHECK(spreadOverSurface(m, allVertices(m), 0).empty());
    CHECK(surfaceDistance(m, std::vector<uint32_t>{}, std::vector<uint32_t>{0}).size() ==
          m.vertexCount());
}

TEST_CASE("a source outside the region is ignored rather than seeding it", "[core][surfacewalk]") {
    const Mesh m = makeGrid(5);
    std::vector<uint32_t> column;
    for (uint32_t z = 0; z < 5; ++z)
        column.push_back(z * 5);

    // Vertex 24 is not in the column. Seeding from it would put a zero into a
    // region it does not belong to, and every distance downstream would be
    // measured from a point the caller excluded on purpose.
    const auto d = surfaceDistance(m, column, std::vector<uint32_t>{24});
    for (const uint32_t v : column)
        CHECK(std::isinf(d[v]));

    // THE discriminating assertion, and the first version of this test did not
    // have it. Checking only the region passes with the guard REMOVED: an
    // out-of-region source has no walkable edges either way, so the region
    // stays unreachable regardless. What actually differs is the source's own
    // entry -- 0, a distance reported for a vertex the caller excluded, versus
    // infinity. A mutation that dropped the guard walked straight through the
    // weaker version.
    CHECK(std::isinf(d[24]));
}

TEST_CASE("roots spread over the REAL scalp instead of piling at its rim", "[core][surfacewalk]") {
    // The regression, on the mesh that produced the original failure.
    //
    // The scalp region is taken by the measurement `memory/todo.md` records:
    // the head above y=6.0 spans y 6.000..8.491 with the cranium centre at
    // (0, 7.75, 0.50), and a hairline of -19 + 31*cos(theta) degrees. Rather
    // than reproduce that trigonometry here -- it belongs to the style
    // generator, not to this primitive -- the region is simply "the cranium
    // cap", every vertex above the cranium centre's height. That is a scalp by
    // any definition and it is enough to show the behaviour.
    const auto mesh = loadObj(std::filesystem::path(MH_DATA_DIR) / "3dobjs" / "base.obj");
    REQUIRE(mesh.has_value());

    const auto coords = mesh->coord();
    const auto scalp  = bodyScalp(*mesh);
    INFO("body scalp vertices: " << scalp.size());
    REQUIRE(scalp.size() == 157);  // measured; 303 would mean helpers crept in

    constexpr size_t kRoots = 24;
    const auto picks        = spreadOverSurface(*mesh, scalp, kRoots);
    REQUIRE(picks.size() == kRoots);

    // Every root is ON the scalp. The old fallback put them on the rim, which
    // is still technically on the mesh -- so this alone is not the assertion
    // that matters, it is only the floor.
    for (const uint32_t p : picks) {
        REQUIRE(std::find(scalp.begin(), scalp.end(), p) != scalp.end());
    }

    // THE assertion. Collapse looks like roots crowding together; spread looks
    // like every pair being far apart over the surface. MEASURED on the shipped
    // base mesh: 157 BODY scalp vertices, and the closest pair of 24 roots sits
    // 0.387 dm apart over the surface. (An earlier version of this test read
    // 1.126 dm because its region was the height-only 303, which included the
    // helper-hair envelope -- a bigger, more spread-out set, and the wrong
    // one.) The bound is roughly half the measurement, which still leaves the
    // failure it is for -- sweeps landing on top of one another at the front
    // edge -- at or near zero.
    float closest = 1e9F;
    for (const uint32_t a : picks) {
        const auto d = surfaceDistance(*mesh, scalp, std::vector<uint32_t>{a});
        for (const uint32_t b : picks) {
            if (a != b) closest = std::min(closest, d[b]);
        }
    }
    INFO("closest pair of roots: " << closest << " dm apart over the surface");
    CHECK(closest > 0.25F);

    // And they cover the cap rather than one side of it: both halves in x, and
    // both halves in z, carry roots. A pile at the front edge fails on z.
    // Measured on the body scalp: left 11 / right 13 and back 11 / front 13.
    // (Not the exact 12/12 the height-only region gave: that set was symmetric
    // in a way the body cap alone is not.) The bound is a third of an even
    // split, so it fails on a pile rather than on a mesh that is merely not
    // perfectly balanced.
    int left = 0, right = 0, front = 0, back = 0;
    for (const uint32_t p : picks) {
        (coords[p].x < 0.0F ? left : right)++;
        (coords[p].z < 0.5F ? back : front)++;
    }
    INFO("left " << left << " right " << right << " back " << back << " front " << front);
    CHECK(left >= 4);
    CHECK(right >= 4);
    CHECK(back >= 4);
    CHECK(front >= 4);
}

TEST_CASE("a path follows edges from source to target", "[core][surfacewalk]") {
    const Mesh m      = makeGrid(5);
    const auto region = allVertices(m);

    // Distances say HOW FAR the nape is from the hairline. Routing a cornrow
    // needs the actual chain of vertices between them, which is what this adds.
    const auto path = pathOverSurface(m, region, 0, 24);
    REQUIRE(path.size() == 9);  // 8 steps on a 5x5 grid, so 9 vertices
    CHECK(path.front() == 0);
    CHECK(path.back() == 24);

    // THE assertion: every consecutive pair is a real mesh edge. A "path" that
    // merely started and ended in the right place while teleporting through
    // the middle would satisfy the size and the endpoints.
    for (size_t i = 0; i + 1 < path.size(); ++i) {
        const auto step = surfaceDistance(m, region, std::vector<uint32_t>{path[i]});
        INFO("step " << i << ": " << path[i] << " -> " << path[i + 1]);
        CHECK_THAT(step[path[i + 1]], WithinAbs(1.0, 1e-5));
    }
}

TEST_CASE("a path stays inside the region", "[core][surfacewalk]") {
    const Mesh m = makeGrid(5);
    std::vector<uint32_t> column;
    for (uint32_t z = 0; z < 5; ++z)
        column.push_back(z * 5);

    // The region is the wall. This is what keeps a scalp path off the face.
    const auto path = pathOverSurface(m, column, 0, 20);
    REQUIRE(path.size() == 5);
    for (const uint32_t v : path)
        CHECK(std::find(column.begin(), column.end(), v) != column.end());
}

TEST_CASE("an unreachable or excluded endpoint yields no path", "[core][surfacewalk]") {
    const Mesh m = makeGrid(5);
    std::vector<uint32_t> column;
    for (uint32_t z = 0; z < 5; ++z)
        column.push_back(z * 5);

    // Not a truncated path and not a throw: empty, so a caller cannot mistake
    // a partial route for a whole one.
    CHECK(pathOverSurface(m, column, 0, 24).empty());          // target outside
    CHECK(pathOverSurface(m, column, 24, 0).empty());          // source outside
    CHECK(pathOverSurface(m, allVertices(m), 0, 99).empty());  // not a vertex
}

TEST_CASE("a path to the source itself is just the source", "[core][surfacewalk]") {
    const Mesh m      = makeGrid(5);
    const auto region = allVertices(m);
    const auto path   = pathOverSurface(m, region, 7, 7);
    REQUIRE(path.size() == 1);
    CHECK(path.front() == 7);
}

TEST_CASE("a real scalp path runs front to back without leaving the cap", "[core][surfacewalk]") {
    // The regression, on the mesh that produced the original failure. A
    // cornrow is exactly this: a route from the hairline to the nape that
    // never leaves the scalp.
    const auto mesh = loadObj(std::filesystem::path(MH_DATA_DIR) / "3dobjs" / "base.obj");
    REQUIRE(mesh.has_value());

    const auto coords = mesh->coord();
    const auto scalp  = bodyScalp(*mesh);
    REQUIRE(scalp.size() == 157);

    // Frontmost and backmost cap vertices near the midline: the two ends a
    // front-to-back parting runs between.
    uint32_t front = scalp.front();
    uint32_t back  = scalp.front();
    for (const uint32_t v : scalp) {
        if (std::abs(coords[v].x) > 0.15F) continue;  // near the midline
        if (coords[v].z > coords[front].z) front = v;
        if (coords[v].z < coords[back].z) back = v;
    }
    REQUIRE(front != back);

    const auto path = pathOverSurface(*mesh, scalp, front, back);
    REQUIRE(path.size() > 2);
    CHECK(path.front() == front);
    CHECK(path.back() == back);
    for (const uint32_t v : path) {
        REQUIRE(std::find(scalp.begin(), scalp.end(), v) != scalp.end());
    }

    // It actually travels: the straight-line span between the ends is real, so
    // a path that never moved in z would be a different route entirely.
    INFO("path of " << path.size() << " vertices, z " << coords[front].z << " -> "
                    << coords[back].z);
    CHECK(coords[front].z - coords[back].z > 0.5F);
}
