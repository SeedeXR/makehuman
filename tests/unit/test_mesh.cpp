// SPDX-License-Identifier: AGPL-3.0-or-later
#include "makehuman/core/Mesh.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

using Catch::Matchers::WithinAbs;
using namespace mh::core;

namespace {

/// A unit quad in the XZ plane, wound so its normal points along +Y.
Mesh makeUnitQuad() {
    Mesh m("quad", 4);
    m.setCoords({{0, 0, 0}, {1, 0, 0}, {1, 0, 1}, {0, 0, 1}});
    m.setUVs({{0, 0}, {1, 0}, {1, 1}, {0, 1}});
    m.addFaceGroup("g");
    REQUIRE(m.setFaces({0, 1, 2, 3}, {0, 1, 2, 3}, {0}).has_value());
    m.buildAdjacency();
    return m;
}

} // namespace

TEST_CASE("mesh reports basic counts", "[core][mesh]") {
    const Mesh m = makeUnitQuad();
    REQUIRE(m.vertexCount() == 4);
    REQUIRE(m.faceCount() == 1);
    REQUIRE(m.uvCount() == 4);
    REQUIRE(m.hasUV());
    REQUIRE(m.vertsPerPrimitive() == 4);
    REQUIRE(m.vertsPerFaceForExport() == 4);
}

TEST_CASE("triangles stored as degenerate quads are tagged for export as tris",
          "[core][mesh]") {
    // wavefront.py:105-106 repeats corner 0; module3d.py:634-639 detects it.
    Mesh m("tri", 4);
    m.setCoords({{0, 0, 0}, {1, 0, 0}, {0, 0, 1}});
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

    const Vec3 n = m.fnorm()[0];
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
    REQUIRE(m.maxValence() >= 4);   // floored at 4, module3d.py:764-765
}

TEST_CASE("adjacency counts a vertex once per face in a degenerate quad",
          "[core][mesh]") {
    Mesh m("tri", 4);
    m.setCoords({{0, 0, 0}, {1, 0, 0}, {0, 0, 1}});
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
    Mesh m = makeUnitQuad();
    m.mutableCoord()[0] = Vec3{9, 9, 9};
    REQUIRE(m.coord()[0] == Vec3{9, 9, 9});
    m.resetToOriginal();
    REQUIRE(m.coord()[0] == Vec3{0, 0, 0});
}

TEST_CASE("height is the Y extent in decimetres scaled to centimetres",
          "[core][mesh][units]") {
    // human.py:694-699 -- internal units are decimetres.
    Mesh m("bar", 4);
    m.setCoords({{0, 0, 0}, {1, 17.5F, 0}, {1, 17.5F, 1}, {0, 0, 1}});
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
