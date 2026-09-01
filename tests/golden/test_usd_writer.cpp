// SPDX-License-Identifier: AGPL-3.0-or-later
//
// USD ASCII export. There is no oracle in the reference -- MakeHuman has no USD
// support at all -- and assimp has none either, so these tests check the format
// structurally and tools/run_blender_validation.sh checks it in Blender.

#include <sstream>
#include "makehuman/core/ObjReader.h"
#include "makehuman/core/RenderMesh.h"
#include "makehuman/io/UsdWriter.h"
#include "makehuman/rig/Skeleton.h"
#include "makehuman/rig/Skinning.h"
#include "makehuman/rig/VertexWeights.h"

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

// USD was the only format left carrying no materials at all. The convention is
// UsdPreviewSurface: a Material prim wrapping a Shader, and each Mesh binding
// the one it uses.
TEST_CASE("a material becomes a UsdPreviewSurface the mesh binds", "[usd][material]") {
    const core::Mesh m = quadAt(0.0F, "body");
    const auto rm      = core::RenderMesh::build(m);

    foundation::MaterialDesc skin;
    skin.name      = "DefaultSkin";
    skin.diffuse   = {0.9F, 0.7F, 0.6F};
    skin.shininess = 0.25F;

    const auto out = std::filesystem::temp_directory_path() / "mh_usd_mat.usda";
    std::error_code ec;
    std::filesystem::remove(out, ec);
    REQUIRE(io::writeUsdaScene(out, {{{rm.view(), "body", &skin}}}).has_value());

    const std::string t = readAll(out);
    INFO(t.substr(0, 900));
    CHECK(t.find("def Material \"DefaultSkin\"") != std::string::npos);
    CHECK(t.find("uniform token info:id = \"UsdPreviewSurface\"") != std::string::npos);
    // The mesh must actually bind it, or the material is decoration.
    CHECK(t.find("rel material:binding = </MakeHuman/Looks/DefaultSkin>") != std::string::npos);
    // MaterialBindingAPI must be APPLIED, and it is prim metadata -- it belongs
    // in the parentheses before the body. Emitting it as a property instead
    // made the stage fail to open at all, which only usdchecker revealed.
    CHECK(t.find("    (\n        prepend apiSchemas = [\"MaterialBindingAPI\"]\n    )\n") !=
          std::string::npos);
    // A UsdPrimvarReader's varname is a string; a token is rejected as
    // ShaderSdrCompliance.MismatchedPropertyType.
    CHECK(t.find("token inputs:varname") == std::string::npos);
    // Roughness is derived from shininess, as every other writer does.
    CHECK(t.find("inputs:roughness = 0.75") != std::string::npos);

    std::filesystem::remove(out, ec);
}

// Two meshes with different materials must bind different ones -- otherwise a
// DCC tool paints the clothes with the skin.
TEST_CASE("each mesh binds its own material", "[usd][material]") {
    const core::Mesh a = quadAt(0.0F, "body");
    const core::Mesh b = quadAt(8.0F, "eyes");
    const auto rmA     = core::RenderMesh::build(a);
    const auto rmB     = core::RenderMesh::build(b);

    foundation::MaterialDesc skin;
    skin.name = "DefaultSkin";
    foundation::MaterialDesc eye;
    eye.name = "Eye_brown";

    const auto out = std::filesystem::temp_directory_path() / "mh_usd_mat2.usda";
    std::error_code ec;
    std::filesystem::remove(out, ec);
    REQUIRE(io::writeUsdaScene(out, {{{rmA.view(), "body", &skin}, {rmB.view(), "eyes", &eye}}})
                .has_value());

    const std::string t = readAll(out);
    CHECK(countOccurrences(t, "def Material ") == 2);
    CHECK(t.find("</MakeHuman/Looks/DefaultSkin>") != std::string::npos);
    CHECK(t.find("</MakeHuman/Looks/Eye_brown>") != std::string::npos);
    // One Looks scope holding both.
    CHECK(countOccurrences(t, "def Scope \"Looks\"") == 1);

    std::filesystem::remove(out, ec);
}

// A material-less scene must be exactly what it was before materials existed --
// no empty Looks scope, no stray binding.
TEST_CASE("no material means no Looks scope", "[usd][material]") {
    const core::Mesh m = quadAt(0.0F, "solo");
    const auto rm      = core::RenderMesh::build(m);

    const auto out = std::filesystem::temp_directory_path() / "mh_usd_nomat.usda";
    std::error_code ec;
    std::filesystem::remove(out, ec);
    REQUIRE(io::writeUsdaScene(out, {{{rm.view(), "mesh"}}}).has_value());

    const std::string t = readAll(out);
    CHECK(t.find("Looks") == std::string::npos);
    CHECK(t.find("material:binding") == std::string::npos);

    std::filesystem::remove(out, ec);
}

// A texture is referenced by an asset path, so the file has to be beside the
// stage -- the same rule the OBJ writer follows for map_Kd.
TEST_CASE("a textured material references a copied texture", "[usd][material]") {
    const auto texDir = std::filesystem::temp_directory_path() / "mh_usd_tex_src";
    std::filesystem::create_directories(texDir);
    const auto texSrc = texDir / "mh_usd_tex.png";
    {
        std::ofstream f(texSrc, std::ios::binary);
        f << "not really a png, but a real file";
    }

    const core::Mesh m = quadAt(0.0F, "body");
    const auto rm      = core::RenderMesh::build(m);
    foundation::MaterialDesc mat;
    mat.name           = "Painted";
    mat.diffuseTexture = texSrc;

    const auto out = std::filesystem::temp_directory_path() / "mh_usd_tex.usda";
    std::error_code ec;
    std::filesystem::remove(out, ec);
    REQUIRE(io::writeUsdaScene(out, {{{rm.view(), "body", &mat}}}).has_value());

    const std::string t = readAll(out);
    INFO(t.substr(0, 1200));
    CHECK(t.find("uniform token info:id = \"UsdUVTexture\"") != std::string::npos);
    CHECK(t.find("asset inputs:file = @mh_usd_tex.png@") != std::string::npos);
    // Named beside the stage, so it must BE beside the stage.
    CHECK(std::filesystem::exists(out.parent_path() / "mh_usd_tex.png"));

    std::filesystem::remove_all(texDir, ec);
    std::filesystem::remove(out.parent_path() / "mh_usd_tex.png", ec);
    std::filesystem::remove(out, ec);
}

// --- USDZ -------------------------------------------------------------------
//
// USDZ is a zip with two rules that are not optional, both taken from a
// reference archive produced by Apple's own `usdzip` rather than from memory:
//
//   * every entry STORED, never deflated -- a consumer memory-maps the archive
//     and reads the stage in place, so compressed data is unreadable;
//   * every entry's DATA on a 64-byte boundary, padded through the zip extra
//     field with header id 0x1986 (usdzip emits id 0x1986 / size 22 / zeros,
//     giving 30 + 8 + 26 = 64).
//
// `usdchecker --arkit` accepts the result -- Apple's own validator on its
// strictest profile -- which is the check that actually matters. These
// assertions pin the structure so a regression is caught without that tool.
TEST_CASE("a usdz is a stored, 64-byte aligned zip", "[io][usd][usdz]") {
    const core::Mesh m = baseMesh();
    const auto rm      = core::RenderMesh::build(m);
    const std::vector<io::UsdSceneEntry> scene{{rm.view(), "body", nullptr}};

    const auto out = std::filesystem::temp_directory_path() / "mh_package.usdz";
    REQUIRE(io::writeUsdzScene(out, scene).has_value());

    std::ifstream in(out, std::ios::binary);
    REQUIRE(in);
    const std::string bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    REQUIRE(bytes.size() > 64);

    const auto u16 = [&bytes](size_t at) {
        return static_cast<unsigned>(static_cast<unsigned char>(bytes[at])) |
               (static_cast<unsigned>(static_cast<unsigned char>(bytes[at + 1])) << 8U);
    };

    // Local file header signature.
    CHECK(bytes.compare(0, 4, "PK\x03\x04", 4) == 0);
    // Compression method 0 = STORED. Deflate here makes the stage unreadable to
    // a consumer that memory-maps the archive.
    CHECK(u16(8) == 0);

    const size_t nameLen  = u16(26);
    const size_t extraLen = u16(28);
    const size_t dataAt   = 30 + nameLen + extraLen;

    // The stage is the FIRST entry -- that is how a reader finds it.
    CHECK(bytes.compare(30, nameLen, out.stem().string() + ".usda") == 0);

    // The rule that a hand-rolled zip gets wrong.
    INFO("data begins at " << dataAt);
    CHECK(dataAt % 64 == 0);

    // Padding must be a WELL-FORMED extra field, not loose bytes: a strict
    // reader parses this as TLV.
    REQUIRE(extraLen >= 4);
    CHECK(u16(30 + nameLen) == 0x1986);
    CHECK(u16(30 + nameLen + 2) == extraLen - 4);

    // And the payload really is the stage.
    CHECK(bytes.compare(dataAt, 9, "#usda 1.0") == 0);

    std::error_code ec;
    std::filesystem::remove(out, ec);
}

// --- UsdSkel ----------------------------------------------------------------
//
// `usdchecker` reports Success on the result, but it cannot catch either of the
// two traps below -- both produce a perfectly valid stage that poses wrongly.
// So they are pinned here.
TEST_CASE("a skinned stage binds a UsdSkel skeleton", "[io][usd][usdskel]") {
    const auto rigPath = std::filesystem::path(MH_DATA_DIR) / "rigs" / "default.mhskel";
    if (!std::filesystem::exists(rigPath)) return;

    core::Mesh mesh = baseMesh();
    auto skel       = rig::loadSkeleton(rigPath);
    REQUIRE(skel.has_value());
    REQUIRE(skel->updateJoints(mesh.coord()));
    REQUIRE(skel->buildRestMatrices());
    auto vw = rig::loadWeights(std::filesystem::path(MH_DATA_DIR) / "rigs" / "default_weights.mhw",
                               mesh.vertexCount());
    REQUIRE(vw.has_value());

    const auto rm       = core::RenderMesh::build(mesh);
    const auto compiled = vw->compile(*skel, 4);
    const auto skin     = rig::buildSkinData(*skel, compiled, rm.vmap());
    const auto view     = skin.view();
    REQUIRE(skin.globalRest.size() == 163);

    const auto out = std::filesystem::temp_directory_path() / "mh_usdskel.usda";
    const std::vector<io::UsdSceneEntry> scene{{rm.view(), "body", nullptr}};
    REQUIRE(io::writeUsdaScene(out, scene, {}, &view).has_value());

    std::ifstream in(out);
    const std::string t((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());

    // UsdSkel requires the skeleton and everything bound to it under a SkelRoot.
    CHECK(t.find("def SkelRoot ") != std::string::npos);
    CHECK(t.find("def Skeleton \"Skel\"") != std::string::npos);
    CHECK(t.find("prepend apiSchemas = [\"SkelBindingAPI\"]") != std::string::npos);
    CHECK(t.find("rel skel:skeleton = </") != std::string::npos);
    CHECK(t.find("int[] primvars:skel:jointIndices") != std::string::npos);
    CHECK(t.find("float[] primvars:skel:jointWeights") != std::string::npos);
    CHECK(t.find("elementSize = 4") != std::string::npos);

    // TRAP 1: joint tokens are USD PATHS made of identifiers. MakeHuman bone
    // names carry a dot and a dash -- `upperarm01.L`, `finger1-1.L` -- and a dot
    // is the property separator in a USD path. Emitting them raw makes a stage
    // usdchecker rejects, so they are sanitised.
    const std::string kJointsKey = "uniform token[] joints = [";
    const auto jointsAt          = t.find(kJointsKey);
    REQUIRE(jointsAt != std::string::npos);
    // From the OPENING bracket of the array, not from the key: the key itself
    // contains a `]` (in `token[]`), and searching from its start finds that
    // one and yields an empty slice that trivially "has no dots".
    const auto arrayAt   = jointsAt + kJointsKey.size();
    const auto jointsEnd = t.find(']', arrayAt);
    REQUIRE(jointsEnd != std::string::npos);
    const std::string joints = t.substr(arrayAt, jointsEnd - arrayAt);
    CHECK(joints.find('.') == std::string::npos);
    CHECK(joints.find('-') == std::string::npos);
    // The hierarchy IS the token: a child's path contains its parent's.
    CHECK(joints.find("root/spine05") != std::string::npos);
    CHECK(joints.find("clavicle_L/shoulder01_L/upperarm01_L") != std::string::npos);

    // TRAP 2: USD uses ROW vectors, this codebase uses COLUMN vectors. The two
    // are transposes, so an untransposed write emits every joint transposed --
    // and the stage still validates. The check is where translation lands: in
    // USD it is the LAST ROW, so the root bone's head must appear there.
    const auto bindAt = t.find("uniform matrix4d[] bindTransforms = [");
    REQUIRE(bindAt != std::string::npos);
    const std::string firstMatrix = t.substr(bindAt, 260);

    // The FOURTH tuple must BE the translation. Checking only that the numbers
    // appear somewhere is vacuous -- they appear either way, just in different
    // places -- and an earlier version of this test did exactly that and passed
    // with the transpose removed.
    // Scaled, like the points: the stage is written in metres and the rig is
    // authored in decimetres. Derived from unitScale rather than written out as
    // a second constant -- this assertion held the UNSCALED numbers until the
    // 10x skeleton was found, so a literal here is exactly how the bug hid.
    const float us = io::unitScale(io::Unit::Meter);
    const auto t0  = skel->bones[0].matRestGlobal.translation();
    std::ostringstream lastRow;
    lastRow << "(" << t0.x * us << ", " << t0.y * us << ", " << t0.z * us << ", 1)";
    INFO("first bindTransform: " << firstMatrix);
    INFO("expecting last row " << lastRow.str());
    CHECK(firstMatrix.find(lastRow.str()) != std::string::npos);

    // Untransposed, the last row would be the identity's (0, 0, 0, 1) and the
    // translation would sit in the last COLUMN instead.
    CHECK(firstMatrix.find("(0, 0, 0, 1)") == std::string::npos);

    std::error_code ec;
    std::filesystem::remove(out, ec);
}

// The skeleton has to be in the SAME SPACE as the mesh.
//
// It was not. `points` are multiplied by the unit scale; `bindTransforms` and
// `restTransforms` emitted `globalRest` verbatim, so at the default unit
// (metre, scale 0.1) the mesh came out in metres and the rig in decimetres --
// **a skeleton ten times the size of the body**. Measured in Blender on our own
// export: mesh z −0.8178..0.8416, armature z −8.0385..8.4481.
//
// `usdchecker` passes either way. It validates the stage's structure, not
// whether the rig fits the mesh, which is why this needs its own check.
//
// Asserted as containment rather than against the scale factor: every joint's
// bind translation must lie inside the mesh's own `extent`. A 10x rig fails by
// an order of magnitude, and the property survives a change of unit.
TEST_CASE("the skeleton is in the same space as the mesh", "[io][usd][usdskel][units]") {
    const auto rigPath = std::filesystem::path(MH_DATA_DIR) / "rigs" / "default.mhskel";
    if (!std::filesystem::exists(rigPath)) return;

    core::Mesh mesh = baseMesh();
    auto skel       = rig::loadSkeleton(rigPath);
    REQUIRE(skel.has_value());
    REQUIRE(skel->updateJoints(mesh.coord()));
    REQUIRE(skel->buildRestMatrices());
    auto vw = rig::loadWeights(std::filesystem::path(MH_DATA_DIR) / "rigs" / "default_weights.mhw",
                               mesh.vertexCount());
    REQUIRE(vw.has_value());

    const auto rm       = core::RenderMesh::build(mesh);
    const auto compiled = vw->compile(*skel, 4);
    const auto skin     = rig::buildSkinData(*skel, compiled, rm.vmap());
    const auto view     = skin.view();

    const auto out = std::filesystem::temp_directory_path() / "mh_usdskel_space.usda";
    const std::vector<io::UsdSceneEntry> scene{{rm.view(), "body", nullptr}};
    REQUIRE(io::writeUsdaScene(out, scene, {}, &view).has_value());
    const std::string t = readAll(out);

    // The mesh's own declared bounds, straight out of the file.
    const auto extentAt = t.find("float3[] extent = [(");
    REQUIRE(extentAt != std::string::npos);
    const auto extentEnd = t.find("]", extentAt + 20);
    REQUIRE(extentEnd != std::string::npos);
    std::string ext = t.substr(extentAt + 20, extentEnd - extentAt - 20);
    for (char& c : ext)
        if (c == '(' || c == ')' || c == ',') c = ' ';
    std::istringstream es(ext);
    double lox = 0;
    double loy = 0;
    double loz = 0;
    double hix = 0;
    double hiy = 0;
    double hiz = 0;
    es >> lox >> loy >> loz >> hix >> hiy >> hiz;
    REQUIRE(hiy > loy);

    // Every bindTransform's translation. A USD matrix4d is ROW-vector, so the
    // translation is the LAST ROW -- elements 12,13,14 of the sixteen.
    const std::string kKey = "uniform matrix4d[] bindTransforms = [";
    const auto at          = t.find(kKey);
    REQUIRE(at != std::string::npos);
    const auto arrayAt = at + kKey.size();
    const auto endAt   = t.find("]\n", arrayAt);
    REQUIRE(endAt != std::string::npos);
    std::string mats = t.substr(arrayAt, endAt - arrayAt);
    for (char& c : mats)
        if (c == '(' || c == ')' || c == ',') c = ' ';

    std::istringstream ms(mats);
    std::vector<double> nums;
    for (double d = 0; ms >> d;)
        nums.push_back(d);
    REQUIRE(nums.size() == skin.globalRest.size() * 16);

    // A little slack: joints sit inside the body, but the root can be at a foot.
    const double padY = (hiy - loy) * 0.1;
    size_t outside    = 0;
    for (size_t j = 0; j < skin.globalRest.size(); ++j) {
        const double ty = nums[j * 16 + 13];
        if (ty < loy - padY || ty > hiy + padY) ++outside;
    }
    INFO("mesh y " << loy << ".." << hiy << ", joints outside " << outside << " of "
                   << skin.globalRest.size());
    CHECK(outside == 0);
}

TEST_CASE("an unskinned stage is unchanged", "[io][usd][usdskel]") {
    // Passing no skin must leave the output exactly as it was before UsdSkel
    // existed -- a plain Xform, no skeleton, no binding.
    const core::Mesh m = baseMesh();
    const auto rm      = core::RenderMesh::build(m);
    const auto out     = std::filesystem::temp_directory_path() / "mh_usdnoskel.usda";
    const std::vector<io::UsdSceneEntry> scene{{rm.view(), "body", nullptr}};
    REQUIRE(io::writeUsdaScene(out, scene).has_value());

    std::ifstream in(out);
    const std::string t((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    CHECK(t.find("def Xform ") != std::string::npos);
    CHECK(t.find("SkelRoot") == std::string::npos);
    CHECK(t.find("Skeleton") == std::string::npos);
    CHECK(t.find("skel:") == std::string::npos);

    std::error_code ec;
    std::filesystem::remove(out, ec);
}
