// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Writing more than one mesh into a single OBJ: what a dressed character is.
// Before this the app exported the body alone, so a character wearing anything
// came out naked.

#include "makehuman/core/ObjReader.h"
#include "makehuman/foundation/Types.h"
#include "makehuman/io/ObjWriter.h"
#include "makehuman/io/SceneIO.h"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

using namespace mh;
namespace fs = std::filesystem;

namespace {

/// A unit quad as two triangles, offset along X so two of them do not coincide.
struct Quad {
    std::vector<foundation::Vec3> coord;
    std::vector<foundation::Vec2> texco;
    std::vector<uint32_t> fvert;
    std::vector<uint32_t> fuvs;

    [[nodiscard]] foundation::MeshView view() const {
        foundation::MeshView v;
        v.coord                 = coord;
        v.texco                 = texco;
        v.fvert                 = fvert;
        v.fuvs                  = fuvs;
        v.vertsPerPrimitive     = 4;
        v.vertsPerFaceForExport = 4;
        return v;
    }
};

Quad quadAt(float x) {
    Quad q;
    q.coord = {{x, 0, 0}, {x + 1, 0, 0}, {x + 1, 1, 0}, {x, 1, 0}};
    q.texco = {{0, 0}, {1, 0}, {1, 1}, {0, 1}};
    q.fvert = {0, 1, 2, 3};
    q.fuvs  = {0, 1, 2, 3};
    return q;
}

std::string readAll(const fs::path& p) {
    std::ifstream in(p);
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

size_t countLinesStarting(const std::string& text, std::string_view prefix) {
    size_t n = 0;
    std::istringstream in(text);
    for (std::string line; std::getline(in, line);) {
        if (line.rfind(prefix, 0) == 0) ++n;
    }
    return n;
}

fs::path tmp(const char* name) {
    return fs::temp_directory_path() / name;
}

}  // namespace

TEST_CASE("two meshes write into one OBJ with separate groups", "[io][objscene]") {
    const Quad body = quadAt(0.0F);
    const Quad worn = quadAt(4.0F);
    const auto out  = tmp("mh_scene_two.obj");
    fs::remove(out);

    const std::vector<io::ObjSceneEntry> scene{
        {body.view(), "body", nullptr, {}},
        {worn.view(), "eyes", nullptr, {}},
    };
    const auto r = io::writeObjScene(out, scene);
    REQUIRE(r.has_value());
    CHECK(r->vertices == 8);
    CHECK(r->faces == 2);

    const std::string text = readAll(out);
    CHECK(countLinesStarting(text, "v ") == 8);
    CHECK(countLinesStarting(text, "vt ") == 8);
    // One group per mesh, named -- otherwise a DCC tool imports one merged blob
    // and the clothes cannot be selected separately.
    CHECK(countLinesStarting(text, "g ") == 2);
    CHECK(text.find("g body") != std::string::npos);
    CHECK(text.find("g eyes") != std::string::npos);
}

// OBJ indices are file-global and 1-based, so the second mesh's faces must be
// offset by the first mesh's vertex count. Getting this wrong is the classic
// multi-mesh OBJ bug: every mesh after the first draws the first one's
// geometry.
TEST_CASE("the second mesh's indices are offset past the first", "[io][objscene]") {
    const Quad a   = quadAt(0.0F);
    const Quad b   = quadAt(4.0F);
    const auto out = tmp("mh_scene_offset.obj");
    fs::remove(out);

    REQUIRE(io::writeObjScene(out, {{{a.view(), "a", nullptr, {}}, {b.view(), "b", nullptr, {}}}})
                .has_value());

    const std::string text = readAll(out);
    // The last face line must reference vertices 5..8, not 1..4.
    std::istringstream in(text);
    std::string lastFace;
    for (std::string line; std::getline(in, line);) {
        if (line.rfind("f ", 0) == 0) lastFace = line;
    }
    INFO("last face line: " << lastFace);
    CHECK(lastFace.find("5/5") != std::string::npos);
    CHECK(lastFace.find("8/8") != std::string::npos);
}

// The weakest part of the offset arithmetic: entries that do not carry the same
// attributes. Normals and UVs advance their own counters, so a mesh with no
// normals must not consume a normal slot -- otherwise every later mesh reads
// the wrong normals, which no vertex-count check would notice.
TEST_CASE("mixed attributes keep their own index counters", "[io][objscene]") {
    Quad withUv = quadAt(0.0F);  // UVs, no normals
    Quad both   = quadAt(4.0F);  // UVs and normals
    std::vector<foundation::Vec3> normals(4, foundation::Vec3{0, 0, 1});

    foundation::MeshView bothView = both.view();
    bothView.vnorm                = normals;

    const auto out = tmp("mh_scene_mixed.obj");
    fs::remove(out);
    REQUIRE(io::writeObjScene(out, {{{withUv.view(), "nonormals", nullptr, {}},
                                     {bothView, "withnormals", nullptr, {}}}})
                .has_value());

    const std::string text = readAll(out);
    // Only the second entry contributes normals, so there is exactly one set.
    CHECK(countLinesStarting(text, "vn ") == 4);
    CHECK(countLinesStarting(text, "v ") == 8);
    CHECK(countLinesStarting(text, "vt ") == 8);

    std::istringstream in(text);
    std::vector<std::string> faces;
    for (std::string line; std::getline(in, line);) {
        if (line.rfind("f ", 0) == 0) faces.push_back(line);
    }
    REQUIRE(faces.size() == 2);
    INFO("face 1: " << faces[0] << "\nface 2: " << faces[1]);

    // The first mesh has no normals, so its corners are v/vt with no third field.
    CHECK(faces[0].find("//") == std::string::npos);
    CHECK(faces[0] == "f 1/1 2/2 3/3 4/4");
    // The second mesh's vertices and UVs are offset past the first (5..8), but
    // its normals start at 1 -- nothing has written a normal before it.
    CHECK(faces[1] == "f 5/5/1 6/6/2 7/7/3 8/8/4");
}

// OBJ has no "no material" state -- `usemtl` stays in effect once emitted -- so
// a material-less entry after a materialled one would be textured as the first.
// Refused rather than written wrong.
TEST_CASE("mixing materialled and material-less entries is refused", "[io][objscene]") {
    const Quad a = quadAt(0.0F);
    const Quad b = quadAt(4.0F);
    foundation::MaterialDesc skin;
    skin.name = "Skin";

    const auto out = tmp("mh_scene_mixedmat.obj");
    fs::remove(out);
    const auto r =
        io::writeObjScene(out, {{{a.view(), "a", &skin, {}}, {b.view(), "b", nullptr, {}}}});
    REQUIRE_FALSE(r.has_value());
    CHECK(r.error().detail.find("no material") != std::string::npos);
}

// Two entries sharing one material must write a single newmtl block; two
// DIFFERENT materials sharing a name would silently lose one, since consumers
// keep the last block they read.
TEST_CASE("materials are deduplicated by name", "[io][objscene]") {
    const Quad a = quadAt(0.0F);
    const Quad b = quadAt(4.0F);
    foundation::MaterialDesc skin;
    skin.name = "Skin";

    const auto out = tmp("mh_scene_sharedmat.obj");
    const auto mtl = tmp("mh_scene_sharedmat.mtl");
    fs::remove(out);
    fs::remove(mtl);
    REQUIRE(io::writeObjScene(out, {{{a.view(), "a", &skin, {}}, {b.view(), "b", &skin, {}}}})
                .has_value());
    CHECK(countLinesStarting(readAll(mtl), "newmtl ") == 1);

    foundation::MaterialDesc clash;
    clash.name     = "Skin";  // same name, different material
    clash.opacity  = 0.5F;
    const auto bad = tmp("mh_scene_clash.obj");
    fs::remove(bad);
    const auto r =
        io::writeObjScene(bad, {{{a.view(), "a", &skin, {}}, {b.view(), "b", &clash, {}}}});
    REQUIRE_FALSE(r.has_value());
    CHECK(r.error().detail.find("both named") != std::string::npos);
}

// Independent confirmation: our writer's output read back by assimp, which
// knows nothing about our code. It reports the mesh count from the file.
TEST_CASE("assimp reads back both meshes", "[io][objscene]") {
    const Quad a   = quadAt(0.0F);
    const Quad b   = quadAt(4.0F);
    const auto out = tmp("mh_scene_assimp.obj");
    fs::remove(out);

    REQUIRE(io::writeObjScene(out, {{{a.view(), "a", nullptr, {}}, {b.view(), "b", nullptr, {}}}})
                .has_value());

    const auto back = io::importMesh(out);
    REQUIRE(back.has_value());
    CHECK(back->meshCount == 2);
}

// A one-entry scene must be byte-identical to what writeObj produces, so the
// single-mesh path cannot quietly regress while the new one is exercised.
TEST_CASE("a one-mesh scene matches the single-mesh writer", "[io][objscene]") {
    const Quad q      = quadAt(0.0F);
    const auto viaOne = tmp("mh_scene_one.obj");
    const auto viaOld = tmp("mh_scene_old.obj");
    fs::remove(viaOne);
    fs::remove(viaOld);

    io::ObjWriteOptions opts;
    opts.objectName = "mesh";
    REQUIRE(io::writeObj(viaOld, q.view(), opts).has_value());
    REQUIRE(io::writeObjScene(viaOne, {{{q.view(), "mesh", nullptr, {}}}}).has_value());

    CHECK(readAll(viaOne) == readAll(viaOld));
}

// A .mtl that names a texture nobody copied is a broken file. The writer emits
// `map_Kd <name>`, so the file has to be there -- the reference copies it
// (legacy/python/shared/wavefront.py:278). Before materials reached the OBJ
// path no .mtl existed at all, so this became reachable only once a dressed
// export carried real materials.
TEST_CASE("a referenced texture is copied next to the OBJ", "[io][objscene]") {
    const Quad q = quadAt(0.0F);

    // The source must live in a DIFFERENT directory from the output, or
    // "does the texture exist beside the .mtl" finds the source and passes
    // whether or not anything was copied. (It did, before this was fixed.)
    const auto texDir = fs::temp_directory_path() / "mh_scene_tex_src";
    fs::create_directories(texDir);
    const auto texSrc = texDir / "mh_scene_tex_src.png";
    {
        std::ofstream out(texSrc, std::ios::binary);
        out << "not really a png, but a real file";
    }

    foundation::MaterialDesc mat;
    mat.name           = "Textured";
    mat.diffuseTexture = texSrc;

    const auto out = tmp("mh_scene_tex.obj");
    const auto mtl = tmp("mh_scene_tex.mtl");
    fs::remove(out);
    fs::remove(mtl);

    REQUIRE(io::writeObjScene(out, {{{q.view(), "q", &mat, {}}}}).has_value());

    const std::string text = readAll(mtl);
    INFO(text);
    REQUIRE(text.find("map_Kd ") != std::string::npos);

    // Whatever name the .mtl gives must resolve beside the .mtl itself.
    const auto at           = text.find("map_Kd ") + 7;
    const auto end          = text.find('\n', at);
    const std::string named = text.substr(at, end - at);
    INFO("map_Kd names: " << named);
    CHECK(fs::exists(mtl.parent_path() / named));

    std::error_code ec;
    fs::remove(out, ec);
    fs::remove(mtl, ec);
    fs::remove_all(texDir, ec);
    fs::remove(mtl.parent_path() / texSrc.filename(), ec);
}

// The strongest statement of the same defect: **our own reader refused our own
// export.** `loadObj` rejects a vertex no face references (`LooseVertex`), and
// a masked export declared 5,778 of them, so
// `makehuman --export x.obj` produced a file the application could not reopen:
//
//     vertex referenced by no face (vertex 13380)
//
// The file-size win is real but secondary; this is the part that made the
// export wrong rather than merely wasteful.
TEST_CASE("a masked OBJ can be read back by our own reader", "[io][objscene][compact]") {
    const Quad a   = quadAt(0.0F);
    const Quad b   = quadAt(4.0F);
    const auto out = tmp("mh_scene_readback.obj");
    fs::remove(out);

    const std::array<uint8_t, 1> keep{1};
    const std::array<uint8_t, 1> hide{0};
    const std::vector<io::ObjSceneEntry> scene{
        {a.view(), "body", nullptr, keep},
        {b.view(), "eyes", nullptr, hide},
    };
    REQUIRE(io::writeObjScene(out, scene).has_value());

    const auto back = core::loadObj(out);
    if (!back) INFO(back.error().message());
    REQUIRE(back.has_value());
    CHECK(back->vertexCount() == 4);
    CHECK(back->faceCount() == 1);

    fs::remove(out);
}

// The OBJ carried the same dead weight the render-vertex path did, by a
// different mechanism. Measured on `makehuman --export x.obj`: **20,222 `v`
// lines of which 14,444 were referenced** -- 5,778 dead, 28.6% -- because the
// face mask skips FACES while the vertex and UV lists were written whole.
//
// The assertion is reachability, not a count: every `v` and `vt` the file
// declares must be named by some `f`, and the faces must still be the same
// faces. A writer that dropped too many would fail the second half by
// renumbering into the wrong vertices.
TEST_CASE("a masked OBJ declares no vertex its faces never name", "[io][objscene][compact]") {
    // Two quads sharing nothing; the mask hides the second, so its four
    // vertices and four UVs must not be written at all.
    const Quad a   = quadAt(0.0F);
    const Quad b   = quadAt(4.0F);
    const auto out = tmp("mh_scene_masked.obj");
    fs::remove(out);

    const std::array<uint8_t, 1> keep{1};
    const std::array<uint8_t, 1> hide{0};
    const std::vector<io::ObjSceneEntry> scene{
        {a.view(), "body", nullptr, keep},
        {b.view(), "eyes", nullptr, hide},
    };
    const auto r = io::writeObjScene(out, scene);
    REQUIRE(r.has_value());
    CHECK(r->faces == 1);
    CHECK(r->skipped == 1);
    CHECK(r->vertices == 4);  // not 8

    const std::string text = readAll(out);
    CHECK(countLinesStarting(text, "v ") == 4);
    CHECK(countLinesStarting(text, "vt ") == 4);

    // ...and the surviving face must still name vertices that exist. Dropping
    // too many would renumber into the wrong ones and show up here.
    size_t maxV = 0;
    std::istringstream lines(text);
    for (std::string line; std::getline(lines, line);) {
        if (line.rfind("f ", 0) != 0) continue;
        std::istringstream in(line.substr(2));
        for (std::string tok; in >> tok;) {
            const size_t slash = tok.find('/');
            maxV = std::max(maxV, static_cast<size_t>(std::stoul(tok.substr(0, slash))));
        }
    }
    CHECK(maxV == 4);
}
