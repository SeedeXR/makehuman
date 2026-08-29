// SPDX-License-Identifier: Apache-2.0
//
// glTF/GLB export. Unlike every other format in this port there is NO reference
// implementation to compare against -- the Python MakeHuman has no glTF support
// at all -- so this is validated two ways instead:
//
//   1. Spec conformance, checked directly against the bytes: GLB magic,
//      version, chunk types and the 4-byte chunk alignment the spec requires.
//   2. An INDEPENDENT reader. assimp is a different implementation by different
//      authors; if it agrees on the counts and bounds, the file is not merely
//      self-consistent.

#include "makehuman/core/RenderMesh.h"
#include "makehuman/io/GltfWriter.h"
#include "makehuman/rig/Skeleton.h"
#include "makehuman/rig/Skinning.h"
#include "makehuman/rig/VertexWeights.h"

#include "makehuman/core/ObjReader.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#if defined(MH_HAVE_ASSIMP)
#include <assimp/postprocess.h>
#include <assimp/scene.h>
#include <assimp/Importer.hpp>
#endif

using Catch::Matchers::WithinAbs;
using namespace mh;

namespace {

std::vector<uint8_t> readFile(const std::filesystem::path& p) {
    std::ifstream in(p, std::ios::binary | std::ios::ate);
    if (!in) return {};
    const auto n = static_cast<size_t>(in.tellg());
    in.seekg(0);
    std::vector<uint8_t> out(n);
    in.read(reinterpret_cast<char*>(out.data()), static_cast<std::streamsize>(n));
    return out;
}

uint32_t readU32(const std::vector<uint8_t>& b, size_t off) {
    uint32_t v{};
    std::memcpy(&v, b.data() + off, sizeof(v));
    return v;  // the format is little-endian, and so is every target we build for
}

std::filesystem::path tempGlb(const char* stem) {
    return std::filesystem::temp_directory_path() / (std::string("mh_glb_") + stem + ".glb");
}

core::Mesh quad() {
    core::Mesh m("quad", 4);
    REQUIRE(m.setCoords({{0, 0, 0}, {2, 0, 0}, {2, 0, 3}, {0, 0, 3}}).has_value());
    REQUIRE(m.setUVs({{0, 0}, {1, 0}, {1, 1}, {0, 1}}).has_value());
    m.addFaceGroup("g");
    REQUIRE(m.setFaces({0, 1, 2, 3}, {0, 1, 2, 3}, {0}).has_value());
    m.buildAdjacency();
    m.calcNormals();
    return m;
}

}  // namespace

TEST_CASE("the GLB container conforms to the spec", "[io][gltf]") {
    const auto out = tempGlb("header");
    const auto m   = quad();
    REQUIRE(io::writeGlb(out, core::RenderMesh::build(m).view()).has_value());

    const auto b = readFile(out);
    REQUIRE(b.size() >= 28);

    CHECK(readU32(b, 0) == 0x46546C67);  // "glTF"
    CHECK(readU32(b, 4) == 2);           // version 2
    CHECK(readU32(b, 8) == b.size());    // total length includes the header

    const uint32_t jsonLen = readU32(b, 12);
    CHECK(readU32(b, 16) == 0x4E4F534A);  // "JSON"
    CHECK(jsonLen % 4 == 0);              // chunks are 4-byte aligned

    const size_t binHeader = 20 + jsonLen;
    const uint32_t binLen  = readU32(b, binHeader);
    CHECK(readU32(b, binHeader + 4) == 0x004E4942);  // "BIN\0"
    CHECK(binLen % 4 == 0);

    // The two chunks plus their headers must account for the whole file.
    CHECK(12 + 8 + jsonLen + 8 + binLen == b.size());

    // JSON padding is spaces, not NULs (spec requirement).
    const std::string json(reinterpret_cast<const char*>(b.data()) + 20, jsonLen);
    CHECK(json.front() == '{');
    CHECK((json.back() == '}' || json.back() == ' '));

    std::error_code ec;
    std::filesystem::remove(out, ec);
}

TEST_CASE("the JSON declares the accessors glTF requires", "[io][gltf]") {
    const auto out = tempGlb("json");
    const auto m   = quad();
    REQUIRE(io::writeGlb(out, core::RenderMesh::build(m).view()).has_value());

    const auto b           = readFile(out);
    const uint32_t jsonLen = readU32(b, 12);
    const std::string json(reinterpret_cast<const char*>(b.data()) + 20, jsonLen);

    CHECK(json.find("\"version\":\"2.0\"") != std::string::npos);
    CHECK(json.find("\"POSITION\"") != std::string::npos);
    CHECK(json.find("\"NORMAL\"") != std::string::npos);
    CHECK(json.find("\"TEXCOORD_0\"") != std::string::npos);
    CHECK(json.find("\"mode\":4") != std::string::npos);  // TRIANGLES
    CHECK(json.find("\"pbrMetallicRoughness\"") != std::string::npos);
    // POSITION accessors MUST carry min and max; many loaders reject the file
    // without them.
    CHECK(json.find("\"min\":[") != std::string::npos);
    CHECK(json.find("\"max\":[") != std::string::npos);

    std::error_code ec;
    std::filesystem::remove(out, ec);
}

TEST_CASE("quads are triangulated for export", "[io][gltf]") {
    // glTF has no quad primitive.
    const auto out = tempGlb("tris");
    const auto m   = quad();
    const auto r   = io::writeGlb(out, core::RenderMesh::build(m).view());
    REQUIRE(r.has_value());
    CHECK(r->triangles == 2);  // one quad -> two triangles
    CHECK(r->vertices == 4);

    std::error_code ec;
    std::filesystem::remove(out, ec);
}

TEST_CASE("the default unit is metres, not decimetres", "[io][gltf]") {
    // glTF's unit is the metre and MakeHuman's is the decimetre. Writing
    // decimetres would make every model ten times too large in any engine that
    // honours the spec.
    io::GltfWriteOptions opt;
    CHECK(opt.unit == io::Unit::Meter);
    CHECK_THAT(io::unitScale(opt.unit), WithinAbs(0.1, 1e-6));
}

TEST_CASE("an empty mesh is rejected", "[io][gltf]") {
    const core::Mesh m;
    const auto r = io::writeGlb(tempGlb("empty"), core::RenderMesh::build(m).view());
    REQUIRE_FALSE(r.has_value());
    CHECK(r.error().kind == io::GltfWriteErrorKind::EmptyMesh);
    CHECK_FALSE(r.error().message().empty());
}

#if defined(MH_HAVE_ASSIMP)
TEST_CASE("an independent library reads the exported GLB", "[io][gltf][assimp]") {
    const auto src = std::filesystem::path(MH_DATA_DIR) / "3dobjs" / "base.obj";
    if (!std::filesystem::exists(src)) SKIP("base.obj not present");

    const auto mesh = core::loadObj(src);
    REQUIRE(mesh.has_value());

    const auto out = tempGlb("assimp");
    const auto r   = io::writeGlb(out, core::RenderMesh::build(*mesh).view());
    REQUIRE(r.has_value());

    Assimp::Importer importer;
    const aiScene* scene = importer.ReadFile(out.string(), 0);
    INFO("assimp: " << importer.GetErrorString());
    REQUIRE(scene != nullptr);
    REQUIRE(scene->mNumMeshes == 1);

    const aiMesh* am = scene->mMeshes[0];
    CHECK(am->mNumVertices == r->vertices);
    CHECK(am->mNumFaces == r->triangles);
    CHECK(am->HasNormals());
    CHECK(am->HasTextureCoords(0));
    CHECK(scene->mNumMaterials >= 1);

    // Every face must be a triangle.
    size_t nonTriangles = 0;
    for (unsigned i = 0; i < am->mNumFaces; ++i) {
        if (am->mFaces[i].mNumIndices != 3) ++nonTriangles;
    }
    CHECK(nonTriangles == 0);

    std::error_code ec;
    std::filesystem::remove(out, ec);
}

TEST_CASE("the exported model is metre-scaled and human-sized", "[io][gltf][assimp]") {
    // A unit error is the single most common export bug and the easiest to
    // miss, so assert real-world plausibility through the independent reader.
    const auto src = std::filesystem::path(MH_DATA_DIR) / "3dobjs" / "base.obj";
    if (!std::filesystem::exists(src)) SKIP("base.obj not present");

    const auto mesh = core::loadObj(src);
    REQUIRE(mesh.has_value());
    const auto out = tempGlb("scale");
    REQUIRE(io::writeGlb(out, core::RenderMesh::build(*mesh).view()).has_value());

    Assimp::Importer importer;
    const aiScene* scene = importer.ReadFile(out.string(), 0);
    REQUIRE(scene != nullptr);
    const aiMesh* am = scene->mMeshes[0];

    float lo = 1e30F, hi = -1e30F;
    for (unsigned i = 0; i < am->mNumVertices; ++i) {
        lo = std::min(lo, am->mVertices[i].y);
        hi = std::max(hi, am->mVertices[i].y);
    }
    const float heightMetres = hi - lo;
    INFO("height = " << heightMetres << " m");
    CHECK(heightMetres > 1.4F);
    CHECK(heightMetres < 2.1F);

    std::error_code ec;
    std::filesystem::remove(out, ec);
}

TEST_CASE("feet on ground places the model at y = 0", "[io][gltf][assimp]") {
    const auto src = std::filesystem::path(MH_DATA_DIR) / "3dobjs" / "base.obj";
    if (!std::filesystem::exists(src)) SKIP("base.obj not present");

    const auto mesh = core::loadObj(src);
    REQUIRE(mesh.has_value());

    io::GltfWriteOptions opt;
    opt.feetOnGround = true;
    const auto out   = tempGlb("ground");
    REQUIRE(io::writeGlb(out, core::RenderMesh::build(*mesh).view(), opt).has_value());

    Assimp::Importer importer;
    const aiScene* scene = importer.ReadFile(out.string(), 0);
    REQUIRE(scene != nullptr);
    const aiMesh* am = scene->mMeshes[0];

    float lo = 1e30F;
    for (unsigned i = 0; i < am->mNumVertices; ++i)
        lo = std::min(lo, am->mVertices[i].y);
    CHECK_THAT(lo, WithinAbs(0.0, 1e-4));

    std::error_code ec;
    std::filesystem::remove(out, ec);
}
#endif

// ---------------------------------------------------------------- skinning

namespace {

/// Parses the GLB's JSON chunk, so the structure can be asserted without a
/// glTF library. The chunk layout is checked by the tests above.
std::string glbJson(const std::filesystem::path& p) {
    std::ifstream in(p, std::ios::binary);
    REQUIRE(in);
    const std::string bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    const auto start = bytes.find('{');
    REQUIRE(start != std::string::npos);
    // The JSON chunk is padded with spaces to a 4-byte boundary; trim them.
    auto end = bytes.rfind('}');
    REQUIRE(end != std::string::npos);
    return bytes.substr(start, end - start + 1);
}

/// The first float of the array that follows @p marker.
float firstFloatAfter(const std::string& j, const std::string& marker) {
    const auto at = j.find(marker);
    if (at == std::string::npos) return 0.0F;
    return std::strtof(j.c_str() + at + marker.size(), nullptr);
}

/// The @p n-th (0-based) float of the array that follows @p marker.
float nthFloatAfter(const std::string& j, const std::string& marker, size_t n) {
    auto at = j.find(marker);
    if (at == std::string::npos) return 0.0F;
    const char* p = j.c_str() + at + marker.size();
    float v       = 0.0F;
    for (size_t i = 0; i <= n; ++i) {
        char* end = nullptr;
        v         = std::strtof(p, &end);
        if (end == p) return 0.0F;
        p = end;
        while (*p == ',' || *p == ' ')
            ++p;
    }
    return v;
}

struct RiggedFixture {
    core::Mesh mesh;
    rig::Skeleton skel;
    rig::SkinData skin;
};

RiggedFixture buildRigged() {
    auto mesh = core::loadObj(std::filesystem::path(MH_DATA_DIR) / "3dobjs" / "base.obj");
    REQUIRE(mesh.has_value());
    auto skel = rig::loadSkeleton(std::filesystem::path(MH_DATA_DIR) / "rigs" / "default.mhskel");
    REQUIRE(skel.has_value());
    REQUIRE(skel->updateJoints(mesh->coord()));
    REQUIRE(skel->buildRestMatrices());
    auto vw = rig::loadWeights(std::filesystem::path(MH_DATA_DIR) / "rigs" / "default_weights.mhw",
                               mesh->vertexCount());
    REQUIRE(vw.has_value());

    const auto rm       = core::RenderMesh::build(*mesh);
    const auto compiled = vw->compile(*skel, io::kGltfInfluences);
    auto skin           = rig::buildSkinData(*skel, compiled, rm.vmap());
    REQUIRE_FALSE(skin.jointNames.empty());
    return RiggedFixture{std::move(*mesh), std::move(*skel), std::move(skin)};
}

}  // namespace

// The weights are per MESH vertex (19,158) but glTF's JOINTS_0/WEIGHTS_0 are
// vertex ATTRIBUTES, so they must be per RENDER vertex (21,833). Skipping the
// expansion leaves everything past the first UV seam weighted to the wrong
// bone -- which looks like a rigging error, not an indexing one.
TEST_CASE("skin data is expanded onto the unwelded vertices", "[gltf][skin]") {
    auto f        = buildRigged();
    const auto rm = core::RenderMesh::build(f.mesh);

    REQUIRE(f.skin.globalRest.size() == 163);
    REQUIRE(f.skin.jointNames.size() == 163);
    CHECK(f.skin.view().vertexCount() == rm.vertexCount());
    CHECK(rm.vertexCount() > f.mesh.vertexCount());  // seams really did split

    // Every render vertex must carry the weights of the MESH vertex it came
    // from -- compared against the compiled source, not against itself.
    auto vw = rig::loadWeights(std::filesystem::path(MH_DATA_DIR) / "rigs" / "default_weights.mhw",
                               f.mesh.vertexCount());
    REQUIRE(vw.has_value());
    const auto compiled = vw->compile(f.skel, io::kGltfInfluences);

    size_t mismatched   = 0;
    size_t unnormalised = 0;
    for (size_t rv = 0; rv < rm.vertexCount(); ++rv) {
        const uint32_t mv = rm.vmap()[rv];
        float sum         = 0.0F;
        for (size_t i = 0; i < 4; ++i) {
            if (f.skin.joints[rv * 4 + i] != compiled.boneIndex[mv * 4 + i]) ++mismatched;
            if (f.skin.weights[rv * 4 + i] != compiled.weight[mv * 4 + i]) ++mismatched;
            sum += f.skin.weights[rv * 4 + i];
        }
        // Sums to 1 proves the row was filled rather than left zero.
        if (std::abs(sum - 1.0F) > 1e-4F) ++unnormalised;
    }
    CHECK(mismatched == 0);
    CHECK(unnormalised == 0);
}

TEST_CASE("a rigged GLB carries a skin, joints and inverse binds", "[gltf][skin]") {
    auto f              = buildRigged();
    const auto rm       = core::RenderMesh::build(f.mesh);
    const auto skinView = f.skin.view();

    const auto out = std::filesystem::temp_directory_path() / "mh_rigged_test.glb";
    const auto r   = io::writeGlb(out, rm.view(), {}, nullptr, &skinView);
    REQUIRE(r.has_value());

    const std::string j = glbJson(out);
    CHECK(j.find("\"skins\":[") != std::string::npos);
    CHECK(j.find("\"inverseBindMatrices\":") != std::string::npos);
    CHECK(j.find("\"JOINTS_0\":") != std::string::npos);
    CHECK(j.find("\"WEIGHTS_0\":") != std::string::npos);
    CHECK(j.find("\"skin\":0") != std::string::npos);
    // 163 joint nodes plus the mesh node.
    CHECK(j.find("\"root\"") != std::string::npos);

    std::error_code ec;
    std::filesystem::remove(out, ec);
}

// The writer derives node transforms and inverse binds from ONE scaled array,
// so a unit change moves the rig with the mesh. If they drifted apart the model
// would import at the right size with a rig ten times too large.
TEST_CASE("joints scale with the mesh", "[gltf][skin]") {
    auto f              = buildRigged();
    const auto rm       = core::RenderMesh::build(f.mesh);
    const auto skinView = f.skin.view();

    const auto metres = std::filesystem::temp_directory_path() / "mh_rig_m.glb";
    const auto decis  = std::filesystem::temp_directory_path() / "mh_rig_dm.glb";

    io::GltfWriteOptions m;
    m.unit = io::Unit::Meter;
    io::GltfWriteOptions d;
    d.unit = io::Unit::Decimeter;

    REQUIRE(io::writeGlb(metres, rm.view(), m, nullptr, &skinView).has_value());
    REQUIRE(io::writeGlb(decis, rm.view(), d, nullptr, &skinView).has_value());

    const auto jm = glbJson(metres);
    const auto jd = glbJson(decis);
    CHECK(jm.find("\"skins\":[") != std::string::npos);
    CHECK(jd.find("\"skins\":[") != std::string::npos);

    // File SIZE proves nothing -- the numbers are text, so "0.5" and "5" differ
    // in length and the two files legitimately differ. Compare the geometry
    // instead: a decimetre export is exactly 10x a metre one, and the JOINTS
    // must scale by the same factor or the rig detaches from the body.
    const float meshM = firstFloatAfter(jm, "\"max\":[");
    const float meshD = firstFloatAfter(jd, "\"max\":[");
    REQUIRE(meshM > 0.0F);
    CHECK(std::abs(meshD / meshM - 10.0F) < 1e-3F);

    // The root joint's node matrix: glTF is column-major, so the translation is
    // elements 12..14 of the 16.
    const float jointM = nthFloatAfter(jm, "\"matrix\":[", 13);
    const float jointD = nthFloatAfter(jd, "\"matrix\":[", 13);
    REQUIRE(std::abs(jointM) > 1e-6F);
    CHECK(std::abs(jointD / jointM - 10.0F) < 1e-3F);

    std::error_code ec;
    std::filesystem::remove(metres, ec);
    std::filesystem::remove(decis, ec);
}

TEST_CASE("a skin that does not describe the mesh is refused", "[gltf][skin]") {
    auto f        = buildRigged();
    const auto rm = core::RenderMesh::build(f.mesh);

    // Wrong influence count.
    auto bad        = f.skin;
    bad.influences  = 3;
    const auto view = bad.view();
    const auto out  = std::filesystem::temp_directory_path() / "mh_badskin.glb";
    const auto r    = io::writeGlb(out, rm.view(), {}, nullptr, &view);
    REQUIRE_FALSE(r.has_value());
    CHECK(r.error().kind == io::GltfWriteErrorKind::InvalidSkin);

    // A joint index past the end of the skeleton.
    auto bad2      = f.skin;
    bad2.joints[0] = 9999;
    const auto v2  = bad2.view();
    const auto r2  = io::writeGlb(out, rm.view(), {}, nullptr, &v2);
    REQUIRE_FALSE(r2.has_value());
    CHECK(r2.error().kind == io::GltfWriteErrorKind::InvalidSkin);

    std::error_code ec;
    std::filesystem::remove(out, ec);
}
