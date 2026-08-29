// SPDX-License-Identifier: AGPL-3.0-or-later
//
// The reference's tangents are provably wrong in three independent ways
// (see Mesh::calcVertexTangents' doc comment and memory/project_context.md
// section 8), so these assert MATHEMATICAL PROPERTIES rather than equality
// with the reference. memory/test.md section 3.4 requires exactly this.

#include "makehuman/core/Mesh.h"
#include "makehuman/core/ObjReader.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <cmath>
#include <filesystem>

using Catch::Matchers::WithinAbs;
using namespace mh::core;

namespace {

/// A unit quad in the XZ plane with a matching unit UV square. Its tangent
/// (dU direction) must run along +X and its bitangent along +Z.
Mesh makeUvQuad() {
    Mesh m("quad", 4);
    REQUIRE(m.setCoords({{0, 0, 0}, {1, 0, 0}, {1, 0, 1}, {0, 0, 1}}).has_value());
    REQUIRE(m.setUVs({{0, 0}, {1, 0}, {1, 1}, {0, 1}}).has_value());
    m.addFaceGroup("g");
    REQUIRE(m.setFaces({0, 1, 2, 3}, {0, 1, 2, 3}, {0}).has_value());
    m.buildAdjacency();
    m.calcNormals();
    return m;
}

}  // namespace

TEST_CASE("tangents are empty without UVs", "[core][tangent]") {
    Mesh m("t", 4);
    REQUIRE(m.setCoords({{0, 0, 0}, {1, 0, 0}, {0, 0, 1}}).has_value());
    m.addFaceGroup("g");
    REQUIRE(m.setFaces({0, 1, 2, 0}, {}, {0}).has_value());
    m.calcNormals();
    m.calcVertexTangents();
    CHECK(m.vtang().empty());  // module3d.py:375-376
}

TEST_CASE("tangent points along increasing U", "[core][tangent]") {
    Mesh m = makeUvQuad();
    m.calcVertexTangents();
    REQUIRE(m.vtang().size() == 4);

    // U increases with +X in this quad, so the tangent must too.
    for (const Vec4& t : m.vtang()) {
        CHECK_THAT(t.x, WithinAbs(1.0, 1e-5));
        CHECK_THAT(t.y, WithinAbs(0.0, 1e-5));
        CHECK_THAT(t.z, WithinAbs(0.0, 1e-5));
    }
}

TEST_CASE("tangents are unit length", "[core][tangent]") {
    Mesh m = makeUvQuad();
    m.calcVertexTangents();
    for (const Vec4& t : m.vtang()) {
        const Vec3 v{t.x, t.y, t.z};
        CHECK_THAT(std::sqrt(dot(v, v)), WithinAbs(1.0, 1e-5));
    }
}

TEST_CASE("tangents are orthogonal to their vertex normal", "[core][tangent]") {
    // This is the Gram-Schmidt step's whole purpose, and the property the
    // reference's chained-assignment bug destroys.
    Mesh m = makeUvQuad();
    m.calcVertexTangents();
    for (size_t i = 0; i < m.vtang().size(); ++i) {
        const Vec4& t = m.vtang()[i];
        const Vec3 tv{t.x, t.y, t.z};
        CHECK_THAT(dot(tv, m.vnorm()[i]), WithinAbs(0.0, 1e-5));
    }
}

TEST_CASE("handedness is +/-1", "[core][tangent]") {
    Mesh m = makeUvQuad();
    m.calcVertexTangents();
    for (const Vec4& t : m.vtang()) {
        CHECK((t.w == 1.0F || t.w == -1.0F));
    }
}

TEST_CASE("mirrored UVs flip handedness", "[core][tangent]") {
    // Handedness exists precisely to encode a mirrored UV shell. Flipping V
    // reverses the bitangent, so w must flip sign.
    Mesh a = makeUvQuad();
    a.calcVertexTangents();

    Mesh b("quad", 4);
    REQUIRE(b.setCoords({{0, 0, 0}, {1, 0, 0}, {1, 0, 1}, {0, 0, 1}}).has_value());
    REQUIRE(b.setUVs({{0, 1}, {1, 1}, {1, 0}, {0, 0}}).has_value());  // V mirrored
    b.addFaceGroup("g");
    REQUIRE(b.setFaces({0, 1, 2, 3}, {0, 1, 2, 3}, {0}).has_value());
    b.buildAdjacency();
    b.calcNormals();
    b.calcVertexTangents();

    REQUIRE(a.vtang().size() == b.vtang().size());
    CHECK(a.vtang()[0].w != b.vtang()[0].w);
}

TEST_CASE("a degenerate UV triangle does not poison its vertices", "[core][tangent]") {
    // All three corners share one UV, so the face has no tangent basis. The
    // reference nudges the zero deltas to 1e-7 and invents a direction; we skip
    // the face and fall back to something orthonormal.
    Mesh m("degen", 4);
    REQUIRE(m.setCoords({{0, 0, 0}, {1, 0, 0}, {1, 0, 1}, {0, 0, 1}}).has_value());
    REQUIRE(m.setUVs({{0.5F, 0.5F}}).has_value());
    m.addFaceGroup("g");
    REQUIRE(m.setFaces({0, 1, 2, 3}, {0, 0, 0, 0}, {0}).has_value());
    m.buildAdjacency();
    m.calcNormals();
    m.calcVertexTangents();

    REQUIRE(m.vtang().size() == 4);
    for (size_t i = 0; i < m.vtang().size(); ++i) {
        const Vec4& t = m.vtang()[i];
        const Vec3 tv{t.x, t.y, t.z};
        const float len = std::sqrt(dot(tv, tv));
        CHECK(std::isfinite(len));
        CHECK_THAT(len, WithinAbs(1.0, 1e-5));                    // never NaN
        CHECK_THAT(dot(tv, m.vnorm()[i]), WithinAbs(0.0, 1e-5));  // still orthogonal
    }
}

TEST_CASE("every base mesh tangent is finite, unit, and orthogonal", "[core][tangent][golden]") {
    const auto path = std::filesystem::path(MH_DATA_DIR) / "3dobjs" / "base.obj";
    if (!std::filesystem::exists(path)) SKIP("base.obj not present");

    auto mesh = loadObj(path);
    REQUIRE(mesh.has_value());
    mesh->calcVertexTangents();
    REQUIRE(mesh->vtang().size() == mesh->vertexCount());

    size_t badLen   = 0;
    size_t badOrtho = 0;
    size_t badW     = 0;
    for (size_t i = 0; i < mesh->vtang().size(); ++i) {
        const Vec4& t = mesh->vtang()[i];
        const Vec3 tv{t.x, t.y, t.z};
        const float len = std::sqrt(dot(tv, tv));
        if (!std::isfinite(len) || std::abs(len - 1.0F) > 1e-4F) ++badLen;
        if (std::abs(dot(tv, mesh->vnorm()[i])) > 1e-3F) ++badOrtho;
        if (t.w != 1.0F && t.w != -1.0F) ++badW;
    }
    CHECK(badLen == 0);
    CHECK(badOrtho == 0);
    CHECK(badW == 0);
}
