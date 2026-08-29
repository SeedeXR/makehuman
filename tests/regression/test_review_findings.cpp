// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Regression tests for the code-review findings on commit 61f48893.
// Each test FAILS on the pre-fix code; several were heap-overflows under ASan.

#include "makehuman/core/Mesh.h"
#include "makehuman/core/ObjReader.h"
#include "makehuman/core/RenderMesh.h"

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <set>

#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

using namespace mh::core;

namespace {

class TempObj {
public:
    explicit TempObj(std::string_view contents) {
        static int counter = 0;
        path_              = std::filesystem::temp_directory_path() /
                ("mh_reg_" + std::to_string(++counter) + ".obj");
        std::ofstream out(path_);
        out << contents;
    }

    ~TempObj() {
        std::error_code ec;
        std::filesystem::remove(path_, ec);
    }

    TempObj(const TempObj&)            = delete;
    TempObj& operator=(const TempObj&) = delete;

    [[nodiscard]] const std::filesystem::path& path() const { return path_; }

private:
    std::filesystem::path path_;
};

}  // namespace

// --- Finding 1 -------------------------------------------------------------
// maxValence_ was uint8_t. A vertex with >=256 incident faces wrapped it to 0,
// so the adjacency stride became 0, nothing was recorded, and EVERY vertex
// normal silently fell back to the zero-guard {0,1,0}. No error, no diagnostic.
TEST_CASE("valence above 255 does not wrap and destroy every normal", "[regression][core][mesh]") {
    constexpr uint32_t kFan = 300;  // > 255 incident faces on the apex
    constexpr float kPi     = 3.14159265358979323846F;

    // A cone, not a flat fan: with a planar fan every true normal really is
    // {0,1,0}, which is indistinguishable from the zero-guard fallback.
    std::vector<Vec3> coords;
    coords.push_back(Vec3{0.0F, 1.0F, 0.0F});  // apex
    for (uint32_t i = 0; i < kFan; ++i) {
        const float a = 2.0F * kPi * static_cast<float>(i) / static_cast<float>(kFan);
        coords.push_back(Vec3{std::cos(a), 0.0F, std::sin(a)});
    }

    std::vector<uint32_t> fv;
    std::vector<uint16_t> fg;
    for (uint32_t i = 0; i < kFan; ++i) {
        const uint32_t a = i + 1U;
        const uint32_t b = (i + 1U) % kFan + 1U;
        fv.insert(fv.end(), {0U, a, b, 0U});  // degenerate quad
        fg.push_back(0);
    }

    Mesh m("cone", 4);
    REQUIRE(m.setCoords(std::move(coords)).has_value());
    m.addFaceGroup("g");
    REQUIRE(m.setFaces(std::move(fv), {}, std::move(fg)).has_value());
    m.buildAdjacency();

    // Before the fix this wrapped to 0, the adjacency stride became 0, and
    // nothing was recorded.
    REQUIRE(m.maxValence() >= kFan);

    m.calcNormals();
    REQUIRE(m.vnorm().size() == kFan + 1U);

    // Every normal must be finite and unit length...
    for (const Vec3& n : m.vnorm()) {
        const float len = std::sqrt(dot(n, n));
        REQUIRE(std::isfinite(len));
        REQUIRE(std::abs(len - 1.0F) < 1e-4F);
    }

    // ...the apex, fed by all 300 faces, must come out parallel to Y. The sign
    // depends on this test's arbitrary winding, which is not what is under test.
    CHECK(std::abs(m.vnorm()[0].y) > 0.9F);

    // ...and the rim normals must genuinely differ from one another, which they
    // cannot if the adjacency was empty and every one fell back to {0,1,0}.
    const Vec3 r1 = m.vnorm()[1];
    const Vec3 r2 = m.vnorm()[1U + kFan / 2U];
    CHECK(dot(r1, r2) < 0.5F);
}

// --- Finding 2 -------------------------------------------------------------
// setFaces left the old vface_/nfaces_ in place. Because coord_ was unchanged,
// the staleness guard in calcVertexNormals still held, so it indexed fnorm_
// (resized to the NEW, smaller face count) through OLD face indices.
// ASan: container-overflow in Vec3::operator+=.
TEST_CASE("replacing the face set invalidates stale adjacency", "[regression][core][mesh]") {
    Mesh m("m", 4);
    REQUIRE(m.setCoords({{0, 0, 0}, {1, 0, 0}, {1, 0, 1}, {0, 0, 1}}).has_value());
    m.addFaceGroup("g");

    REQUIRE(m.setFaces({0, 1, 2, 3, 0, 1, 2, 3, 0, 1, 2, 3}, {}, {0, 0, 0}).has_value());
    m.buildAdjacency();
    REQUIRE(m.faceCount() == 3);

    // Shrink to one face. The old adjacency references faces 1 and 2.
    REQUIRE(m.setFaces({0, 1, 2, 3}, {}, {0}).has_value());
    REQUIRE(m.faceCount() == 1);

    m.calcNormals();  // read out of bounds before the fix
    CHECK(m.vnorm().size() == 4);
}

// --- Finding 3 / 10 --------------------------------------------------------
// calcFaceNormals indexed coord_[fvert_[...]] with no bounds check while
// buildAdjacency guarded the same indices. setFaces is public and validated
// nothing, so an out-of-range index was a heap-buffer-overflow.
TEST_CASE("setFaces rejects an out-of-range vertex index", "[regression][core][mesh]") {
    Mesh m("m", 4);
    REQUIRE(m.setCoords({{0, 0, 0}, {1, 0, 0}, {0, 0, 1}}).has_value());
    m.addFaceGroup("g");

    const auto r = m.setFaces({0, 1, 99, 2}, {}, {0});
    REQUIRE_FALSE(r.has_value());
    CHECK(r.error() == MeshError::VertexIndexOutOfRange);
    CHECK(m.faceCount() == 0);  // mesh left unchanged
}

TEST_CASE("setFaces rejects an out-of-range UV index", "[regression][core][mesh]") {
    Mesh m("m", 4);
    REQUIRE(m.setCoords({{0, 0, 0}, {1, 0, 0}, {0, 0, 1}, {1, 0, 1}}).has_value());
    REQUIRE(m.setUVs({{0, 0}, {1, 0}}).has_value());
    m.addFaceGroup("g");

    const auto r = m.setFaces({0, 1, 2, 3}, {0, 1, 42, 0}, {0});
    REQUIRE_FALSE(r.has_value());
    CHECK(r.error() == MeshError::UvIndexOutOfRange);
}

TEST_CASE("setFaces rejects a UV array not parallel to the vertex array",
          "[regression][core][mesh]") {
    Mesh m("m", 4);
    REQUIRE(m.setCoords({{0, 0, 0}, {1, 0, 0}, {0, 0, 1}, {1, 0, 1}}).has_value());
    REQUIRE(m.setUVs({{0, 0}, {1, 0}, {1, 1}, {0, 1}}).has_value());
    m.addFaceGroup("g");

    const auto r = m.setFaces({0, 1, 2, 3}, {0, 1}, {0});
    REQUIRE_FALSE(r.has_value());
    CHECK(r.error() == MeshError::UvArraySizeMismatch);
}

TEST_CASE("setFaces rejects a face array that is not a whole number of primitives",
          "[regression][core][mesh]") {
    Mesh m("m", 4);
    REQUIRE(m.setCoords({{0, 0, 0}, {1, 0, 0}, {0, 0, 1}}).has_value());
    m.addFaceGroup("g");

    const auto r = m.setFaces({0, 1, 2}, {}, {0});  // 3 entries, stride 4
    REQUIRE_FALSE(r.has_value());
    CHECK(r.error() == MeshError::FaceArraySizeMismatch);
}

// --- Finding 9 -------------------------------------------------------------
// hasUV_ was stored and set unconditionally by setUVs, so calling setUVs AFTER
// setFaces({}) left hasUV() true with an empty fuvs_ — a consumer indexing
// fuvs() in parallel with fvert() got nothing.
TEST_CASE("hasUV stays false when faces carry no UV indices", "[regression][core][mesh]") {
    Mesh m("m", 4);
    REQUIRE(m.setCoords({{0, 0, 0}, {1, 0, 0}, {0, 0, 1}, {1, 0, 1}}).has_value());
    m.addFaceGroup("g");
    REQUIRE(m.setFaces({0, 1, 2, 3}, {}, {0}).has_value());
    // Setting UVs after faces is fine when no UV index was recorded: fuvs_ is
    // empty, so nothing can be stranded.
    REQUIRE(m.setUVs({{0, 0}, {1, 0}, {1, 1}, {0, 1}}).has_value());

    CHECK_FALSE(m.hasUV());
    CHECK(m.fuvs().empty());
}

// --- Finding 4 -------------------------------------------------------------
// A malformed v/vt line was silently dropped, shifting every later index so the
// file loaded "successfully" as a completely different mesh.
TEST_CASE("a malformed v line is an error, not a silent index shift", "[regression][core][obj]") {
    const TempObj f("g t\nv 0 0 0\nv 1 0\nv 1 0 0\nv 0 0 1\nf 1 2 3\n");
    const auto m = loadObj(f.path());
    REQUIRE_FALSE(m.has_value());
    CHECK(m.error().kind == ObjErrorKind::MalformedVertex);
    CHECK(m.error().line == 3);
}

TEST_CASE("a non-numeric vertex component is an error", "[regression][core][obj]") {
    const TempObj f("g t\nv zero 0 0\nv 1 0 0\nv 0 0 1\nf 1 2 3\n");
    const auto m = loadObj(f.path());
    REQUIRE_FALSE(m.has_value());
    CHECK(m.error().kind == ObjErrorKind::MalformedVertex);
}

TEST_CASE("a malformed vt line is an error", "[regression][core][obj]") {
    const TempObj f("g t\nv 0 0 0\nv 1 0 0\nv 0 0 1\nvt 0\nf 1 2 3\n");
    const auto m = loadObj(f.path());
    REQUIRE_FALSE(m.has_value());
    CHECK(m.error().kind == ObjErrorKind::MalformedVertex);
}

// --- Finding 5 -------------------------------------------------------------
// `o` was treated as `g`, creating a spurious empty face group and never
// setting the mesh name. wavefront.py:128-129 sets the name and creates no
// group. data/3dobjs/axis.obj opens with `o Axis` before three `g` statements.
TEST_CASE("an o statement sets the name and creates no face group", "[regression][core][obj]") {
    const TempObj f("o Axis\ng red\nv 0 0 0\nv 1 0 0\nv 0 0 1\nf 1 2 3\n");
    const auto m = loadObj(f.path());
    REQUIRE(m.has_value());
    CHECK(m->name() == "Axis");
    REQUIRE(m->faceGroups().size() == 1);
    CHECK(m->faceGroups()[0].name == "red");
}

TEST_CASE("the shipped axis.obj loads with the reference's group count",
          "[regression][core][obj][golden]") {
    const auto path = std::filesystem::path(MH_DATA_DIR) / "3dobjs" / "axis.obj";
    if (!std::filesystem::exists(path)) SKIP("axis.obj not present");

    const auto m = loadObj(path);
    REQUIRE(m.has_value());
    // 'o Axis' plus exactly three 'g' statements -> 3 groups, name "Axis".
    CHECK(m->faceGroups().size() == 3);
    CHECK(m->name() == "Axis");
}

// --- Finding 6 -------------------------------------------------------------
// An f statement with fewer than 3 corners was silently ignored, while >4 was a
// hard error — asymmetric, and it hid malformed input.
TEST_CASE("a face with fewer than three corners is an error", "[regression][core][obj]") {
    const TempObj f("g t\nv 0 0 0\nv 1 0 0\nv 0 0 1\nf 1 2\nf 1 2 3\n");
    const auto m = loadObj(f.path());
    REQUIRE_FALSE(m.has_value());
    CHECK(m.error().kind == ObjErrorKind::DegenerateFace);
}

// --- Finding 7 -------------------------------------------------------------
// parseCorner checked only from_chars' error code, not that it consumed the
// whole token, so "1x" parsed as vertex 1.
TEST_CASE("a face corner with trailing garbage is an error", "[regression][core][obj]") {
    const TempObj f("g t\nv 0 0 0\nv 1 0 0\nv 0 0 1\nf 1x 2 3\n");
    const auto m = loadObj(f.path());
    REQUIRE_FALSE(m.has_value());
    CHECK(m.error().kind == ObjErrorKind::BadIndex);
}

TEST_CASE("a UV index with trailing garbage is an error", "[regression][core][obj]") {
    const TempObj f("g t\nv 0 0 0\nv 1 0 0\nv 0 0 1\nvt 0 0\nf 1/1q 2/1 3/1\n");
    const auto m = loadObj(f.path());
    REQUIRE_FALSE(m.has_value());
    CHECK(m.error().kind == ObjErrorKind::BadIndex);
}

// ===========================================================================
// Review round 2 — findings on commit f88e2b4c (tangents + RenderMesh).
// ===========================================================================

// --- Finding 1 (HIGH) ------------------------------------------------------
// calcVertexTangents built each face's basis from corners 0,1,2 only and then
// broadcast it to all four corners via vface_. But a quad is DRAWN as
// (0,1,2)+(0,2,3), so corner 3 got a basis from a triangle it is not part of
// and triangle (0,2,3) contributed nothing. On base.obj this reached 179 deg
// of error. Repro: pinch the UVs at corner 1 so triangle (0,1,2) is
// UV-degenerate while (0,2,3) is perfectly well defined.
TEST_CASE("a quad's second triangle contributes to tangents", "[regression][core][tangent]") {
    Mesh m("pinch", 4);
    REQUIRE(m.setCoords({{0, 0, 0}, {1, 0, 0}, {1, 0, 1}, {0, 0, 1}}).has_value());
    REQUIRE(m.setUVs({{0, 0}, {0, 0}, {1, 1}, {1, 0}})
                .has_value());  // uv1 == uv0 -> (0,1,2) degenerate
    m.addFaceGroup("g");
    REQUIRE(m.setFaces({0, 1, 2, 3}, {0, 1, 2, 3}, {0}).has_value());
    m.buildAdjacency();
    m.calcNormals();
    m.calcVertexTangents();

    REQUIRE(m.vtang().size() == 4);
    // Triangle (0,2,3) gives dP/dU along +Z. Before the fix every vertex got
    // the arbitrary (1,0,0) fallback because the only triangle considered was
    // the degenerate one.
    const Vec4& t = m.vtang()[0];
    INFO("tangent = " << t.x << ", " << t.y << ", " << t.z);
    CHECK(std::abs(t.z) > 0.5F);
}

// --- Finding 2 (HIGH) ------------------------------------------------------
// Mesh.h promised calcVertexTangents would compute normals if missing, but
// setCoords zero-filled vnorm_, so the size guard was always satisfied and the
// tangents were orthogonalised against the ZERO vector -- making handedness
// always +1.
TEST_CASE("tangents are correct without an explicit calcNormals call",
          "[regression][core][tangent]") {
    Mesh m("mirror", 4);
    REQUIRE(m.setCoords({{0, 0, 0}, {1, 0, 0}, {1, 0, 1}, {0, 0, 1}}).has_value());
    REQUIRE(m.setUVs({{0, 1}, {1, 1}, {1, 0}, {0, 0}}).has_value());  // mirrored V -> handedness -1
    m.addFaceGroup("g");
    REQUIRE(m.setFaces({0, 1, 2, 3}, {0, 1, 2, 3}, {0}).has_value());

    m.calcVertexTangents();  // deliberately NO calcNormals() first

    REQUIRE(m.vtang().size() == 4);
    REQUIRE(m.vnorm().size() == 4);
    const Vec3& n = m.vnorm()[0];
    CHECK(std::sqrt(dot(n, n)) > 0.5F);  // a real normal, not the zero vector
    CHECK(m.vtang()[0].w == -1.0F);      // mirrored UVs must flip handedness
}

// --- Finding 3 (HIGH) ------------------------------------------------------
// setFaces validated fuvs against texco at that moment; setUVs then replaced
// texco with no revalidation, so calcVertexTangents read out of bounds.
TEST_CASE("shrinking the UV array after setFaces is rejected", "[regression][core][mesh]") {
    Mesh m("m", 4);
    REQUIRE(m.setCoords({{0, 0, 0}, {1, 0, 0}, {1, 0, 1}, {0, 0, 1}}).has_value());
    REQUIRE(m.setUVs({{0, 0}, {1, 0}, {1, 1}, {0, 1}}).has_value());
    m.addFaceGroup("g");
    REQUIRE(m.setFaces({0, 1, 2, 3}, {0, 1, 2, 3}, {0}).has_value());

    const auto r = m.setUVs({{0, 0}});  // would strand fuvs indices 1..3
    REQUIRE_FALSE(r.has_value());
    CHECK(r.error() == MeshError::UvIndexOutOfRange);

    m.calcNormals();
    m.calcVertexTangents();  // must not read out of bounds
    CHECK(m.vtang().size() == 4);
}

// --- Finding 4 (HIGH) ------------------------------------------------------
// vmap_ held indices validated against the OLD vertex array; refreshPositions
// indexed through an unchecked span.
TEST_CASE("shrinking the vertex array after setFaces is rejected", "[regression][core][mesh]") {
    // vmap_ holds indices validated against the vertex array as it was at build
    // time. Rather than guard every consumer that gathers through it, the shrink
    // is refused at the source -- the same reasoning as setUVs.
    Mesh m("m", 4);
    REQUIRE(m.setCoords({{0, 0, 0}, {1, 0, 0}, {1, 0, 1}, {0, 0, 1}}).has_value());
    m.addFaceGroup("g");
    REQUIRE(m.setFaces({0, 1, 2, 3}, {}, {0}).has_value());
    m.calcNormals();

    auto rm = RenderMesh::build(m);
    REQUIRE(rm.vertexCount() == 4);

    const auto r = m.setCoords({{0, 0, 0}});  // would strand fvert indices 1..3
    REQUIRE_FALSE(r.has_value());
    CHECK(r.error() == MeshError::VertexIndexOutOfRange);

    CHECK(rm.matches(m));  // mesh unchanged, table still valid
    rm.refreshPositions(m);
    CHECK(rm.coord().size() == rm.vertexCount());
}

// --- Finding 8 (MEDIUM) ----------------------------------------------------
// A legal topology change -- same vertex count, different faces -- leaves the
// unweld table stale. refreshPositions must detect that rather than gather
// through indices that no longer describe the mesh.
TEST_CASE("refreshPositions detects a stale topology", "[regression][core][render]") {
    Mesh m("m", 4);
    REQUIRE(m.setCoords({{0, 0, 0}, {1, 0, 0}, {1, 0, 1}, {0, 0, 1}}).has_value());
    m.addFaceGroup("g");
    REQUIRE(m.setFaces({0, 1, 2, 3, 0, 1, 2, 3}, {}, {0, 0}).has_value());
    m.calcNormals();

    auto rm = RenderMesh::build(m);
    REQUIRE(rm.matches(m));
    const size_t builtIndices = rm.indexCount();

    // Same vertex count, fewer faces: legal, but the table no longer applies.
    REQUIRE(m.setFaces({0, 1, 2, 3}, {}, {0}).has_value());
    m.calcNormals();
    CHECK_FALSE(rm.matches(m));

    rm.refreshPositions(m);                  // must be a no-op, not a bad read
    CHECK(rm.indexCount() == builtIndices);  // still describes the OLD topology

    const auto rebuilt = RenderMesh::build(m);
    CHECK(rebuilt.matches(m));
    CHECK(rebuilt.indexCount() == 6);  // one quad -> two triangles
}

// --- Finding 5 (MEDIUM) ----------------------------------------------------
// groupRanges_ was sized from faceGroups().size(); a face carrying a larger
// group id had its indices silently unreachable from any draw range.
TEST_CASE("every index is reachable through some group range", "[regression][core][render]") {
    Mesh m("m", 4);
    REQUIRE(m.setCoords({{0, 0, 0}, {1, 0, 0}, {1, 0, 1}, {0, 0, 1}}).has_value());
    m.addFaceGroup("only");  // one group registered...
    REQUIRE(m.setFaces({0, 1, 2, 3, 0, 1, 2, 3}, {}, {0, 7}).has_value());  // ...id 7 used
    m.calcNormals();

    const auto rm    = RenderMesh::build(m);
    uint32_t covered = 0;
    for (const auto& g : rm.groupRanges())
        covered += g.count;
    CHECK(covered == rm.indexCount());  // was 6 of 12 before the fix
}

// --- Finding 6 (MEDIUM) ----------------------------------------------------
// Triangulation only ever read corners 0..3, so a pentagon lost geometry.
TEST_CASE("an n-gon is fan-triangulated completely", "[regression][core][render]") {
    Mesh m("pent", 5);
    REQUIRE(m.setCoords({{0, 0, 0}, {1, 0, 0}, {2, 0, 1}, {1, 0, 2}, {0, 0, 2}}).has_value());
    m.addFaceGroup("g");
    REQUIRE(m.setFaces({0, 1, 2, 3, 4}, {}, {0}).has_value());
    m.calcNormals();

    const auto rm = RenderMesh::build(m);
    CHECK(rm.indexCount() == 9);  // a pentagon is 3 triangles

    std::set<uint32_t> referenced(rm.index().begin(), rm.index().end());
    CHECK(referenced.size() == rm.vertexCount());  // no orphaned render vertex
}

// --- Finding 9 (LOW) -------------------------------------------------------
TEST_CASE("tmap is empty when the mesh has no UVs", "[regression][core][render]") {
    Mesh m("m", 4);
    REQUIRE(m.setCoords({{0, 0, 0}, {1, 0, 0}, {1, 0, 1}, {0, 0, 1}}).has_value());
    m.addFaceGroup("g");
    REQUIRE(m.setFaces({0, 1, 2, 3}, {}, {0}).has_value());
    m.calcNormals();

    const auto rm = RenderMesh::build(m);
    CHECK(rm.texco().empty());
    CHECK(rm.tmap().empty());  // documented as an index into texco; must not lie
}

// Found by self-review of the face-hiding change, then confirmed by running it:
// a Mesh with vertsPerPrimitive == 1 is constructible, setFaces() accepts it,
// and RenderMesh::build then threw std::length_error -- `vpp - 2` is size_t
// arithmetic, so it wrapped to SIZE_MAX and reserve() rejected it. Reachable
// from public API. Pre-existed the mask work; fixed while it was in hand.
TEST_CASE("a 1-corner mesh does not underflow the index reserve", "[regression][rendermesh]") {
    Mesh m("degenerate", 1);
    REQUIRE(m.setCoords({{0, 0, 0}, {1, 0, 0}, {0, 1, 0}}).has_value());
    REQUIRE(m.setFaces({0, 1, 2}, {}, {}).has_value());
    REQUIRE(m.faceCount() == 3);

    const auto rm = RenderMesh::build(m);  // threw std::length_error before
    CHECK(rm.indexCount() == 0);           // nothing to fan-triangulate
}
