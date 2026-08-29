// SPDX-License-Identifier: AGPL-3.0-or-later
#include "makehuman/core/Subdivider.h"

#include "makehuman/core/ObjReader.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <cmath>
#include <filesystem>

using Catch::Matchers::WithinAbs;
using namespace mh::core;

namespace {

/// A single quad: the smallest case with a boundary on all four sides.
Mesh makeQuad() {
    Mesh m("quad", 4);
    REQUIRE(m.setCoords({{0, 0, 0}, {1, 0, 0}, {1, 0, 1}, {0, 0, 1}}).has_value());
    REQUIRE(m.setUVs({{0, 0}, {1, 0}, {1, 1}, {0, 1}}).has_value());
    m.addFaceGroup("g");
    REQUIRE(m.setFaces({0, 1, 2, 3}, {0, 1, 2, 3}, {0}).has_value());
    m.buildAdjacency();
    m.calcNormals();
    return m;
}

/// A 2x2 grid of quads: 9 vertices, one of which (the centre) is interior with
/// valence 4 -- the only case that exercises the interior Catmull-Clark rule.
Mesh makeGrid() {
    Mesh m("grid", 4);
    std::vector<Vec3> coords;
    for (int z = 0; z < 3; ++z) {
        for (int x = 0; x < 3; ++x) {
            coords.push_back(Vec3{static_cast<float>(x), 0.0F, static_cast<float>(z)});
        }
    }
    REQUIRE(m.setCoords(std::move(coords)).has_value());
    m.addFaceGroup("g");

    std::vector<uint32_t> fv;
    std::vector<uint16_t> fg;
    for (uint32_t z = 0; z < 2; ++z) {
        for (uint32_t x = 0; x < 2; ++x) {
            const uint32_t v0 = z * 3 + x;
            fv.insert(fv.end(), {v0, v0 + 1, v0 + 4, v0 + 3});
            fg.push_back(0);
        }
    }
    REQUIRE(m.setFaces(std::move(fv), {}, std::move(fg)).has_value());
    m.buildAdjacency();
    m.calcNormals();
    return m;
}

}  // namespace

TEST_CASE("subdivision rejects a non-quad mesh", "[core][subdiv]") {
    // The reference bails out the same way (catmull_clark_subdivision.py:516-518).
    Mesh m("tri", 3);
    REQUIRE(m.setCoords({{0, 0, 0}, {1, 0, 0}, {0, 0, 1}}).has_value());
    m.addFaceGroup("g");
    REQUIRE(m.setFaces({0, 1, 2}, {}, {0}).has_value());

    CHECK_FALSE(Subdivider::build(m).has_value());
}

TEST_CASE("one quad subdivides into four", "[core][subdiv]") {
    const Mesh m  = makeQuad();
    const auto sd = Subdivider::build(m);
    REQUIRE(sd.has_value());

    // 4 base + 1 face point + 4 edge points = 9 vertices; 1 quad -> 4 quads.
    CHECK(sd->mesh().vertexCount() == 9);
    CHECK(sd->mesh().faceCount() == 4);
    CHECK(sd->edgeCount() == 4);
    CHECK(sd->faceBase() == 4);
    CHECK(sd->edgeBase() == 5);
}

TEST_CASE("the face point is the average of its parent face", "[core][subdiv]") {
    const Mesh m  = makeQuad();
    const auto sd = Subdivider::build(m);
    REQUIRE(sd.has_value());

    const Vec3& c = sd->mesh().coord()[sd->faceBase()];
    CHECK_THAT(c.x, WithinAbs(0.5, 1e-6));
    CHECK_THAT(c.y, WithinAbs(0.0, 1e-6));
    CHECK_THAT(c.z, WithinAbs(0.5, 1e-6));
}

TEST_CASE("boundary edge points are edge midpoints", "[core][subdiv]") {
    // Every edge of a lone quad is a boundary, so each edge point must be the
    // plain midpoint rather than the 4-point average (:415-426).
    const Mesh m  = makeQuad();
    const auto sd = Subdivider::build(m);
    REQUIRE(sd.has_value());

    for (size_t e = 0; e < sd->edgeCount(); ++e) {
        const Vec3& p = sd->mesh().coord()[sd->edgeBase() + e];
        CHECK_THAT(p.y, WithinAbs(0.0, 1e-6));
        // Midpoint of a unit-square edge: one coordinate 0.5, the other 0 or 1.
        const bool onEdge = (std::abs(p.x - 0.5F) < 1e-6F &&
                             (std::abs(p.z) < 1e-6F || std::abs(p.z - 1.0F) < 1e-6F)) ||
                            (std::abs(p.z - 0.5F) < 1e-6F &&
                             (std::abs(p.x) < 1e-6F || std::abs(p.x - 1.0F) < 1e-6F));
        CHECK(onEdge);
    }
}

TEST_CASE("an interior vertex uses the Catmull-Clark rule", "[core][subdiv]") {
    // The centre of a 2x2 grid has valence 4 and equal edge/face counts, so it
    // takes (F + 2R + (n-3)P)/n. On a flat symmetric grid that leaves it put.
    const Mesh m  = makeGrid();
    const auto sd = Subdivider::build(m);
    REQUIRE(sd.has_value());

    constexpr size_t kCentre = 4;  // middle of the 3x3 vertex grid
    REQUIRE(m.nfacesAt(kCentre) == 4);

    const Vec3& p = sd->mesh().coord()[kCentre];
    CHECK_THAT(p.x, WithinAbs(1.0, 1e-5));
    CHECK_THAT(p.z, WithinAbs(1.0, 1e-5));
}

TEST_CASE("subdivision is planar-preserving for a flat grid", "[core][subdiv]") {
    const Mesh m  = makeGrid();
    const auto sd = Subdivider::build(m);
    REQUIRE(sd.has_value());

    // Every vertex of a flat mesh must stay in the plane.
    for (const Vec3& p : sd->mesh().coord()) {
        CHECK_THAT(p.y, WithinAbs(0.0, 1e-6));
    }
}

TEST_CASE("subdivision preserves face groups", "[core][subdiv]") {
    const Mesh m  = makeGrid();
    const auto sd = Subdivider::build(m);
    REQUIRE(sd.has_value());

    CHECK(sd->mesh().faceGroups().size() == m.faceGroups().size());
    for (const uint16_t g : sd->mesh().group())
        CHECK(g == 0);
}

TEST_CASE("UVs are carried through subdivision", "[core][subdiv]") {
    const Mesh m  = makeQuad();
    const auto sd = Subdivider::build(m);
    REQUIRE(sd.has_value());

    CHECK(sd->mesh().hasUV());
    // 4 base + 1 face + 4 edge UVs.
    CHECK(sd->mesh().uvCount() == 9);

    // The face UV is the average of the parent's four corners.
    const Vec2& c = sd->mesh().texco()[m.uvCount()];
    CHECK_THAT(c.x, WithinAbs(0.5, 1e-6));
    CHECK_THAT(c.y, WithinAbs(0.5, 1e-6));
}

TEST_CASE("refresh follows a morph without rebuilding topology", "[core][subdiv]") {
    Mesh m  = makeGrid();
    auto sd = Subdivider::build(m);
    REQUIRE(sd.has_value());

    const size_t vertsBefore = sd->mesh().vertexCount();
    const size_t facesBefore = sd->mesh().faceCount();

    for (Vec3& v : m.mutableCoord())
        v.y += 3.0F;
    m.calcNormals();
    sd->refresh(m);

    CHECK(sd->mesh().vertexCount() == vertsBefore);
    CHECK(sd->mesh().faceCount() == facesBefore);
    for (const Vec3& p : sd->mesh().coord()) {
        CHECK_THAT(p.y, WithinAbs(3.0, 1e-5));
    }
}

TEST_CASE("refresh is a no-op on a stale topology", "[core][subdiv]") {
    Mesh m  = makeGrid();
    auto sd = Subdivider::build(m);
    REQUIRE(sd.has_value());
    REQUIRE(sd->matches(m));

    // Same vertex count, fewer faces.
    REQUIRE(m.setFaces({0, 1, 4, 3}, {}, {0}).has_value());
    m.calcNormals();
    CHECK_FALSE(sd->matches(m));
    sd->refresh(m);  // must not index through a stale table
    CHECK(sd->mesh().faceCount() == 16);
}

TEST_CASE("the base mesh subdivides to the reference's counts", "[core][subdiv][golden][parity]") {
    const auto path = std::filesystem::path(MH_DATA_DIR) / "3dobjs" / "base.obj";
    if (!std::filesystem::exists(path)) SKIP("base.obj not present");

    const auto mesh = loadObj(path);
    REQUIRE(mesh.has_value());

    const auto sd = Subdivider::build(*mesh);
    REQUIRE(sd.has_value());

    // Measured from the Python reference on 2026-08-29 via
    // benchmarks/baseline_python_core.py:
    //     subdiv: {'verts': 75008, 'faces': 73944}
    CHECK(sd->mesh().vertexCount() == 75008);
    CHECK(sd->mesh().faceCount() == 73944);

    // 19,158 + 18,486 + edges == 75,008, so the mesh has 37,364 unique edges.
    CHECK(sd->edgeCount() == 75008 - 19158 - 18486);

    // The subdivided mesh must be structurally sound.
    const auto nVerts = static_cast<uint32_t>(sd->mesh().vertexCount());
    size_t bad        = 0;
    for (const uint32_t v : sd->mesh().fvert()) {
        if (v >= nVerts) ++bad;
    }
    CHECK(bad == 0);

    size_t badNormals = 0;
    for (const Vec3& n : sd->mesh().vnorm()) {
        const float len = std::sqrt(dot(n, n));
        if (!std::isfinite(len) || std::abs(len - 1.0F) > 1e-4F) ++badNormals;
    }
    CHECK(badNormals == 0);
}

TEST_CASE("subdivision roughly preserves the base mesh's bounds", "[core][subdiv][golden]") {
    const auto path = std::filesystem::path(MH_DATA_DIR) / "3dobjs" / "base.obj";
    if (!std::filesystem::exists(path)) SKIP("base.obj not present");

    const auto mesh = loadObj(path);
    REQUIRE(mesh.has_value());
    const auto sd = Subdivider::build(*mesh);
    REQUIRE(sd.has_value());

    // Catmull-Clark shrinks toward the limit surface, so bounds must stay
    // inside the original and close to it -- a smoke test that no vertex flew
    // off, which a sign or index error would cause immediately.
    const auto pb = mesh->boundingBox();
    const auto sb = sd->mesh().boundingBox();
    REQUIRE(pb.has_value());
    REQUIRE(sb.has_value());

    CHECK(sb->first.y >= pb->first.y - 1e-3F);
    CHECK(sb->second.y <= pb->second.y + 1e-3F);
    CHECK(sd->mesh().heightCm() > mesh->heightCm() * 0.9F);
}

TEST_CASE("the boundary base-vertex rule is exercised", "[core][subdiv]") {
    // Coverage gap found in review: on a rectangular grid every boundary vertex
    // has fewer than 3 faces, so makeQuad/makeGrid both land in the valence<3
    // fallback and the boundary rule at Subdivider.cpp:266 is never reached.
    //
    // An open 3-quad fan around vertex 0 gives it 3 faces and 4 edges, so
    // nFace >= 3 but nEdge != nFace -- the boundary branch.
    Mesh m("fan", 4);
    REQUIRE(m.setCoords({{0, 0, 0},
                         {1, 0, 0},
                         {1, 0, 1},
                         {0, 0, 1},
                         {-1, 0, 1},
                         {-1, 0, 0},
                         {-1, 0, -1},
                         {0, 0, -1}})
                .has_value());
    m.addFaceGroup("g");
    REQUIRE(m.setFaces({0, 1, 2, 3, 0, 3, 4, 5, 0, 5, 6, 7}, {}, {0, 0, 0}).has_value());
    m.buildAdjacency();
    m.calcNormals();

    const auto sd = Subdivider::build(m);
    REQUIRE(sd.has_value());

    // Confirm the branch really is the boundary one, not the interior rule.
    REQUIRE(m.nfacesAt(0) == 3);
    CHECK(sd->mesh().vertexCount() == 8 + 3 + 10);  // base + face points + edges

    // Vertex 0 has 4 incident edges, 2 of them boundary (to 1 and to 7). The
    // boundary rule is (sum of boundary edge midpoints + P) / (count + 1)
    //   = ((0.5,0,0) + (0,0,-0.5) + (0,0,0)) / 3
    const Vec3& p = sd->mesh().coord()[0];
    CHECK_THAT(p.x, WithinAbs(0.5 / 3.0, 1e-5));
    CHECK_THAT(p.y, WithinAbs(0.0, 1e-6));
    CHECK_THAT(p.z, WithinAbs(-0.5 / 3.0, 1e-5));
}

TEST_CASE("a triangle mesh is declined, as the reference declines it", "[core][subdiv]") {
    // Review finding: the gate must be vertsPerFaceForExport, not
    // vertsPerPrimitive. A triangle mesh from OBJ is stored as degenerate quads,
    // so vertsPerPrimitive() is 4 while export is 3 -- gating on the former
    // would subdivide meshes the reference declines
    // (catmull_clark_subdivision.py:516-518).
    Mesh m("tris", 4);
    REQUIRE(m.setCoords({{0, 0, 0}, {1, 0, 0}, {0, 0, 1}, {1, 0, 1}}).has_value());
    m.addFaceGroup("g");
    REQUIRE(m.setFaces({0, 1, 2, 0, 1, 3, 2, 1}, {}, {0, 0}).has_value());
    REQUIRE(m.vertsPerPrimitive() == 4);
    REQUIRE(m.vertsPerFaceForExport() == 3);  // degenerate quads

    CHECK_FALSE(Subdivider::build(m).has_value());
}

TEST_CASE("subdivision does not require the parent's adjacency", "[core][subdiv]") {
    // Review finding: refresh() read parent.nfacesAt()/faceAt(). If
    // buildAdjacency() was never called -- or setFaces() was called after it,
    // which clears them -- every vertex reported 0 faces and silently took the
    // valence<3 fallback. The subdivider now owns its face adjacency.
    Mesh with = makeGrid();  // has buildAdjacency()
    Mesh without("grid", 4);
    REQUIRE(
        without.setCoords(std::vector<Vec3>(with.coord().begin(), with.coord().end())).has_value());
    without.addFaceGroup("g");
    REQUIRE(without
                .setFaces(std::vector<uint32_t>(with.fvert().begin(), with.fvert().end()), {},
                          std::vector<uint16_t>(with.group().begin(), with.group().end()))
                .has_value());
    // deliberately NO buildAdjacency()

    const auto a = Subdivider::build(with);
    const auto b = Subdivider::build(without);
    REQUIRE(a.has_value());
    REQUIRE(b.has_value());

    REQUIRE(a->mesh().vertexCount() == b->mesh().vertexCount());
    for (size_t i = 0; i < a->mesh().vertexCount(); ++i) {
        CHECK_THAT(b->mesh().coord()[i].x,
                   WithinAbs(static_cast<double>(a->mesh().coord()[i].x), 1e-6));
        CHECK_THAT(b->mesh().coord()[i].z,
                   WithinAbs(static_cast<double>(a->mesh().coord()[i].z), 1e-6));
    }
}

TEST_CASE("refresh detects a same-size topology swap", "[core][subdiv]") {
    // Review finding: matches() compared counts only, so swapping in a
    // different topology with the same counts passed and refresh() wrote wrong
    // geometry through a stale edge table with no error.
    Mesh m  = makeGrid();
    auto sd = Subdivider::build(m);
    REQUIRE(sd.has_value());
    REQUIRE(sd->matches(m));

    // Same vertex count, same face count, different faces.
    std::vector<uint32_t> swapped(m.fvert().begin(), m.fvert().end());
    std::swap(swapped[0], swapped[2]);
    REQUIRE(
        m.setFaces(std::move(swapped), {}, std::vector<uint16_t>(m.faceCount(), 0)).has_value());

    CHECK_FALSE(sd->matches(m));  // was true before the fix
}

TEST_CASE("the subdivided mesh's morph base is its own geometry", "[core][subdiv]") {
    // Review finding: setCoords in build() captured the zero placeholder as
    // origCoord_, so resetToOriginal() collapsed the mesh to the origin.
    Mesh m  = makeGrid();
    auto sd = Subdivider::build(m);
    REQUIRE(sd.has_value());

    const Vec3 before = sd->mesh().coord()[4];
    sd->mesh().resetToOriginal();
    const Vec3 after = sd->mesh().coord()[4];

    CHECK_THAT(after.x, WithinAbs(static_cast<double>(before.x), 1e-6));
    CHECK_THAT(after.z, WithinAbs(static_cast<double>(before.z), 1e-6));
}
