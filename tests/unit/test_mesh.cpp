// SPDX-License-Identifier: AGPL-3.0-or-later
#include "makehuman/core/Mesh.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <algorithm>
#include <cmath>
#include <vector>

using Catch::Matchers::WithinAbs;
using namespace mh::core;

namespace {

/// A unit quad in the XZ plane, wound so its normal points along +Y.
Mesh makeUnitQuad() {
    Mesh m("quad", 4);
    REQUIRE(m.setCoords({{0, 0, 0}, {1, 0, 0}, {1, 0, 1}, {0, 0, 1}}).has_value());
    REQUIRE(m.setUVs({{0, 0}, {1, 0}, {1, 1}, {0, 1}}).has_value());
    m.addFaceGroup("g");
    REQUIRE(m.setFaces({0, 1, 2, 3}, {0, 1, 2, 3}, {0}).has_value());
    m.buildAdjacency();
    return m;
}

}  // namespace

TEST_CASE("mesh reports basic counts", "[core][mesh]") {
    const Mesh m = makeUnitQuad();
    REQUIRE(m.vertexCount() == 4);
    REQUIRE(m.faceCount() == 1);
    REQUIRE(m.uvCount() == 4);
    REQUIRE(m.hasUV());
    REQUIRE(m.vertsPerPrimitive() == 4);
    REQUIRE(m.vertsPerFaceForExport() == 4);
}

TEST_CASE("triangles stored as degenerate quads are tagged for export as tris", "[core][mesh]") {
    // wavefront.py:105-106 repeats corner 0; module3d.py:634-639 detects it.
    Mesh m("tri", 4);
    REQUIRE(m.setCoords({{0, 0, 0}, {1, 0, 0}, {0, 0, 1}}).has_value());
    m.addFaceGroup("g");
    REQUIRE(m.setFaces({0, 1, 2, 0}, {}, {0}).has_value());
    REQUIRE(m.vertsPerFaceForExport() == 3);
}

TEST_CASE("face normals are unnormalised and area-weighted", "[core][mesh][normals]") {
    // module3d.py:339-341 leaves the cross product unnormalised on purpose:
    // its magnitude is what makes the vertex-normal sum area-weighted.
    Mesh m = makeUnitQuad();
    m.calcFaceNormals();
    REQUIRE(m.fnorm().size() == 1);

    const Vec3 n    = m.fnorm()[0];
    const float len = std::sqrt(dot(n, n));
    REQUIRE(len > 0.0F);
    REQUIRE_THAT(n.x / len, WithinAbs(0.0, 1e-6));
    REQUIRE_THAT(std::abs(n.y) / len, WithinAbs(1.0, 1e-6));
    REQUIRE_THAT(n.z / len, WithinAbs(0.0, 1e-6));
}

TEST_CASE("vertex normals are unit length", "[core][mesh][normals]") {
    Mesh m = makeUnitQuad();
    m.calcNormals();
    REQUIRE(m.vnorm().size() == 4);
    for (const Vec3& n : m.vnorm()) {
        REQUIRE_THAT(std::sqrt(dot(n, n)), WithinAbs(1.0, 1e-5));
    }
}

TEST_CASE("adjacency records the incident face for every vertex", "[core][mesh]") {
    const Mesh m = makeUnitQuad();
    REQUIRE(m.maxValence() >= 4);  // floored at 4, module3d.py:764-765
}

TEST_CASE("adjacency counts a vertex once per face in a degenerate quad", "[core][mesh]") {
    Mesh m("tri", 4);
    REQUIRE(m.setCoords({{0, 0, 0}, {1, 0, 0}, {0, 0, 1}}).has_value());
    m.addFaceGroup("g");
    REQUIRE(m.setFaces({0, 1, 2, 0}, {}, {0}).has_value());
    m.buildAdjacency();
    m.calcNormals();
    // Vertex 0 appears twice in the quad but belongs to one face; its normal
    // must not be double-counted into a non-unit vector.
    for (const Vec3& n : m.vnorm()) {
        REQUIRE_THAT(std::sqrt(dot(n, n)), WithinAbs(1.0, 1e-5));
    }
}

TEST_CASE("resetToOriginal restores the morph base", "[core][mesh]") {
    // The morph base is captured at setCoords, matching module3d.py:532, and
    // restored by algos3d.py:493-494 before every full stack rebuild.
    Mesh m              = makeUnitQuad();
    m.mutableCoord()[0] = Vec3{9, 9, 9};
    REQUIRE(m.coord()[0] == Vec3{9, 9, 9});
    m.resetToOriginal();
    REQUIRE(m.coord()[0] == Vec3{0, 0, 0});
}

TEST_CASE("changeCoords deforms without moving the morph base", "[core][mesh]") {
    // module3d.py:591. Deformation -- skinning, proxy fitting -- goes through
    // this and NOT setCoords, which would make the deformed mesh the base that
    // resetToOriginal restores; see Mesh.h and tests/regression/test_repose.cpp.
    Mesh m = makeUnitQuad();
    REQUIRE(m.changeCoords({{0, 9, 0}, {1, 9, 0}, {1, 9, 1}, {0, 9, 1}}).has_value());
    CHECK(m.coord()[0] == Vec3{0, 9, 0});
    CHECK(m.origCoord()[0] == Vec3{0, 0, 0});
    m.resetToOriginal();
    CHECK(m.coord()[0] == Vec3{0, 0, 0});

    // Adding or removing vertices is setCoords' job, and only setCoords
    // re-validates the face indices that would strand.
    const auto shorter = m.changeCoords({{0, 0, 0}, {1, 0, 0}});
    REQUIRE_FALSE(shorter.has_value());
    CHECK(shorter.error() == MeshError::VertexCountMismatch);
    CHECK(m.vertexCount() == 4);
}

TEST_CASE("height is the Y extent in decimetres scaled to centimetres", "[core][mesh][units]") {
    // human.py:694-699 -- internal units are decimetres.
    Mesh m("bar", 4);
    REQUIRE(m.setCoords({{0, 0, 0}, {1, 17.5F, 0}, {1, 17.5F, 1}, {0, 0, 1}}).has_value());
    m.addFaceGroup("g");
    REQUIRE(m.setFaces({0, 1, 2, 3}, {}, {0}).has_value());
    REQUIRE_THAT(m.heightCm(), WithinAbs(175.0, 1e-4));
}

TEST_CASE("bounding box of an empty mesh is absent", "[core][mesh]") {
    const Mesh m;
    REQUIRE_FALSE(m.boundingBox().has_value());
}

TEST_CASE("face groups deduplicate by name", "[core][mesh]") {
    Mesh m("m", 4);
    const uint16_t a = m.addFaceGroup("head");
    const uint16_t b = m.addFaceGroup("head");
    REQUIRE(a == b);
    REQUIRE(m.faceGroups().size() == 1);
    REQUIRE(m.findFaceGroup("head").has_value());
    REQUIRE_FALSE(m.findFaceGroup("nose").has_value());
}

// ---------------------------------------------------------------------------
// compactToFaces: a copy holding only the faces a mask keeps.
//
// Lifted out of `Subdivider.cpp`, where it was a file-local helper called by
// `Subdivider::build(parent, faceMask)` -- its byte-parity coverage is
// `tests/golden/test_subdiv_masked_parity.cpp`, which still passes and is what
// makes the move safe. It is on `Mesh` now because DECIMATION needs the same
// operation: a face mask cannot survive an edge collapse, so it has to be
// baked into the geometry before the collapse rather than carried past it.
// ---------------------------------------------------------------------------

namespace {

/// A 2x2 grid of quads: 9 vertices, 4 faces, one UV per vertex.
Mesh makeGrid() {
    Mesh m("grid", 4);
    REQUIRE(m.setCoords({{0, 0, 0},
                         {1, 0, 0},
                         {2, 0, 0},
                         {0, 0, 1},
                         {1, 0, 1},
                         {2, 0, 1},
                         {0, 0, 2},
                         {1, 0, 2},
                         {2, 0, 2}})
                .has_value());
    // The UV indices are deliberately NOT the vertex indices -- UV j belongs to
    // vertex 8-j. The base mesh has 21,334 UVs for 19,158 vertices, so the two
    // spaces never coincide there either, and a fixture where they DO hides
    // every confusion between them. This one did: a mutation replacing the UV
    // renumbering with the vertex renumbering passed until the fixture changed.
    //
    // Each UV still carries the position of the vertex it pairs with, so "the
    // UV beside this corner matches the corner's position" stays a checkable
    // claim after both index spaces are renumbered independently.
    std::vector<Vec2> uvs(m.vertexCount());
    for (size_t v = 0; v < m.vertexCount(); ++v)
        uvs[m.vertexCount() - 1 - v] = {m.coord()[v].x / 2.0F, m.coord()[v].z / 2.0F};
    REQUIRE(m.setUVs(std::move(uvs)).has_value());
    m.addFaceGroup("lower");
    m.addFaceGroup("upper");
    const std::vector<uint32_t> fv{0, 1, 4, 3, 1, 2, 5, 4, 3, 4, 7, 6, 4, 5, 8, 7};
    std::vector<uint32_t> fu;
    for (const uint32_t v : fv)
        fu.push_back(8U - v);
    REQUIRE(m.setFaces(fv, fu, {0, 0, 1, 1}).has_value());
    return m;
}

}  // namespace

TEST_CASE("compactToFaces keeps only the masked faces and renumbers", "[core][mesh][compact]") {
    const Mesh m = makeGrid();
    // The two lower faces only. They use vertices 0,1,2,3,4,5 -- six of nine.
    const auto out = m.compactToFaces(std::vector<uint8_t>{1, 1, 0, 0});
    REQUIRE(out.has_value());

    CHECK(out->faceCount() == 2);
    CHECK(out->vertexCount() == 6);
    CHECK(out->uvCount() == 6);
    // Ascending order preserved, which is what the reference's np.argwhere
    // gives and what the subdivision output order depends on.
    CHECK(out->coord()[0].x == 0.0F);
    CHECK(out->coord()[5].x == 2.0F);
    CHECK(out->coord()[5].z == 1.0F);
    // Renumbered, not merely filtered: vertex 4 of the original is index 4
    // here by luck, but 5 became 5 and 6..8 are gone.
    for (const uint32_t c : out->fvert())
        CHECK(c < 6);
    for (const uint32_t c : out->fuvs())
        CHECK(c < 6);
    // The corner-to-UV pairing must survive the double renumbering. It is the
    // one thing two independent index spaces make easy to get wrong.
    for (size_t c = 0; c < out->fvert().size(); ++c) {
        const Vec3& p  = out->coord()[out->fvert()[c]];
        const Vec2& uv = out->texco()[out->fuvs()[c]];
        CHECK(uv.x == p.x / 2.0F);
        CHECK(uv.y == p.z / 2.0F);
    }
}

TEST_CASE("compactToFaces carries the face groups", "[core][mesh][compact]") {
    const Mesh m = makeGrid();
    // One face from each group, so a per-face group index that was dropped or
    // shifted shows up.
    const auto out = m.compactToFaces(std::vector<uint8_t>{1, 0, 1, 0});
    REQUIRE(out.has_value());
    REQUIRE(out->faceCount() == 2);
    CHECK(out->group()[0] == 0);
    CHECK(out->group()[1] == 1);
    // Every group is carried, named, even ones that lost all their faces --
    // the ids are what `staticFaceMask` and the .mhm both key on.
    REQUIRE(out->faceGroups().size() == 2);
    CHECK(out->faceGroups()[0].name == "lower");
    CHECK(out->faceGroups()[1].name == "upper");
}

TEST_CASE("compactToFaces refuses a mask of the wrong length", "[core][mesh][compact]") {
    const Mesh m = makeGrid();
    // Silently treating a short mask as "keep the rest" would export whatever
    // geometry the caller forgot about.
    CHECK_FALSE(m.compactToFaces(std::vector<uint8_t>{1, 1}).has_value());
    CHECK(m.compactToFaces(std::vector<uint8_t>{1, 1}).error() == MeshError::MaskSizeMismatch);
    CHECK_FALSE(m.compactToFaces({}).has_value());
    CHECK_FALSE(m.compactToFaces(std::vector<uint8_t>{1, 1, 1, 1, 1}).has_value());
}

TEST_CASE("compactToFaces keeping everything is a faithful copy", "[core][mesh][compact]") {
    const Mesh m   = makeGrid();
    const auto out = m.compactToFaces(std::vector<uint8_t>{1, 1, 1, 1});
    REQUIRE(out.has_value());
    CHECK(out->vertexCount() == m.vertexCount());
    CHECK(out->uvCount() == m.uvCount());
    CHECK(out->faceCount() == m.faceCount());
    CHECK(std::ranges::equal(out->fvert(), m.fvert()));
    CHECK(std::ranges::equal(out->fuvs(), m.fuvs()));
    CHECK(std::ranges::equal(out->group(), m.group()));
}

TEST_CASE("compactToFaces on a UV-less mesh keeps it UV-less", "[core][mesh][compact]") {
    Mesh m("plain", 4);
    REQUIRE(m.setCoords({{0, 0, 0}, {1, 0, 0}, {1, 0, 1}, {0, 0, 1}}).has_value());
    m.addFaceGroup("g");
    REQUIRE(m.setFaces({0, 1, 2, 3}, {}, {0}).has_value());
    const auto out = m.compactToFaces(std::vector<uint8_t>{1});
    REQUIRE(out.has_value());
    CHECK_FALSE(out->hasUV());
    CHECK(out->uvCount() == 0);
    CHECK(out->faceCount() == 1);
}

TEST_CASE("compactToFaces names the vertex each survivor came from", "[core][mesh][compact]") {
    // The mapping exists so a caller can carry PER-VERTEX data across the
    // compaction -- skin weights, in the one caller that needs it. Without it
    // a decimated export has no honest way to weight anything and must refuse
    // the rig, which is what it did.
    const Mesh m = makeGrid();
    std::vector<uint32_t> source;
    const auto out = m.compactToFaces(std::vector<uint8_t>{1, 1, 0, 0}, &source);
    REQUIRE(out.has_value());

    REQUIRE(source.size() == out->vertexCount());
    // Ascending, and a plain selection: that ordering is what the whole
    // compaction is built around, and the caller composes two of these.
    CHECK(std::ranges::is_sorted(source));
    CHECK(source == std::vector<uint32_t>{0, 1, 2, 3, 4, 5});

    // Positions agree, unlike after a decimation: nothing MOVES here.
    for (size_t v = 0; v < out->vertexCount(); ++v) {
        CHECK(out->coord()[v].x == m.coord()[source[v]].x);
        CHECK(out->coord()[v].z == m.coord()[source[v]].z);
    }
}

TEST_CASE("compactToFaces provenance skips the dropped vertices", "[core][mesh][compact]") {
    // The two UPPER faces use vertices 3..8, so the mapping must start at 3. A
    // mapping that was merely 0..N would satisfy every length check and be
    // wrong by three.
    const Mesh m = makeGrid();
    std::vector<uint32_t> source;
    const auto out = m.compactToFaces(std::vector<uint8_t>{0, 0, 1, 1}, &source);
    REQUIRE(out.has_value());
    CHECK(source == std::vector<uint32_t>{3, 4, 5, 6, 7, 8});
}
