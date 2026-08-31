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
#include <array>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

using namespace mh;

namespace {

/// A quad offset along X, so two of them are distinguishable in one stage.
core::Mesh quadAt(float x, const char* name) {
    core::Mesh m(name, 4);
    REQUIRE(m.setCoords({{x, 0, 0}, {x + 2, 0, 0}, {x + 2, 0, 3}, {x, 0, 3}}).has_value());
    REQUIRE(m.setUVs({{0, 0}, {1, 0}, {1, 1}, {0, 1}}).has_value());
    m.addFaceGroup("g");
    REQUIRE(m.setFaces({0, 1, 2, 3}, {0, 1, 2, 3}, {0}).has_value());
    m.buildAdjacency();
    m.calcNormals();
    return m;
}

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

TEST_CASE("USDA honours every unit, in the points and the header", "[usd][units]") {
    const auto mesh = baseMesh();
    const auto rm   = core::RenderMesh::build(mesh);

    // The base mesh is 16.9455 dm tall -- 169.5 cm, a real human height.
    struct Case {
        io::Unit unit;
        const char* name;
        double height;
        const char* metersPerUnit;
    };

    const std::array<Case, 4> cases{{
        {io::Unit::Decimeter, "dm", 16.9455, "0.1"},
        {io::Unit::Meter, "m", 1.69455, "1"},
        {io::Unit::Centimeter, "cm", 169.455, "0.01"},
        {io::Unit::Inch, "in", 66.7146, "0.0254"},
    }};

    for (const auto& c : cases) {
        CAPTURE(c.name);
        io::UsdWriteOptions o;
        o.unit = c.unit;

        const auto out = std::filesystem::temp_directory_path() /
                         ("mh_usd_unit_" + std::string(c.name) + ".usda");
        REQUIRE(io::writeUsda(out, rm.view(), o).has_value());
        const std::string t2 = readAll(out);

        // metersPerUnit must AGREE with the points. Two independent knobs for
        // one physical fact is how a file claims metres while holding
        // centimetres.
        CHECK(t2.find(std::string("metersPerUnit = ") + c.metersPerUnit) != std::string::npos);

        // Measure the body from the extent rather than trusting the header.
        const std::string exMarker = "float3[] extent = [";
        const auto exAt            = t2.find(exMarker);
        REQUIRE(exAt != std::string::npos);
        const auto exFrom = exAt + exMarker.size();
        const auto exEnd  = t2.find("]\n", exFrom);
        REQUIRE(exEnd != std::string::npos);

        // extent is [(minx, miny, minz), (maxx, maxy, maxz)]; y is index 1 and 4.
        const std::string ex = t2.substr(exFrom, exEnd - exFrom);
        std::vector<double> nums;
        const char* p2 = ex.c_str();
        while (*p2 != 0 && nums.size() < 6) {
            if ((*p2 >= '0' && *p2 <= '9') || *p2 == '-') {
                char* end = nullptr;
                nums.push_back(std::strtod(p2, &end));
                p2 = end;
            } else {
                ++p2;
            }
        }
        REQUIRE(nums.size() == 6);
        const double height = nums[4] - nums[1];
        INFO("height " << height << " expected " << c.height);
        CHECK(std::abs(height - c.height) < c.height * 1e-4);

        std::error_code ec;
        std::filesystem::remove(out, ec);
    }
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

// USD was the last single-mesh writer: a dressed character exported to .usda
// arrived naked. A stage is a scene graph, so several meshes is its natural
// shape -- one Mesh prim per entry under the same Xform.
TEST_CASE("several meshes write into one USD stage", "[usd][multimesh]") {
    const core::Mesh a = quadAt(0.0F, "body");
    const core::Mesh b = quadAt(8.0F, "eyes");
    const auto rmA     = core::RenderMesh::build(a);
    const auto rmB     = core::RenderMesh::build(b);

    const auto out = std::filesystem::temp_directory_path() / "mh_usd_scene.usda";
    std::error_code ec;
    std::filesystem::remove(out, ec);

    const std::vector<io::UsdSceneEntry> scene{{rmA.view(), "body"}, {rmB.view(), "eyes"}};
    const auto r = io::writeUsdaScene(out, scene);
    REQUIRE(r.has_value());
    CHECK(r->vertices == rmA.view().vertexCount() + rmB.view().vertexCount());
    CHECK(r->triangles == rmA.view().triangleCount() + rmB.view().triangleCount());

    const std::string t = readAll(out);
    INFO(t.substr(0, 400));
    // One Mesh prim per entry, each named -- a DCC tool must be able to select
    // the clothes apart from the body.
    CHECK(countOccurrences(t, "def Mesh ") == 2);
    CHECK(t.find("def Mesh \"body\"") != std::string::npos);
    CHECK(t.find("def Mesh \"eyes\"") != std::string::npos);
    // Still exactly one Xform wrapping them, and one defaultPrim.
    CHECK(countOccurrences(t, "def Xform ") == 1);
    CHECK(countOccurrences(t, "defaultPrim") == 1);

    std::filesystem::remove(out, ec);
}

// The single-mesh entry point stays byte-identical: it is a one-entry scene,
// and every existing consumer of a .usda export must see what it saw before.
TEST_CASE("a one-entry USD scene matches the single-mesh writer", "[usd][multimesh]") {
    const core::Mesh m = quadAt(0.0F, "solo");
    const auto rm      = core::RenderMesh::build(m);

    const auto viaOld = std::filesystem::temp_directory_path() / "mh_usd_one_old.usda";
    const auto viaNew = std::filesystem::temp_directory_path() / "mh_usd_one_new.usda";
    std::error_code ec;
    std::filesystem::remove(viaOld, ec);
    std::filesystem::remove(viaNew, ec);

    REQUIRE(io::writeUsda(viaOld, rm.view()).has_value());
    REQUIRE(io::writeUsdaScene(viaNew, {{{rm.view(), "mesh"}}}).has_value());
    CHECK(readAll(viaOld) == readAll(viaNew));

    std::filesystem::remove(viaOld, ec);
    std::filesystem::remove(viaNew, ec);
}
