// SPDX-License-Identifier: AGPL-3.0-or-later
//
// USD ASCII export. There is no oracle in the reference -- MakeHuman has no USD
// support at all -- and assimp has none either, so these tests check the format
// structurally and tools/run_blender_validation.sh checks it in Blender.

#include "makehuman/core/ObjReader.h"
#include "makehuman/core/RenderMesh.h"
#include "makehuman/io/UsdWriter.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>

using namespace mh;

namespace {

core::Mesh baseMesh() {
    auto m = core::loadObj(std::filesystem::path(MH_DATA_DIR) / "3dobjs" / "base.obj");
    REQUIRE(m.has_value());
    return std::move(*m);
}

std::string readAll(const std::filesystem::path& p) {
    std::ifstream in(p);
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

size_t countOccurrences(const std::string& hay, const std::string& needle) {
    size_t n  = 0;
    size_t at = hay.find(needle);
    while (at != std::string::npos) {
        ++n;
        at = hay.find(needle, at + needle.size());
    }
    return n;
}

}  // namespace

TEST_CASE("a USDA file has the required header and prim structure", "[usd]") {
    const auto mesh = baseMesh();
    const auto rm   = core::RenderMesh::build(mesh);

    const auto out = std::filesystem::temp_directory_path() / "mh_usd_header.usda";
    const auto r   = io::writeUsda(out, rm.view());
    REQUIRE(r.has_value());
    CHECK(r->vertices == rm.vertexCount());
    CHECK(r->triangles == rm.indexCount() / 3);

    const std::string t = readAll(out);
    CHECK(t.starts_with("#usda 1.0"));
    CHECK(t.find("defaultPrim = \"MakeHuman\"") != std::string::npos);
    CHECK(t.find("metersPerUnit = 1") != std::string::npos);
    // USD records the up axis, so unlike BVH a consumer never has to guess.
    CHECK(t.find("upAxis = \"Y\"") != std::string::npos);
    CHECK(t.find("def Xform \"MakeHuman\"") != std::string::npos);
    CHECK(t.find("def Mesh \"mesh\"") != std::string::npos);
    // Without this a consumer may treat the mesh as a subdivision cage and
    // render a smoothed, shrunken body.
    CHECK(t.find("uniform token subdivisionScheme = \"none\"") != std::string::npos);

    std::error_code ec;
    std::filesystem::remove(out, ec);
}

// RenderView is already unwelded and fan-triangulated, so every face is a
// triangle and there is exactly one normal and one UV per point. That is what
// makes interpolation "vertex" correct rather than "faceVarying".
TEST_CASE("USDA geometry arrays are consistent", "[usd]") {
    const auto mesh = baseMesh();
    const auto rm   = core::RenderMesh::build(mesh);

    const auto out = std::filesystem::temp_directory_path() / "mh_usd_geom.usda";
    REQUIRE(io::writeUsda(out, rm.view()).has_value());
    const std::string t = readAll(out);

    // faceVertexCounts is all 3s, one per triangle.
    // Search for the closing bracket from AFTER the marker: the marker itself
    // contains "int[]", so scanning from its start finds THAT ']' four
    // characters in and yields an empty array. The first version of this test
    // did exactly that and counted zero 3s out of 36,972.
    const std::string fvcMarker = "int[] faceVertexCounts = [";
    const auto fvcAt            = t.find(fvcMarker);
    REQUIRE(fvcAt != std::string::npos);
    const auto fvcFrom = fvcAt + fvcMarker.size();
    const auto fvcEnd  = t.find(']', fvcFrom);
    REQUIRE(fvcEnd != std::string::npos);
    const std::string fvc = t.substr(fvcFrom, fvcEnd - fvcFrom);
    CHECK(static_cast<size_t>(std::count(fvc.begin(), fvc.end(), '3')) == rm.indexCount() / 3);

    CHECK(t.find("point3f[] points = [") != std::string::npos);
    CHECK(t.find("normal3f[] normals = [") != std::string::npos);
    CHECK(t.find("texCoord2f[] primvars:st = [") != std::string::npos);
    // Both attribute arrays are per point, not per face corner.
    CHECK(countOccurrences(t, "interpolation = \"vertex\"") == 2);

    std::error_code ec;
    std::filesystem::remove(out, ec);
}

// USD's UV origin is bottom-left, the SAME as OBJ and MakeHuman -- unlike
// glTF, whose origin is top-left and which therefore flips V. Flipping here
// too would mirror every texture vertically.
TEST_CASE("USDA does not flip V", "[usd]") {
    core::Mesh m("uv", 4);
    REQUIRE(m.setCoords({{0, 0, 0}, {1, 0, 0}, {1, 1, 0}, {0, 1, 0}}).has_value());
    REQUIRE(m.setUVs({{0.0F, 0.25F}, {1, 0}, {1, 1}, {0, 1}}).has_value());
    REQUIRE(m.setFaces({0, 1, 2, 3}, {0, 1, 2, 3}, {0}).has_value());
    const auto rm = core::RenderMesh::build(m);

    const auto out = std::filesystem::temp_directory_path() / "mh_usd_uv.usda";
    REQUIRE(io::writeUsda(out, rm.view()).has_value());
    const std::string t = readAll(out);

    // 0.25 must appear as written, not as 0.75.
    const std::string stMarker = "primvars:st = [";
    const auto stAt            = t.find(stMarker);
    REQUIRE(stAt != std::string::npos);
    const auto stFrom = stAt + stMarker.size();
    const auto stEnd  = t.find(']', stFrom);
    REQUIRE(stEnd != std::string::npos);
    const std::string st = t.substr(stFrom, stEnd - stFrom);
    CHECK(st.find("0.25") != std::string::npos);
    CHECK(st.find("0.75") == std::string::npos);

    std::error_code ec;
    std::filesystem::remove(out, ec);
}

TEST_CASE("USDA scale reaches the points and the extent", "[usd]") {
    const auto mesh = baseMesh();
    const auto rm   = core::RenderMesh::build(mesh);

    io::UsdWriteOptions dm;
    dm.scale       = 1.0F;  // keep decimetres
    const auto out = std::filesystem::temp_directory_path() / "mh_usd_dm.usda";
    REQUIRE(io::writeUsda(out, rm.view(), dm).has_value());
    const std::string t = readAll(out);

    // The base mesh is 16.9455 dm tall; at scale 1 the extent must show that,
    // not the 1.69 metres the default produces.
    const auto extAt = t.find("float3[] extent = [");
    REQUIRE(extAt != std::string::npos);
    const auto extEnd    = t.find("]\n", extAt);
    const std::string ex = t.substr(extAt, extEnd - extAt);
    CHECK(ex.find("8.4") != std::string::npos);  // the y bounds are ~+/-8.45 dm

    std::error_code ec;
    std::filesystem::remove(out, ec);
}

TEST_CASE("an empty mesh and a bad path are refused", "[usd]") {
    const core::Mesh empty;
    const auto rm = core::RenderMesh::build(empty);
    const auto p  = std::filesystem::temp_directory_path() / "mh_usd_empty.usda";
    const auto r  = io::writeUsda(p, rm.view());
    REQUIRE_FALSE(r.has_value());
    CHECK(r.error().kind == io::UsdWriteErrorKind::EmptyMesh);

    const auto mesh = baseMesh();
    const auto rm2  = core::RenderMesh::build(mesh);
    const auto dir  = std::filesystem::temp_directory_path() / "mh_usd_blocked";
    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
    std::filesystem::create_directories(dir / "out.usda", ec);  // a directory, not a file
    const auto r2 = io::writeUsda(dir / "out.usda", rm2.view());
    REQUIRE_FALSE(r2.has_value());
    CHECK(r2.error().kind == io::UsdWriteErrorKind::CannotOpen);
    std::filesystem::remove_all(dir, ec);
}
