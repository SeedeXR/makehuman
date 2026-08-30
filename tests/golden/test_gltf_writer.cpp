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
#include "makehuman/core/Target.h"
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

/// A quad translated along X, so two of them are distinguishable in one file.
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

// ----------------------------------------------------------- morph targets

namespace {

/// Loads a target and expands it onto the render vertices, as an exporter must.
std::vector<foundation::Vec3> expandedTarget(const core::Mesh& mesh, const core::RenderMesh& rm,
                                             const char* rel) {
    auto t = core::loadTarget(std::filesystem::path(MH_DATA_DIR) / "targets" / rel);
    REQUIRE(t.has_value());
    std::vector<foundation::Vec3> deltas;
    REQUIRE(core::expandTargetToRenderVertices(*t, rm.vmap(), mesh.vertexCount(), deltas));
    return deltas;
}

}  // namespace

// A target touches mesh vertices; glTF morph targets are per RENDER vertex. A
// UV seam maps several render vertices to one mesh vertex, and each must get
// the SAME delta or the seam tears open when the target is applied.
TEST_CASE("a target expands onto render vertices, seams included", "[gltf][morph]") {
    const auto mesh = core::loadObj(std::filesystem::path(MH_DATA_DIR) / "3dobjs" / "base.obj");
    REQUIRE(mesh.has_value());
    const auto rm = core::RenderMesh::build(*mesh);

    auto t = core::loadTarget(std::filesystem::path(MH_DATA_DIR) / "targets" / "head" /
                              "head-oval.target");
    REQUIRE(t.has_value());
    std::vector<foundation::Vec3> deltas;
    REQUIRE(core::expandTargetToRenderVertices(*t, rm.vmap(), mesh->vertexCount(), deltas));
    REQUIRE(deltas.size() == rm.vertexCount());

    // Build the sparse source as a lookup and confirm every render vertex got
    // exactly its mesh vertex's delta.
    std::vector<foundation::Vec3> perMesh(mesh->vertexCount(), foundation::Vec3{});
    for (size_t i = 0; i < t->verts.size(); ++i)
        perMesh[t->verts[i]] = t->offsets[i];

    size_t wrong = 0;
    size_t moved = 0;
    for (size_t rv = 0; rv < rm.vertexCount(); ++rv) {
        const auto& want = perMesh[rm.vmap()[rv]];
        const auto& got  = deltas[rv];
        if (got.x != want.x || got.y != want.y || got.z != want.z) ++wrong;
        if (want.x != 0.0F || want.y != 0.0F || want.z != 0.0F) ++moved;
    }
    CHECK(wrong == 0);
    // Seams duplicate, so more render vertices move than mesh vertices.
    CHECK(moved > t->verts.size());
}

TEST_CASE("a morphed GLB carries targets, names and default weights", "[gltf][morph]") {
    const auto mesh = core::loadObj(std::filesystem::path(MH_DATA_DIR) / "3dobjs" / "base.obj");
    REQUIRE(mesh.has_value());
    const auto rm = core::RenderMesh::build(*mesh);

    const auto a = expandedTarget(*mesh, rm, "head/head-oval.target");
    const auto b = expandedTarget(*mesh, rm, "nose/nose-base-up.target");
    const std::vector<foundation::MorphTarget> morphs{{"head-oval", a}, {"nose-base-up", b}};

    const auto out = std::filesystem::temp_directory_path() / "mh_morph_test.glb";
    REQUIRE(io::writeGlb(out, rm.view(), {}, nullptr, nullptr, morphs).has_value());

    const std::string j = glbJson(out);
    CHECK(j.find("\"targets\":[") != std::string::npos);
    CHECK(j.find("\"weights\":[0,0]") != std::string::npos);
    // targetNames is what a DCC reads to label the shape keys; without it they
    // import as "Key 1", "Key 2" and become unusable.
    CHECK(j.find("\"targetNames\":[\"head-oval\",\"nose-base-up\"]") != std::string::npos);

    std::error_code ec;
    std::filesystem::remove(out, ec);
}

// The spec requires min/max on every POSITION accessor, and a morph target's
// deltas are one. Omitting it is the most common way a hand-written morph
// export fails validation while still loading in some engines.
TEST_CASE("morph target accessors carry min and max", "[gltf][morph]") {
    const auto mesh = core::loadObj(std::filesystem::path(MH_DATA_DIR) / "3dobjs" / "base.obj");
    REQUIRE(mesh.has_value());
    const auto rm = core::RenderMesh::build(*mesh);
    const auto a  = expandedTarget(*mesh, rm, "head/head-oval.target");
    const std::vector<foundation::MorphTarget> morphs{{"head-oval", a}};

    const auto out = std::filesystem::temp_directory_path() / "mh_morph_minmax.glb";
    REQUIRE(io::writeGlb(out, rm.view(), {}, nullptr, nullptr, morphs).has_value());

    const std::string j = glbJson(out);
    // POSITION (the base mesh) plus one morph target: two accessors with min.
    size_t mins = 0;
    for (size_t at = j.find("\"min\":["); at != std::string::npos; at = j.find("\"min\":[", at + 1))
        ++mins;
    CHECK(mins == 2);

    std::error_code ec;
    std::filesystem::remove(out, ec);
}

TEST_CASE("a morph target of the wrong length is refused", "[gltf][morph]") {
    const auto mesh = core::loadObj(std::filesystem::path(MH_DATA_DIR) / "3dobjs" / "base.obj");
    REQUIRE(mesh.has_value());
    const auto rm = core::RenderMesh::build(*mesh);

    const std::vector<foundation::Vec3> tooShort(10, foundation::Vec3{});
    const std::vector<foundation::MorphTarget> bad{{"short", tooShort}};

    const auto out = std::filesystem::temp_directory_path() / "mh_morph_bad.glb";
    const auto r   = io::writeGlb(out, rm.view(), {}, nullptr, nullptr, bad);
    REQUIRE_FALSE(r.has_value());
    CHECK(r.error().kind == io::GltfWriteErrorKind::InvalidMorphTarget);

    std::error_code ec;
    std::filesystem::remove(out, ec);
}

// The .target files contain literal (0,0,0) rows -- nose-base-up has 11 of 305.
// They are no-ops, and the count only matters because it explains why Blender
// reports fewer moved vertices than the file has rows.
TEST_CASE("zero-offset rows in a target are no-ops", "[gltf][morph]") {
    auto t = core::loadTarget(std::filesystem::path(MH_DATA_DIR) / "targets" / "nose" /
                              "nose-base-up.target");
    REQUIRE(t.has_value());
    CHECK(t->verts.size() == 305);

    size_t zero = 0;
    for (const auto& o : t->offsets) {
        if (o.x == 0.0F && o.y == 0.0F && o.z == 0.0F) ++zero;
    }
    CHECK(zero == 11);  // 305 - 11 = 294, which is what Blender reports moving
}

// A dressed character is several meshes with their own materials. Until this
// worked, exporting to glTF wrote the body alone -- the format that matters
// most for DCC round-tripping.
TEST_CASE("several meshes are written into one GLB", "[io][gltf][multimesh]") {
    const core::Mesh a = quadAt(0.0F, "body");
    const core::Mesh b = quadAt(8.0F, "eyes");
    const auto rmA     = core::RenderMesh::build(a);
    const auto rmB     = core::RenderMesh::build(b);
    const auto out     = tempGlb("scene");

    const std::vector<io::GltfSceneEntry> scene{{rmA.view(), "body"}, {rmB.view(), "eyes"}};
    const auto r = io::writeGlbScene(out, scene);
    REQUIRE(r.has_value());
    CHECK(r->vertices == rmA.view().vertexCount() + rmB.view().vertexCount());
    CHECK(r->triangles == rmA.view().triangleCount() + rmB.view().triangleCount());

#if defined(MH_HAVE_ASSIMP)
    Assimp::Importer importer;
    const aiScene* sc = importer.ReadFile(out.string(), 0);
    INFO("assimp: " << importer.GetErrorString());
    REQUIRE(sc != nullptr);
    REQUIRE(sc->mNumMeshes == 2);

    // Each mesh must carry its OWN geometry. Sharing one accessor block -- the
    // classic multi-mesh glTF bug -- gives two meshes at the same place.
    float minXa = 1e9F;
    float minXb = 1e9F;
    for (unsigned i = 0; i < sc->mMeshes[0]->mNumVertices; ++i)
        minXa = std::min(minXa, sc->mMeshes[0]->mVertices[i].x);
    for (unsigned i = 0; i < sc->mMeshes[1]->mNumVertices; ++i)
        minXb = std::min(minXb, sc->mMeshes[1]->mVertices[i].x);
    INFO("min x: mesh0 " << minXa << " mesh1 " << minXb);
    // Default unit is metres, so the decimetre coordinates are scaled by 0.1.
    CHECK_THAT(minXa, WithinAbs(0.0, 1e-4));
    CHECK_THAT(minXb, WithinAbs(0.8, 1e-4));

    // One node per mesh, so a DCC tool can select the clothes separately.
    CHECK(sc->mRootNode != nullptr);
    size_t meshRefs = 0;
    for (unsigned i = 0; i < sc->mRootNode->mNumChildren; ++i)
        meshRefs += sc->mRootNode->mChildren[i]->mNumMeshes;
    meshRefs += sc->mRootNode->mNumMeshes;
    CHECK(meshRefs == 2);
#endif

    std::error_code ec;
    std::filesystem::remove(out, ec);
}

TEST_CASE("each mesh keeps its own material", "[io][gltf][multimesh]") {
    const core::Mesh a = quadAt(0.0F, "body");
    const core::Mesh b = quadAt(8.0F, "eyes");
    const auto rmA     = core::RenderMesh::build(a);
    const auto rmB     = core::RenderMesh::build(b);

    foundation::MaterialDesc skin;
    skin.name    = "Skin";
    skin.diffuse = {0.9F, 0.7F, 0.6F};
    foundation::MaterialDesc glass;
    glass.name    = "Eye";
    glass.diffuse = {0.1F, 0.1F, 0.2F};

    const auto out = tempGlb("scenemat");
    const std::vector<io::GltfSceneEntry> scene{{rmA.view(), "body", &skin},
                                                {rmB.view(), "eyes", &glass}};
    REQUIRE(io::writeGlbScene(out, scene).has_value());

#if defined(MH_HAVE_ASSIMP)
    Assimp::Importer importer;
    const aiScene* sc = importer.ReadFile(out.string(), 0);
    REQUIRE(sc != nullptr);
    REQUIRE(sc->mNumMeshes == 2);
    // Two distinct materials, and the meshes must not share one index.
    CHECK(sc->mNumMaterials >= 2);
    CHECK(sc->mMeshes[0]->mMaterialIndex != sc->mMeshes[1]->mMaterialIndex);
#endif

    std::error_code ec;
    std::filesystem::remove(out, ec);
}

// The single-mesh writer is now a wrapper. If it stopped producing what it used
// to, every consumer of the existing export would change under them.
TEST_CASE("a one-entry scene matches the single-mesh writer", "[io][gltf][multimesh]") {
    const core::Mesh m = quad();
    const auto rm      = core::RenderMesh::build(m);
    const auto viaOld  = tempGlb("one_old");
    const auto viaNew  = tempGlb("one_new");

    REQUIRE(io::writeGlb(viaOld, rm.view()).has_value());
    REQUIRE(io::writeGlbScene(viaNew, {{{rm.view(), "MakeHuman"}}}).has_value());

    const auto readAll = [](const std::filesystem::path& p) {
        std::ifstream in(p, std::ios::binary);
        return std::vector<char>((std::istreambuf_iterator<char>(in)),
                                 std::istreambuf_iterator<char>());
    };
    CHECK(readAll(viaOld) == readAll(viaNew));

    std::error_code ec;
    std::filesystem::remove(viaOld, ec);
    std::filesystem::remove(viaNew, ec);
}

// The combination most likely to be subtly wrong: a SKINNED body beside an
// UNSKINNED proxy. Joint nodes follow the mesh nodes, so every joint index --
// in scene roots, node children and skins.joints -- shifts by the entry count.
// Off-by-one here produces a file that loads and animates the wrong nodes.
TEST_CASE("a skinned body and an unskinned proxy coexist", "[gltf][skin][multimesh]") {
    auto f              = buildRigged();
    const auto rmBody   = core::RenderMesh::build(f.mesh);
    const auto skinView = f.skin.view();

    const core::Mesh proxyMesh = quadAt(8.0F, "eyes");
    const auto rmProxy         = core::RenderMesh::build(proxyMesh);

    const auto out = tempGlb("skinned_scene");
    const std::vector<io::GltfSceneEntry> scene{
        {rmBody.view(), "body", nullptr, &skinView},
        {rmProxy.view(), "eyes"},
    };
    REQUIRE(io::writeGlbScene(out, scene).has_value());

    const std::string j = glbJson(out);
    // The body is skinned; the proxy must NOT be. Exactly one "skin": reference.
    CHECK(j.find(R"("skin":0)") != std::string::npos);
    size_t skinRefs = 0;
    for (size_t at = j.find(R"("skin":0)"); at != std::string::npos;
         at        = j.find(R"("skin":0)", at + 1)) {
        ++skinRefs;
    }
    CHECK(skinRefs == 1);

#if defined(MH_HAVE_ASSIMP)
    Assimp::Importer importer;
    const aiScene* sc = importer.ReadFile(out.string(), 0);
    INFO("assimp: " << importer.GetErrorString());
    REQUIRE(sc != nullptr);
    REQUIRE(sc->mNumMeshes == 2);

    // Mesh 0 is the rigged body, mesh 1 the proxy. Bones on the wrong one, or
    // joint indices pointing past the node array, show up here.
    CHECK(sc->mMeshes[0]->HasBones());
    CHECK(sc->mMeshes[0]->mNumBones == skinView.jointCount());
    CHECK_FALSE(sc->mMeshes[1]->HasBones());

    // Every bone must resolve to a real node in the graph.
    size_t unresolved = 0;
    for (unsigned b = 0; b < sc->mMeshes[0]->mNumBones; ++b) {
        if (sc->mRootNode->FindNode(sc->mMeshes[0]->mBones[b]->mName) == nullptr) ++unresolved;
    }
    CHECK(unresolved == 0);
#endif

    std::error_code ec;
    std::filesystem::remove(out, ec);
}

// glTF requires an accessor's min/max to be the true bounds of its data, and a
// validator compares them as doubles against the float data widened to double.
// Printing the bound with too few digits puts it INSIDE the range, which the
// Khronos validator reports as ACCESSOR_ELEMENT_OUT_OF_MAX_BOUND -- a real
// error in a file that still loads in most viewers, so nothing here caught it
// for a long time.
TEST_CASE("accessor bounds actually contain the data", "[io][gltf]") {
    // A coordinate whose float value needs more than 7 significant digits to
    // round-trip: 8.489434 would print as "8.489434" and bound nothing.
    core::Mesh m("awkward", 4);
    REQUIRE(m.setCoords(
                 {{0, 0, 0}, {8.4894335F, 0, 0}, {8.4894335F, 0, 1.2639965F}, {0, 0, 1.2639965F}})
                .has_value());
    REQUIRE(m.setUVs({{0, 0}, {1, 0}, {1, 1}, {0, 1}}).has_value());
    m.addFaceGroup("g");
    REQUIRE(m.setFaces({0, 1, 2, 3}, {0, 1, 2, 3}, {0}).has_value());
    m.buildAdjacency();
    m.calcNormals();

    const auto rm  = core::RenderMesh::build(m);
    const auto out = tempGlb("bounds");
    io::GltfWriteOptions opts;
    opts.unit = io::Unit::Decimeter;  // no scaling, so the values survive intact
    REQUIRE(io::writeGlb(out, rm.view(), opts).has_value());

    const std::string j = glbJson(out);
    const auto maxAt    = j.find("\"max\":[");
    REQUIRE(maxAt != std::string::npos);
    const auto close = j.find(']', maxAt);
    REQUIRE(close != std::string::npos);
    const std::string maxList = j.substr(maxAt + 7, close - (maxAt + 7));
    INFO("declared max: " << maxList);

    // Parse the three components and compare in DOUBLE, the way a validator
    // does -- the float data is widened, so a bound that only round-trips as a
    // float is still too small.
    std::vector<double> got;
    for (size_t at = 0; at < maxList.size();) {
        size_t used = 0;
        got.push_back(std::stod(maxList.substr(at), &used));
        at += used + 1;
    }
    REQUIRE(got.size() == 3);
    CHECK(got[0] >= static_cast<double>(8.4894335F));
    CHECK(got[2] >= static_cast<double>(1.2639965F));

    std::error_code ec;
    std::filesystem::remove(out, ec);
}

// The same two material hazards the OBJ writer refuses. The writers must agree
// about what is legal, or the same scene exports cleanly to one format and
// wrongly to the other.
TEST_CASE("the material contract matches the OBJ writer", "[io][gltf][multimesh]") {
    const core::Mesh a = quadAt(0.0F, "body");
    const core::Mesh b = quadAt(8.0F, "eyes");
    const auto rmA     = core::RenderMesh::build(a);
    const auto rmB     = core::RenderMesh::build(b);

    foundation::MaterialDesc skin;
    skin.name = "Skin";

    SECTION("mixing materialled and material-less entries is refused") {
        // The material-less entry would take the default name, which could be
        // the same as a real material's -- silently sharing its appearance.
        const auto out = tempGlb("mat_mixed");
        const auto r =
            io::writeGlbScene(out, {{{rmA.view(), "body", &skin}, {rmB.view(), "eyes"}}});
        REQUIRE_FALSE(r.has_value());
        CHECK(r.error().detail.find("some entries carry a material") != std::string::npos);
    }

    SECTION("two different materials with one name are refused") {
        foundation::MaterialDesc clash;
        clash.name     = "Skin";
        clash.opacity  = 0.5F;  // would export opaque if silently merged
        const auto out = tempGlb("mat_clash");
        const auto r =
            io::writeGlbScene(out, {{{rmA.view(), "body", &skin}, {rmB.view(), "eyes", &clash}}});
        REQUIRE_FALSE(r.has_value());
        CHECK(r.error().detail.find("both named") != std::string::npos);
    }

    SECTION("entries sharing one material collapse to a single slot") {
        const auto out = tempGlb("mat_shared");
        REQUIRE(io::writeGlbScene(out, {{{rmA.view(), "body", &skin}, {rmB.view(), "eyes", &skin}}})
                    .has_value());
        const std::string j = glbJson(out);
        // One material, referenced by both primitives.
        CHECK(j.find(R"("materials":[{"name":"Skin")") != std::string::npos);
        size_t mats = 0;
        for (size_t at = j.find(R"("name":"Skin")"); at != std::string::npos;
             at        = j.find(R"("name":"Skin")", at + 1)) {
            ++mats;
        }
        CHECK(mats == 1);
        std::error_code ec;
        std::filesystem::remove(out, ec);
    }
}
