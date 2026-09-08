// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Quadric-error-metric decimation.
//
// The reference has NO decimator -- checked, `legacy/python/` has no
// corrective, LOD or simplification code at all -- so there is no parity
// fixture to compare against and every claim here is an invariant or a
// measurement on a mesh whose right answer is known by construction.
//
// Two synthetic meshes carry most of the load. A PLANE has zero quadric error
// everywhere in its interior, so a correct implementation may flatten it as far
// as the topology allows and every surviving vertex must still lie in the
// plane. A CYLINDER has a known radius, so shape preservation is a number
// rather than an impression.

#include "makehuman/core/Decimator.h"

#include "makehuman/core/Mesh.h"
#include "makehuman/core/ObjReader.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <map>
#include <numbers>
#include <set>
#include <vector>

using namespace mh;
using Catch::Approx;

namespace {

/// A `w` x `h` grid of quads in the z = 0 plane, one unit apart.
core::Mesh plane(size_t w, size_t h) {
    core::Mesh m("plane");
    std::vector<core::Vec3> coords;
    for (size_t y = 0; y <= h; ++y) {
        for (size_t x = 0; x <= w; ++x) {
            coords.push_back({static_cast<float>(x), static_cast<float>(y), 0.0F});
        }
    }
    REQUIRE(m.setCoords(std::move(coords)).has_value());

    std::vector<uint32_t> fv;
    const auto at = [w](size_t x, size_t y) { return static_cast<uint32_t>(y * (w + 1) + x); };
    for (size_t y = 0; y < h; ++y) {
        for (size_t x = 0; x < w; ++x) {
            fv.push_back(at(x, y));
            fv.push_back(at(x + 1, y));
            fv.push_back(at(x + 1, y + 1));
            fv.push_back(at(x, y + 1));
        }
    }
    REQUIRE(m.setFaces(std::move(fv), {}, {}).has_value());
    return m;
}

/// A closed tube of `segments` around and `rings` along, radius 1, no caps.
core::Mesh cylinder(size_t segments, size_t rings) {
    core::Mesh m("cylinder");
    std::vector<core::Vec3> coords;
    for (size_t r = 0; r <= rings; ++r) {
        for (size_t s = 0; s < segments; ++s) {
            const double a =
                2.0 * std::numbers::pi * static_cast<double>(s) / static_cast<double>(segments);
            coords.push_back({static_cast<float>(std::cos(a)), static_cast<float>(r),
                              static_cast<float>(std::sin(a))});
        }
    }
    REQUIRE(m.setCoords(std::move(coords)).has_value());

    std::vector<uint32_t> fv;
    const auto at = [segments](size_t s, size_t r) {
        return static_cast<uint32_t>(r * segments + (s % segments));
    };
    for (size_t r = 0; r < rings; ++r) {
        for (size_t s = 0; s < segments; ++s) {
            fv.push_back(at(s, r));
            fv.push_back(at(s + 1, r));
            fv.push_back(at(s + 1, r + 1));
            fv.push_back(at(s, r + 1));
        }
    }
    REQUIRE(m.setFaces(std::move(fv), {}, {}).has_value());
    return m;
}

size_t triangleCount(const core::Mesh& m) {
    return m.faceCount();
}

/// Every corner index in range, every triangle non-degenerate, no vertex left
/// unreferenced. Any decimator that violates one of these produces a file that
/// a reader either rejects or renders as holes.
void checkWellFormed(const core::Mesh& m) {
    REQUIRE(m.vertsPerFaceForExport() == 3);
    std::vector<bool> used(m.vertexCount(), false);
    std::set<std::array<uint32_t, 3>> triangles;
    std::map<std::pair<uint32_t, uint32_t>, int> edges;
    const auto fv = m.fvert();
    for (size_t f = 0; f < m.faceCount(); ++f) {
        const uint32_t a = fv[f * 4];
        const uint32_t b = fv[f * 4 + 1];
        const uint32_t c = fv[f * 4 + 2];
        REQUIRE(fv[f * 4 + 3] == a);  // stored as a degenerate quad
        REQUIRE(a < m.vertexCount());
        REQUIRE(b < m.vertexCount());
        REQUIRE(c < m.vertexCount());
        REQUIRE(a != b);
        REQUIRE(b != c);
        REQUIRE(a != c);
        used[a] = used[b] = used[c] = true;
        for (size_t i = 0; i < 3; ++i) {
            const uint32_t p0 = fv[f * 4 + i];
            const uint32_t p1 = fv[f * 4 + (i + 1) % 3];
            ++edges[{std::min(p0, p1), std::max(p0, p1)}];
        }
        // A duplicate triangle is not degenerate and every index is in range,
        // so nothing above sees it -- and it is exactly what an edge collapse
        // on a tetrahedron produces. Two coincident triangles render as
        // z-fighting and export as a non-manifold mesh.
        std::array<uint32_t, 3> key{a, b, c};
        std::ranges::sort(key);
        REQUIRE(triangles.insert(key).second);
    }
    CHECK(std::ranges::find(used, false) == used.end());

    // MANIFOLD: no edge may be shared by more than two triangles. This is the
    // property the link condition in the collapse loop exists to protect, and
    // it is the one an exporter cares about -- a non-manifold mesh is what
    // makes a DCC refuse a boolean, a subdivision or a UV unwrap.
    for (const auto& [edge, count] : edges) {
        INFO("edge " << edge.first << "-" << edge.second << " is in " << count << " triangles");
        CHECK(count <= 2);
    }
}

std::pair<core::Vec3, core::Vec3> bounds(const core::Mesh& m) {
    core::Vec3 lo{1e30F, 1e30F, 1e30F};
    core::Vec3 hi{-1e30F, -1e30F, -1e30F};
    for (const core::Vec3& v : m.coord()) {
        lo = {std::min(lo.x, v.x), std::min(lo.y, v.y), std::min(lo.z, v.z)};
        hi = {std::max(hi.x, v.x), std::max(hi.y, v.y), std::max(hi.z, v.z)};
    }
    return {lo, hi};
}

}  // namespace

TEST_CASE("ratio 1 triangulates and collapses nothing", "[core][decimate]") {
    const core::Mesh src = plane(4, 4);
    const auto out       = core::decimate(src, {.ratio = 1.0F});
    REQUIRE(out.has_value());

    // 16 quads -> 32 triangles, and not one vertex moved or merged.
    CHECK(triangleCount(*out) == 32);
    CHECK(out->vertexCount() == src.vertexCount());
    checkWellFormed(*out);
    for (size_t v = 0; v < src.vertexCount(); ++v) {
        CHECK(out->coord()[v].x == src.coord()[v].x);
        CHECK(out->coord()[v].y == src.coord()[v].y);
        CHECK(out->coord()[v].z == src.coord()[v].z);
    }
}

TEST_CASE("the target triangle count is reached", "[core][decimate]") {
    const core::Mesh src = plane(20, 20);  // 400 quads -> 800 triangles
    const auto out       = core::decimate(src, {.ratio = 0.5F});
    REQUIRE(out.has_value());
    checkWellFormed(*out);

    // Each collapse removes exactly two triangles, so the count lands on the
    // target or one collapse short of it -- never merely "smaller".
    CHECK(triangleCount(*out) <= 400);
    CHECK(triangleCount(*out) >= 398);
}

TEST_CASE("a plane stays flat, however far it is decimated", "[core][decimate]") {
    // Every interior collapse on a plane has zero quadric error. An
    // implementation that placed the survivor anywhere but in the plane -- the
    // classic sign of a sign error in the quadric solve -- shows up here as a
    // non-zero z, and NOWHERE else in this file.
    const auto out = core::decimate(plane(16, 16), {.ratio = 0.25F});
    REQUIRE(out.has_value());
    checkWellFormed(*out);
    for (const core::Vec3& v : out->coord())
        CHECK(v.z == Approx(0.0F).margin(1e-5));
}

TEST_CASE("a plane's outline survives", "[core][decimate]") {
    // Collapsing a boundary vertex inwards is how a decimator eats a mesh's
    // silhouette. The plane is 16 x 16, so the bounds must still be exactly
    // that.
    const auto out = core::decimate(plane(16, 16), {.ratio = 0.25F});
    REQUIRE(out.has_value());
    const auto [lo, hi] = bounds(*out);
    CHECK(lo.x == Approx(0.0F).margin(1e-5));
    CHECK(lo.y == Approx(0.0F).margin(1e-5));
    CHECK(hi.x == Approx(16.0F).margin(1e-5));
    CHECK(hi.y == Approx(16.0F).margin(1e-5));
}

TEST_CASE("a cylinder keeps its radius", "[core][decimate]") {
    // Shape preservation as a number. A midpoint collapse -- the lazy
    // alternative to solving the quadric -- pulls every survivor towards the
    // axis and shrinks the radius measurably; the quadric solve keeps it.
    const auto out = core::decimate(cylinder(32, 8), {.ratio = 0.4F});
    REQUIRE(out.has_value());
    checkWellFormed(*out);

    double worst = 0.0;
    for (const core::Vec3& v : out->coord()) {
        const double r = std::hypot(static_cast<double>(v.x), static_cast<double>(v.z));
        worst          = std::max(worst, std::abs(r - 1.0));
    }
    CHECK(worst < 0.02);
}

TEST_CASE("decimation is deterministic", "[core][decimate]") {
    const core::Mesh src = cylinder(24, 6);
    const auto a         = core::decimate(src, {.ratio = 0.5F});
    const auto b         = core::decimate(src, {.ratio = 0.5F});
    REQUIRE(a.has_value());
    REQUIRE(b.has_value());
    REQUIRE(a->vertexCount() == b->vertexCount());
    REQUIRE(a->faceCount() == b->faceCount());
    for (size_t v = 0; v < a->vertexCount(); ++v) {
        CHECK(a->coord()[v].x == b->coord()[v].x);
        CHECK(a->coord()[v].y == b->coord()[v].y);
        CHECK(a->coord()[v].z == b->coord()[v].z);
    }
    CHECK(std::ranges::equal(a->fvert(), b->fvert()));
}

TEST_CASE("an unreachable target stops early instead of failing", "[core][decimate][slow]") {
    // Measured on the base mesh: it reduces exactly on target down to 25%, then
    // FLOORS at 5,451 triangles (14.7%), and asking for 5% gives the same mesh
    // as asking for 10%. Two things stop it: the attribute refusals, once every
    // remaining edge touches a UV seam or a group boundary, and the error
    // ceiling.
    //
    // Pinned because the alternatives are both wrong. Erroring would fail a
    // legitimate request. Looping forever on a queue that can never reach the
    // target is the bug this asserts the absence of. And *reaching* 5% is the
    // worst of the three: unbounded, this mesh goes to 4,373 triangles with
    // every numeric invariant above still passing, and the Blender render shows
    // a flat sheet where the chest was.
    const auto base = core::loadObj(std::filesystem::path(MH_DATA_DIR) / "3dobjs" / "base.obj");
    REQUIRE(base.has_value());
    const auto tenth     = core::decimate(*base, {.ratio = 0.10F});
    const auto twentieth = core::decimate(*base, {.ratio = 0.05F});
    REQUIRE(tenth.has_value());
    REQUIRE(twentieth.has_value());
    CHECK(tenth->faceCount() == twentieth->faceCount());
    CHECK(tenth->faceCount() == 5451);
    // Above the unbounded floor by a real margin: this is what says the ceiling
    // is doing something. Removing it drops this to 4,373.
    CHECK(tenth->faceCount() > 5000);
}

TEST_CASE("no collapse moves the surface further than the ceiling allows",
          "[core][decimate][slow]") {
    // The ceiling in model units, asserted where it can be seen: every
    // surviving vertex must lie within it of SOME original vertex. That is
    // weaker than a true surface distance and still catches the failure --
    // unbounded, the worst collapse on this mesh moves a vertex 9.57 dm, more
    // than half the body's height.
    const auto base = core::loadObj(std::filesystem::path(MH_DATA_DIR) / "3dobjs" / "base.obj");
    REQUIRE(base.has_value());
    const auto out = core::decimate(*base, {.ratio = 0.10F});
    REQUIRE(out.has_value());

    const auto [lo, hi] = bounds(*base);
    const auto dx       = static_cast<double>(hi.x - lo.x);
    const auto dy       = static_cast<double>(hi.y - lo.y);
    const auto dz       = static_cast<double>(hi.z - lo.z);
    const double diag   = std::sqrt(dx * dx + dy * dy + dz * dz);
    // Generous against the 0.004 ceiling: the bound being asserted is that
    // nothing moved by a body-scale distance, not the exact tolerance.
    const double limit = diag * 0.05;

    double worst = 0.0;
    for (const core::Vec3& v : out->coord()) {
        double best = 1e30;
        for (const core::Vec3& o : base->coord()) {
            const auto ex = static_cast<double>(v.x - o.x);
            const auto ey = static_cast<double>(v.y - o.y);
            const auto ez = static_cast<double>(v.z - o.z);
            best          = std::min(best, ex * ex + ey * ey + ez * ez);
        }
        worst = std::max(worst, std::sqrt(best));
    }
    INFO("worst surviving vertex is " << worst << " from any original, limit " << limit);
    CHECK(worst < limit);
}

TEST_CASE("a closed surface stays a surface", "[core][decimate]") {
    // A CLOSED mesh is where topological degradation shows: there is no
    // boundary to stop the collapse and no open edge to make it expensive, so
    // an implementation without the link condition and the tetrahedron guard
    // welds the surface into itself. An octahedron asked to shed 90% of its
    // faces has nowhere to go but wrong.
    //
    // checkWellFormed is the assertion -- specifically its duplicate-triangle
    // check, which is what two coincident faces look like from the outside.
    core::Mesh m("octahedron");
    REQUIRE(m.setCoords({{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}})
                .has_value());
    // Eight triangles, stored as degenerate quads, wound outwards.
    REQUIRE(m.setFaces({0, 2, 4, 0, 2, 1, 4, 2, 1, 3, 4, 1, 3, 0, 4, 3,
                        2, 0, 5, 2, 1, 2, 5, 1, 3, 1, 5, 3, 0, 3, 5, 0},
                       {}, {})
                .has_value());
    REQUIRE(m.faceCount() == 8);

    const auto out = core::decimate(m, {.ratio = 0.1F});
    REQUIRE(out.has_value());
    checkWellFormed(*out);
    // Four is the fewest triangles a closed surface can have. Going below it
    // means the mesh has been folded into itself.
    CHECK(out->faceCount() >= 4);
}

TEST_CASE("a ratio that is not a fraction is refused", "[core][decimate]") {
    const core::Mesh src = plane(2, 2);
    for (const float bad : {0.0F, -0.5F, 1.5F}) {
        const auto out = core::decimate(src, {.ratio = bad});
        REQUIRE_FALSE(out.has_value());
        CHECK(out.error().kind == core::DecimateErrorKind::BadRatio);
    }
    const auto nan = core::decimate(src, {.ratio = std::nanf("")});
    REQUIRE_FALSE(nan.has_value());
    CHECK(nan.error().kind == core::DecimateErrorKind::BadRatio);
}

TEST_CASE("a mesh with no faces is refused", "[core][decimate]") {
    core::Mesh empty("empty");
    const auto out = core::decimate(empty, {.ratio = 0.5F});
    REQUIRE_FALSE(out.has_value());
    CHECK(out.error().kind == core::DecimateErrorKind::NoFaces);
}

TEST_CASE("a collapse never crosses a face group", "[core][decimate]") {
    // Face groups are load-bearing here: `staticFaceMask` hides 138 helper
    // groups by NAME, so a decimator that merged a helper vertex into a body
    // vertex would leave geometry that cannot be hidden any more. Two groups
    // that share an edge, decimated hard: every triangle must still belong to
    // the group it started in, and both groups must survive.
    core::Mesh m("two-groups");
    core::Mesh src       = plane(8, 8);
    const uint16_t lower = m.addFaceGroup("body");
    const uint16_t upper = m.addFaceGroup("helper-thing");
    REQUIRE(lower == 0);
    REQUIRE(upper == 1);

    std::vector<core::Vec3> coords(src.coord().begin(), src.coord().end());
    REQUIRE(m.setCoords(std::move(coords)).has_value());
    std::vector<uint32_t> fv(src.fvert().begin(), src.fvert().end());
    std::vector<uint16_t> groups;
    for (size_t f = 0; f < src.faceCount(); ++f) {
        groups.push_back(f < src.faceCount() / 2 ? lower : upper);
    }
    REQUIRE(m.setFaces(std::move(fv), {}, std::move(groups)).has_value());

    const auto out = core::decimate(m, {.ratio = 0.3F});
    REQUIRE(out.has_value());
    checkWellFormed(*out);

    std::set<uint16_t> seen;
    for (size_t f = 0; f < out->faceCount(); ++f)
        seen.insert(out->group()[f]);
    CHECK(seen == std::set<uint16_t>{lower, upper});
    CHECK(out->faceGroups().size() == 2);

    // The assertions above are NOT the test, and a mutation proved it: a group
    // is a per-FACE attribute that travels with its triangle, so dropping the
    // boundary check entirely still leaves both groups present and both
    // face-group entries intact. It passed.
    //
    // What the check actually protects is the SHARE: how many vertices belong
    // to faces of both groups. The plane's two halves meet along one row of 9
    // vertices, and collapses within that row can only reduce the count. A
    // collapse ACROSS the boundary hands one group's vertex to the other, so it
    // grows -- and on the base mesh that is a helper cage dragging a visible
    // body vertex out of place, geometry the face mask can no longer hide
    // because it is no longer only the helper's.
    const auto sharedVerts = [](const core::Mesh& mesh) {
        std::vector<std::set<uint16_t>> of(mesh.vertexCount());
        for (size_t f = 0; f < mesh.faceCount(); ++f) {
            for (size_t c = 0; c < 4; ++c)
                of[mesh.fvert()[f * 4 + c]].insert(mesh.group()[f]);
        }
        return std::ranges::count_if(of, [](const std::set<uint16_t>& g) { return g.size() > 1; });
    };
    CHECK(sharedVerts(m) == 9);
    CHECK(sharedVerts(*out) <= 9);

    // Honest limit: dropping the group check STILL passes everything here, and
    // the count above is why -- a collapse conserves it. Merging a vertex that
    // is in group 0 only into one that is in both gives the survivor both and
    // removes the other, so the tally is unchanged. Measured on the base mesh,
    // the check cannot fire at all: 0 of 19,158 vertices belong to more than
    // one of the 139 groups, so every group is a separate vertex island. The
    // check is kept for meshes where that is not true; this case documents the
    // gap rather than pretending to close it.
}

TEST_CASE("UVs survive, and no collapse crosses a seam", "[core][decimate]") {
    // The UV index space is independent of the vertex one, so a collapse that
    // merged two vertices carrying different UVs would smear the texture
    // across the seam. Prevented by refusing the collapse rather than by
    // interpolating -- which is why the assertion is that every surviving
    // corner still names a UV that exists.
    core::Mesh m("uv-plane");
    core::Mesh src = plane(8, 8);
    std::vector<core::Vec3> coords(src.coord().begin(), src.coord().end());
    REQUIRE(m.setCoords(std::move(coords)).has_value());

    std::vector<core::Vec2> uvs;
    for (const core::Vec3& v : m.coord())
        uvs.push_back({v.x / 8.0F, v.y / 8.0F});
    REQUIRE(m.setUVs(std::move(uvs)).has_value());

    std::vector<uint32_t> fv(src.fvert().begin(), src.fvert().end());
    std::vector<uint32_t> fu = fv;  // one UV per vertex here
    REQUIRE(m.setFaces(std::move(fv), std::move(fu), {}).has_value());

    const auto out = core::decimate(m, {.ratio = 0.4F});
    REQUIRE(out.has_value());
    checkWellFormed(*out);
    REQUIRE(out->hasUV());
    for (const uint32_t u : out->fuvs())
        CHECK(u < out->uvCount());

    // The invariant is that the vertex-to-UV correspondence stays ONE to one.
    // It cannot be "the UV still equals x/8": a survivor moves to the position
    // that minimises the quadric and keeps its own UV, so the two drift apart
    // by design -- interpolating the UV instead is what would smear a real
    // texture. What must not happen is one vertex ending up with two different
    // UVs across its corners, which is a seam appearing where there was none.
    const auto fvo = out->fvert();
    const auto fuo = out->fuvs();
    std::vector<uint32_t> uvOf(out->vertexCount(), UINT32_MAX);
    for (size_t c = 0; c < fvo.size(); ++c) {
        if (uvOf[fvo[c]] == UINT32_MAX) uvOf[fvo[c]] = fuo[c];
        CHECK(uvOf[fvo[c]] == fuo[c]);
    }

    // ...and the UV table is COMPACTED, not inherited whole. A quarter-size
    // mesh still carrying every original UV is not a level of detail.
    CHECK(out->uvCount() == out->vertexCount());
    CHECK(out->uvCount() < 81);  // the 9x9 grid it started from
}

TEST_CASE("the base mesh decimates to a quarter and keeps its silhouette",
          "[core][decimate][slow]") {
    const auto base = core::loadObj(std::filesystem::path(MH_DATA_DIR) / "3dobjs" / "base.obj");
    REQUIRE(base.has_value());
    const auto [lo0, hi0] = bounds(*base);

    const auto out = core::decimate(*base, {.ratio = 0.25F});
    REQUIRE(out.has_value());
    checkWellFormed(*out);

    // 18,486 quads -> 36,972 triangles -> a quarter of that.
    CHECK(triangleCount(*out) <= 9243);
    CHECK(triangleCount(*out) > 9000);

    // The body is ~16.9 dm tall; a decimator that ate the silhouette would
    // lose centimetres off the extremes, so the bound is tight in absolute
    // terms rather than a percentage.
    const auto [lo, hi] = bounds(*out);
    CHECK(lo.y == Approx(lo0.y).margin(0.05));
    CHECK(hi.y == Approx(hi0.y).margin(0.05));
    CHECK(lo.x == Approx(lo0.x).margin(0.05));
    CHECK(hi.x == Approx(hi0.x).margin(0.05));

    // Every face group that had geometry still has some: this is the property
    // `staticFaceMask` depends on.
    std::set<uint16_t> before;
    for (size_t f = 0; f < base->faceCount(); ++f)
        before.insert(base->group()[f]);
    std::set<uint16_t> after;
    for (size_t f = 0; f < out->faceCount(); ++f)
        after.insert(out->group()[f]);
    CHECK(after == before);
}
