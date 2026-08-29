// SPDX-License-Identifier: AGPL-3.0-or-later
#include "makehuman/core/RenderMesh.h"

#include "makehuman/core/ObjReader.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <cmath>
#include <filesystem>
#include <set>

using Catch::Matchers::WithinAbs;
using namespace mh::core;

namespace {

/// Two quads sharing an edge, but with SEPARATE UVs on the shared vertices --
/// a UV seam. This is the case the unweld exists for.
Mesh makeSeamedPair() {
    Mesh m("seam", 4);
    m.setCoords({{0, 0, 0}, {1, 0, 0}, {1, 0, 1}, {0, 0, 1}, {2, 0, 0}, {2, 0, 1}});
    // Vertices 1 and 2 get two different UVs each (indices 1,2 and 4,5).
    m.setUVs({{0, 0}, {1, 0}, {1, 1}, {0, 1}, {0, 0}, {0, 1}, {1, 0}, {1, 1}});
    m.addFaceGroup("a");
    m.addFaceGroup("b");
    REQUIRE(m.setFaces({0, 1, 2, 3, 1, 4, 5, 2}, {0, 1, 2, 3, 4, 6, 7, 5}, {0, 1}).has_value());
    m.buildAdjacency();
    m.calcNormals();
    return m;
}

}  // namespace

TEST_CASE("a UV seam splits shared vertices into separate render vertices", "[core][render]") {
    const Mesh m  = makeSeamedPair();
    const auto rm = RenderMesh::build(m);

    // 6 mesh vertices, but vertices 1 and 2 each carry two distinct UVs, so the
    // render mesh must have 8 -- this is the whole point of the unweld.
    CHECK(m.vertexCount() == 6);
    CHECK(rm.vertexCount() == 8);
}

TEST_CASE("every render vertex maps back to a valid mesh vertex and UV", "[core][render]") {
    const Mesh m  = makeSeamedPair();
    const auto rm = RenderMesh::build(m);

    for (size_t j = 0; j < rm.vertexCount(); ++j) {
        CHECK(rm.vmap()[j] < m.vertexCount());
        CHECK(rm.tmap()[j] < m.uvCount());
        // The gathered stream must equal the mesh data it points at.
        CHECK(rm.coord()[j] == m.coord()[rm.vmap()[j]]);
        CHECK(rm.texco()[j] == m.texco()[rm.tmap()[j]]);
    }
}

TEST_CASE("quads are triangulated into two triangles", "[core][render]") {
    const Mesh m  = makeSeamedPair();
    const auto rm = RenderMesh::build(m);
    // 2 quads -> 4 triangles -> 12 indices.
    CHECK(rm.indexCount() == 12);
    CHECK(rm.indexCount() % 3 == 0);
}

TEST_CASE("a triangle stored as a degenerate quad yields one triangle", "[core][render]") {
    // wavefront.py:105-106 stores tris as quads with corner 0 repeated. Emitting
    // the second triangle would produce a zero-area face.
    Mesh m("tri", 4);
    m.setCoords({{0, 0, 0}, {1, 0, 0}, {0, 0, 1}});
    m.setUVs({{0, 0}, {1, 0}, {0, 1}});
    m.addFaceGroup("g");
    REQUIRE(m.setFaces({0, 1, 2, 0}, {0, 1, 2, 0}, {0}).has_value());
    m.buildAdjacency();
    m.calcNormals();

    const auto rm = RenderMesh::build(m);
    CHECK(rm.indexCount() == 3);
}

TEST_CASE("indices are in range", "[core][render]") {
    const Mesh m  = makeSeamedPair();
    const auto rm = RenderMesh::build(m);
    for (const uint32_t i : rm.index()) {
        CHECK(i < rm.vertexCount());
    }
}

TEST_CASE("group ranges are contiguous and cover every index", "[core][render]") {
    const Mesh m  = makeSeamedPair();
    const auto rm = RenderMesh::build(m);

    REQUIRE(rm.groupRanges().size() == 2);

    uint32_t total = 0;
    for (const auto& g : rm.groupRanges()) {
        CHECK(g.first + g.count <= rm.indexCount());
        total += g.count;
    }
    CHECK(total == rm.indexCount());

    // Contiguous: each range starts where the previous one ended.
    CHECK(rm.groupRanges()[0].first == 0);
    CHECK(rm.groupRanges()[1].first == rm.groupRanges()[0].count);
}

TEST_CASE("refreshPositions re-gathers without rebuilding topology", "[core][render]") {
    Mesh m  = makeSeamedPair();
    auto rm = RenderMesh::build(m);

    const auto vmapBefore  = std::vector<uint32_t>(rm.vmap().begin(), rm.vmap().end());
    const auto indexBefore = std::vector<uint32_t>(rm.index().begin(), rm.index().end());

    // Morph the mesh, as a target application would.
    for (Vec3& v : m.mutableCoord())
        v.y += 2.0F;
    m.calcNormals();
    rm.refreshPositions(m);

    // Topology is unchanged...
    CHECK(std::equal(vmapBefore.begin(), vmapBefore.end(), rm.vmap().begin()));
    CHECK(std::equal(indexBefore.begin(), indexBefore.end(), rm.index().begin()));
    // ...but positions followed.
    for (size_t j = 0; j < rm.vertexCount(); ++j) {
        CHECK_THAT(rm.coord()[j].y,
                   WithinAbs(static_cast<double>(m.coord()[rm.vmap()[j]].y), 1e-6));
    }
}

TEST_CASE("the base mesh unwelds to the expected size", "[core][render][golden]") {
    const auto path = std::filesystem::path(MH_DATA_DIR) / "3dobjs" / "base.obj";
    if (!std::filesystem::exists(path)) SKIP("base.obj not present");

    auto mesh = loadObj(path);
    REQUIRE(mesh.has_value());
    mesh->calcVertexTangents();

    const auto rm = RenderMesh::build(*mesh);

    // Unwelding can only add vertices, never remove them, and cannot exceed one
    // render vertex per face corner.
    CHECK(rm.vertexCount() >= mesh->vertexCount());
    CHECK(rm.vertexCount() <= mesh->faceCount() * mesh->vertsPerPrimitive());

    // 18,486 quads -> 36,972 triangles -> 110,916 indices.
    CHECK(rm.indexCount() == mesh->faceCount() * 2 * 3);

    // Every attribute stream is present and the right length.
    CHECK(rm.coord().size() == rm.vertexCount());
    CHECK(rm.texco().size() == rm.vertexCount());
    CHECK(rm.vnorm().size() == rm.vertexCount());
    CHECK(rm.vtang().size() == rm.vertexCount());

    for (const uint32_t i : rm.index())
        REQUIRE(i < rm.vertexCount());

    // One draw range per face group, together covering the whole index buffer.
    REQUIRE(rm.groupRanges().size() == mesh->faceGroups().size());
    uint32_t total = 0;
    for (const auto& g : rm.groupRanges())
        total += g.count;
    CHECK(total == rm.indexCount());
}
