// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Regression tests for the code-review findings on commit 61f48893.
// Each test FAILS on the pre-fix code; several were heap-overflows under ASan.

#include "makehuman/core/Mesh.h"
#include "makehuman/core/ObjReader.h"

#include <catch2/catch_test_macros.hpp>

#include <cmath>

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
        path_ = std::filesystem::temp_directory_path() /
                ("mh_reg_" + std::to_string(++counter) + ".obj");
        std::ofstream out(path_);
        out << contents;
    }
    ~TempObj() { std::error_code ec; std::filesystem::remove(path_, ec); }
    TempObj(const TempObj&) = delete;
    TempObj& operator=(const TempObj&) = delete;
    [[nodiscard]] const std::filesystem::path& path() const { return path_; }
private:
    std::filesystem::path path_;
};

} // namespace

// --- Finding 1 -------------------------------------------------------------
// maxValence_ was uint8_t. A vertex with >=256 incident faces wrapped it to 0,
// so the adjacency stride became 0, nothing was recorded, and EVERY vertex
// normal silently fell back to the zero-guard {0,1,0}. No error, no diagnostic.
TEST_CASE("valence above 255 does not wrap and destroy every normal",
          "[regression][core][mesh]") {
    constexpr uint32_t kFan = 300;             // > 255 incident faces on the apex
    constexpr float    kPi  = 3.14159265358979323846F;

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
        fv.insert(fv.end(), {0U, a, b, 0U});   // degenerate quad
        fg.push_back(0);
    }

    Mesh m("cone", 4);
    m.setCoords(std::move(coords));
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
TEST_CASE("replacing the face set invalidates stale adjacency",
          "[regression][core][mesh]") {
    Mesh m("m", 4);
    m.setCoords({{0, 0, 0}, {1, 0, 0}, {1, 0, 1}, {0, 0, 1}});
    m.addFaceGroup("g");

    REQUIRE(m.setFaces({0, 1, 2, 3,  0, 1, 2, 3,  0, 1, 2, 3}, {}, {0, 0, 0}).has_value());
    m.buildAdjacency();
    REQUIRE(m.faceCount() == 3);

    // Shrink to one face. The old adjacency references faces 1 and 2.
    REQUIRE(m.setFaces({0, 1, 2, 3}, {}, {0}).has_value());
    REQUIRE(m.faceCount() == 1);

    m.calcNormals();                            // read out of bounds before the fix
    CHECK(m.vnorm().size() == 4);
}

// --- Finding 3 / 10 --------------------------------------------------------
// calcFaceNormals indexed coord_[fvert_[...]] with no bounds check while
// buildAdjacency guarded the same indices. setFaces is public and validated
// nothing, so an out-of-range index was a heap-buffer-overflow.
TEST_CASE("setFaces rejects an out-of-range vertex index",
          "[regression][core][mesh]") {
    Mesh m("m", 4);
    m.setCoords({{0, 0, 0}, {1, 0, 0}, {0, 0, 1}});
    m.addFaceGroup("g");

    const auto r = m.setFaces({0, 1, 99, 2}, {}, {0});
    REQUIRE_FALSE(r.has_value());
    CHECK(r.error() == MeshError::VertexIndexOutOfRange);
    CHECK(m.faceCount() == 0);                  // mesh left unchanged
}

TEST_CASE("setFaces rejects an out-of-range UV index", "[regression][core][mesh]") {
    Mesh m("m", 4);
    m.setCoords({{0, 0, 0}, {1, 0, 0}, {0, 0, 1}, {1, 0, 1}});
    m.setUVs({{0, 0}, {1, 0}});
    m.addFaceGroup("g");

    const auto r = m.setFaces({0, 1, 2, 3}, {0, 1, 42, 0}, {0});
    REQUIRE_FALSE(r.has_value());
    CHECK(r.error() == MeshError::UvIndexOutOfRange);
}

TEST_CASE("setFaces rejects a UV array not parallel to the vertex array",
          "[regression][core][mesh]") {
    Mesh m("m", 4);
    m.setCoords({{0, 0, 0}, {1, 0, 0}, {0, 0, 1}, {1, 0, 1}});
    m.setUVs({{0, 0}, {1, 0}, {1, 1}, {0, 1}});
    m.addFaceGroup("g");

    const auto r = m.setFaces({0, 1, 2, 3}, {0, 1}, {0});
    REQUIRE_FALSE(r.has_value());
    CHECK(r.error() == MeshError::UvArraySizeMismatch);
}

TEST_CASE("setFaces rejects a face array that is not a whole number of primitives",
          "[regression][core][mesh]") {
    Mesh m("m", 4);
    m.setCoords({{0, 0, 0}, {1, 0, 0}, {0, 0, 1}});
    m.addFaceGroup("g");

    const auto r = m.setFaces({0, 1, 2}, {}, {0});   // 3 entries, stride 4
    REQUIRE_FALSE(r.has_value());
    CHECK(r.error() == MeshError::FaceArraySizeMismatch);
}

// --- Finding 9 -------------------------------------------------------------
// hasUV_ was stored and set unconditionally by setUVs, so calling setUVs AFTER
// setFaces({}) left hasUV() true with an empty fuvs_ — a consumer indexing
// fuvs() in parallel with fvert() got nothing.
TEST_CASE("hasUV stays false when faces carry no UV indices",
          "[regression][core][mesh]") {
    Mesh m("m", 4);
    m.setCoords({{0, 0, 0}, {1, 0, 0}, {0, 0, 1}, {1, 0, 1}});
    m.addFaceGroup("g");
    REQUIRE(m.setFaces({0, 1, 2, 3}, {}, {0}).has_value());
    m.setUVs({{0, 0}, {1, 0}, {1, 1}, {0, 1}});   // out of order, on purpose

    CHECK_FALSE(m.hasUV());
    CHECK(m.fuvs().empty());
}

// --- Finding 4 -------------------------------------------------------------
// A malformed v/vt line was silently dropped, shifting every later index so the
// file loaded "successfully" as a completely different mesh.
TEST_CASE("a malformed v line is an error, not a silent index shift",
          "[regression][core][obj]") {
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
TEST_CASE("an o statement sets the name and creates no face group",
          "[regression][core][obj]") {
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
TEST_CASE("a face with fewer than three corners is an error",
          "[regression][core][obj]") {
    const TempObj f("g t\nv 0 0 0\nv 1 0 0\nv 0 0 1\nf 1 2\nf 1 2 3\n");
    const auto m = loadObj(f.path());
    REQUIRE_FALSE(m.has_value());
    CHECK(m.error().kind == ObjErrorKind::DegenerateFace);
}

// --- Finding 7 -------------------------------------------------------------
// parseCorner checked only from_chars' error code, not that it consumed the
// whole token, so "1x" parsed as vertex 1.
TEST_CASE("a face corner with trailing garbage is an error",
          "[regression][core][obj]") {
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
