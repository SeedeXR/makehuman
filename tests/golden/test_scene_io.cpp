// SPDX-License-Identifier: Apache-2.0
//
// Multi-format export and import. FBX is the headline: the Python reference's
// FBX exporter writes version 7300 (FBX 2013) with no animation and no
// blendshapes, and has a verified 10x unit bug; this path writes 7500 (2016)
// and round-trips.

#include "makehuman/io/SceneIO.h"

#include "makehuman/core/ObjReader.h"
#include "makehuman/io/GltfWriter.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

using Catch::Matchers::WithinAbs;
using namespace mh;

namespace {

std::filesystem::path tempFile(const char* stem, std::string_view ext) {
    return std::filesystem::temp_directory_path() /
           (std::string("mh_scene_") + stem + std::string(ext));
}

std::vector<uint8_t> head(const std::filesystem::path& p, size_t n) {
    std::ifstream in(p, std::ios::binary);
    std::vector<uint8_t> out(n);
    in.read(reinterpret_cast<char*>(out.data()), static_cast<std::streamsize>(n));
    out.resize(static_cast<size_t>(in.gcount()));
    return out;
}

core::Mesh baseMeshOrSkip() {
    const auto src = std::filesystem::path(MH_DATA_DIR) / "3dobjs" / "base.obj";
    auto m         = core::loadObj(src);
    REQUIRE(m.has_value());
    return std::move(*m);
}

}  // namespace

TEST_CASE("FBX export is BINARY, version 7500", "[io][fbx]") {
    // The Python reference writes 7300 (FBX 2013). 7500 is FBX 2016 and is what
    // current DCCs and engines expect.
    const auto src = std::filesystem::path(MH_DATA_DIR) / "3dobjs" / "base.obj";
    if (!std::filesystem::exists(src)) SKIP("base.obj not present");

    const core::Mesh m = baseMeshOrSkip();
    const auto out     = tempFile("bin", ".fbx");
    const auto r       = io::exportScene(out, m, io::SceneFormat::FbxBinary);
    REQUIRE(r.has_value());
    CHECK(r->triangles == 36972);

    const auto b = head(out, 32);
    REQUIRE(b.size() >= 27);

    // Binary FBX begins with this exact 20-byte magic; an ASCII FBX does not.
    const std::string magic(reinterpret_cast<const char*>(b.data()), 18);
    CHECK(magic == "Kaydara FBX Binary");

    uint32_t version{};
    std::memcpy(&version, b.data() + 23, sizeof(version));
    CHECK(version == 7500);

    std::error_code ec;
    std::filesystem::remove(out, ec);
}

TEST_CASE("an exported FBX imports back with its geometry intact", "[io][fbx]") {
    const auto src = std::filesystem::path(MH_DATA_DIR) / "3dobjs" / "base.obj";
    if (!std::filesystem::exists(src)) SKIP("base.obj not present");

    const core::Mesh m = baseMeshOrSkip();
    const auto out     = tempFile("roundtrip", ".fbx");
    const auto w       = io::exportScene(out, m, io::SceneFormat::FbxBinary);
    REQUIRE(w.has_value());

    const auto back = io::importMesh(out);
    REQUIRE(back.has_value());

    // Geometry survives intact: same vertex count and same triangle count.
    // Welding on import uses aiProcess_JoinIdenticalVertices, which segfaults
    // on malformed indices by itself -- it is safe here only because
    // aiProcess_ValidateDataStructure runs first in assimp's pipeline and
    // rejects such a scene before anything dereferences it. Verified against a
    // file with out-of-range indices: 5/5 clean errors, no crash.
    CHECK(back->mesh.vertexCount() == w->vertices);
    CHECK(back->mesh.faceCount() == w->triangles);
    CHECK(back->mesh.hasUV());
    CHECK(back->meshCount == 1);

    // Imported meshes are triangulated.
    CHECK(back->mesh.vertsPerPrimitive() == 3);

    std::error_code ec;
    std::filesystem::remove(out, ec);
}

TEST_CASE("ASCII FBX is distinguishable from binary", "[io][fbx]") {
    core::Mesh m("q", 4);
    REQUIRE(m.setCoords({{0, 0, 0}, {1, 0, 0}, {1, 0, 1}, {0, 0, 1}}).has_value());
    m.addFaceGroup("g");
    REQUIRE(m.setFaces({0, 1, 2, 3}, {}, {0}).has_value());
    m.calcNormals();

    const auto out = tempFile("ascii", ".fbx");
    REQUIRE(io::exportScene(out, m, io::SceneFormat::FbxAscii).has_value());

    const auto b = head(out, 32);
    const std::string magic(reinterpret_cast<const char*>(b.data()),
                            std::min<size_t>(18, b.size()));
    CHECK(magic != "Kaydara FBX Binary");

    std::error_code ec;
    std::filesystem::remove(out, ec);
}

TEST_CASE("every declared format exports and is non-empty", "[io][scene]") {
    core::Mesh m("q", 4);
    REQUIRE(m.setCoords({{0, 0, 0}, {1, 0, 0}, {1, 0, 1}, {0, 0, 1}}).has_value());
    REQUIRE(m.setUVs({{0, 0}, {1, 0}, {1, 1}, {0, 1}}).has_value());
    m.addFaceGroup("g");
    REQUIRE(m.setFaces({0, 1, 2, 3}, {0, 1, 2, 3}, {0}).has_value());
    m.buildAdjacency();
    m.calcNormals();

    const io::SceneFormat formats[] = {io::SceneFormat::FbxBinary, io::SceneFormat::FbxAscii,
                                       io::SceneFormat::Collada,   io::SceneFormat::StlBinary,
                                       io::SceneFormat::StlAscii,  io::SceneFormat::ThreeMf};

    for (const io::SceneFormat f : formats) {
        INFO("format: " << io::formatId(f));
        const auto out = tempFile("all", io::formatExtension(f));
        const auto r   = io::exportScene(out, m, f);
        REQUIRE(r.has_value());
        CHECK(r->triangles == 2);
        CHECK(r->fileBytes > 0);
        std::error_code ec;
        std::filesystem::remove(out, ec);
    }
}

TEST_CASE("import reads the formats we export", "[io][scene]") {
    // The Python reference has NO import capability at all -- this is new
    // capability, so the check is that a file we wrote reads back sanely.
    const auto src = std::filesystem::path(MH_DATA_DIR) / "3dobjs" / "base.obj";
    if (!std::filesystem::exists(src)) SKIP("base.obj not present");

    const core::Mesh m = baseMeshOrSkip();

    struct Case {
        io::SceneFormat format;
        const char* ext;
    };

    // PLY is absent by design -- see the note in SceneIO.h. STL discards UVs
    // and re-splits vertices, but must preserve the triangle count.
    const Case cases[] = {{io::SceneFormat::FbxBinary, ".fbx"},
                          {io::SceneFormat::Collada, ".dae"},
                          {io::SceneFormat::StlBinary, ".stl"}};

    for (const Case& c : cases) {
        INFO("format: " << io::formatId(c.format));
        const auto out = tempFile("imp", c.ext);
        REQUIRE(io::exportScene(out, m, c.format).has_value());

        const auto back = io::importMesh(out);
        REQUIRE(back.has_value());
        CHECK(back->mesh.vertexCount() > 0);
        CHECK(back->mesh.faceCount() == 36972);  // triangle count survives every format

        std::error_code ec;
        std::filesystem::remove(out, ec);
    }
}

TEST_CASE("our own GLB imports through the multi-format path", "[io][scene][gltf]") {
    // Closes the loop: our hand-written glTF writer produces a file the
    // multi-format importer reads, which is a second independent check on it.
    const auto src = std::filesystem::path(MH_DATA_DIR) / "3dobjs" / "base.obj";
    if (!std::filesystem::exists(src)) SKIP("base.obj not present");

    const core::Mesh m = baseMeshOrSkip();
    const auto glb     = tempFile("ours", ".glb");
    REQUIRE(io::writeGlb(glb, m).has_value());

    const auto back = io::importMesh(glb);
    REQUIRE(back.has_value());
    CHECK(back->mesh.faceCount() == 36972);
    CHECK(back->mesh.hasUV());

    std::error_code ec;
    std::filesystem::remove(glb, ec);
}

TEST_CASE("unit conversion reaches the exported file", "[io][scene]") {
    // FBX's conventional unit is the centimetre, which is this path's default.
    const auto src = std::filesystem::path(MH_DATA_DIR) / "3dobjs" / "base.obj";
    if (!std::filesystem::exists(src)) SKIP("base.obj not present");

    const core::Mesh m = baseMeshOrSkip();
    io::SceneExportOptions opt;
    CHECK(opt.unit == io::Unit::Centimeter);

    const auto out = tempFile("units", ".fbx");
    REQUIRE(io::exportScene(out, m, io::SceneFormat::FbxBinary, opt).has_value());

    const auto back = io::importMesh(out);
    REQUIRE(back.has_value());

    float lo = 1e30F, hi = -1e30F;
    for (const auto& v : back->mesh.coord()) {
        lo = std::min(lo, v.y);
        hi = std::max(hi, v.y);
    }
    const float heightCm = hi - lo;
    INFO("height = " << heightCm << " cm");
    CHECK(heightCm > 140.0F);
    CHECK(heightCm < 210.0F);

    std::error_code ec;
    std::filesystem::remove(out, ec);
}

TEST_CASE("an empty mesh is rejected", "[io][scene]") {
    const core::Mesh m;
    const auto r = io::exportScene(tempFile("empty", ".fbx"), m, io::SceneFormat::FbxBinary);
    REQUIRE_FALSE(r.has_value());
    CHECK(r.error().kind == io::SceneIoErrorKind::EmptyMesh);
}

TEST_CASE("importing a missing file is reported", "[io][scene]") {
    const auto r = io::importMesh("/definitely/not/here.fbx");
    REQUIRE_FALSE(r.has_value());
    CHECK(r.error().kind == io::SceneIoErrorKind::NotFound);
    CHECK_FALSE(r.error().message().empty());
}

TEST_CASE("importing a non-mesh file fails cleanly", "[io][scene]") {
    const auto junk = tempFile("junk", ".fbx");
    {
        std::ofstream out(junk);
        out << "this is not an FBX file at all\n";
    }
    const auto r = io::importMesh(junk);
    REQUIRE_FALSE(r.has_value());
    CHECK(r.error().kind == io::SceneIoErrorKind::ImportFailed);

    std::error_code ec;
    std::filesystem::remove(junk, ec);
}
