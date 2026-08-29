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
