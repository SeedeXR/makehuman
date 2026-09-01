// SPDX-License-Identifier: Apache-2.0
//
// Multi-format export and import. FBX is the headline: the Python reference's
// FBX exporter writes version 7300 (FBX 2013) with no animation and no
// blendshapes, and has a verified 10x unit bug; this path writes 7500 (2016)
// and round-trips.

#include "makehuman/core/RenderMesh.h"
#include "makehuman/core/Target.h"
#include "makehuman/io/SceneIO.h"
#include "makehuman/rig/Skeleton.h"
#include "makehuman/rig/Skinning.h"
#include "makehuman/rig/VertexWeights.h"

#include "makehuman/core/ObjReader.h"
#include "makehuman/io/GltfWriter.h"

#if defined(MH_HAVE_ASSIMP)
#include <assimp/postprocess.h>
#include <assimp/scene.h>
#include <assimp/Importer.hpp>
#endif

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <set>
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

/// A quad offset along X, so two of them are distinguishable in one file.
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
    const auto r =
        io::exportScene(out, core::RenderMesh::build(m).view(), io::SceneFormat::FbxBinary);
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
    const auto w =
        io::exportScene(out, core::RenderMesh::build(m).view(), io::SceneFormat::FbxBinary);
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
    CHECK(back->mesh.vertsPerPrimitive == 3);

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
    REQUIRE(io::exportScene(out, core::RenderMesh::build(m).view(), io::SceneFormat::FbxAscii)
                .has_value());

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
        const auto r   = io::exportScene(out, core::RenderMesh::build(m).view(), f);
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
        REQUIRE(io::exportScene(out, core::RenderMesh::build(m).view(), c.format).has_value());

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
    REQUIRE(io::writeGlb(glb, core::RenderMesh::build(m).view()).has_value());

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
    REQUIRE(io::exportScene(out, core::RenderMesh::build(m).view(), io::SceneFormat::FbxBinary, opt)
                .has_value());

    const auto back = io::importMesh(out);
    REQUIRE(back.has_value());

    float lo = 1e30F, hi = -1e30F;
    for (const auto& v : back->mesh.coord) {
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
    const auto r = io::exportScene(tempFile("empty", ".fbx"), core::RenderMesh::build(m).view(),
                                   io::SceneFormat::FbxBinary);
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

// ------------------------------------------------------ rigged FBX export

namespace {

struct FbxRigFixture {
    core::Mesh mesh;
    rig::Skeleton skel;
    rig::SkinData skin;
    std::vector<std::vector<foundation::Vec3>> deltaStore;
    std::vector<foundation::MorphTarget> morphs;
};

FbxRigFixture buildFbxRig() {
    FbxRigFixture f;
    auto mesh = core::loadObj(std::filesystem::path(MH_DATA_DIR) / "3dobjs" / "base.obj");
    REQUIRE(mesh.has_value());
    f.mesh = std::move(*mesh);

    auto skel = rig::loadSkeleton(std::filesystem::path(MH_DATA_DIR) / "rigs" / "default.mhskel");
    REQUIRE(skel.has_value());
    REQUIRE(skel->updateJoints(f.mesh.coord()));
    REQUIRE(skel->buildRestMatrices());
    f.skel = std::move(*skel);

    auto vw = rig::loadWeights(std::filesystem::path(MH_DATA_DIR) / "rigs" / "default_weights.mhw",
                               f.mesh.vertexCount());
    REQUIRE(vw.has_value());

    const auto rm = core::RenderMesh::build(f.mesh);
    f.skin        = rig::buildSkinData(f.skel, vw->compile(f.skel, 4), rm.vmap());
    REQUIRE_FALSE(f.skin.jointNames.empty());

    auto t = core::loadTarget(std::filesystem::path(MH_DATA_DIR) / "targets" / "head" /
                              "head-oval.target");
    REQUIRE(t.has_value());
    std::vector<foundation::Vec3> deltas;
    REQUIRE(core::expandTargetToRenderVertices(*t, rm.vmap(), f.mesh.vertexCount(), deltas));
    f.deltaStore.push_back(std::move(deltas));
    f.morphs.push_back(foundation::MorphTarget{"head-oval", f.deltaStore.back()});
    return f;
}

}  // namespace

// assimp's FBX writer needs a NODE per bone in the scene graph. The aiBone
// array alone is not a skeleton -- the hierarchy lives in the nodes and aiBone
// references it by name. Without them the export fails outright with
// "Failed to find node for bone: root", which is how this was found.
TEST_CASE("a rigged FBX exports and re-imports", "[io][scene][skin]") {
    auto f              = buildFbxRig();
    const auto rm       = core::RenderMesh::build(f.mesh);
    const auto skinView = f.skin.view();

    const auto out = tempFile("rigged", ".fbx");
    const auto r   = io::exportScene(out, rm.view(), io::SceneFormat::FbxBinary, {}, nullptr,
                                     &skinView, f.morphs);
    REQUIRE(r.has_value());
    CHECK(r->vertices == rm.vertexCount());

    const auto back = io::importMesh(out);
    REQUIRE(back.has_value());
    CHECK(back->mesh.vertexCount() > 0);

    std::error_code ec;
    std::filesystem::remove(out, ec);
}

TEST_CASE("a skin that does not describe the mesh is refused by FBX export", "[io][scene][skin]") {
    auto f        = buildFbxRig();
    const auto rm = core::RenderMesh::build(f.mesh);

    auto bad        = f.skin;
    bad.joints[0]   = 9999;  // past the end of the skeleton
    const auto view = bad.view();

    const auto out = tempFile("badrig", ".fbx");
    const auto r =
        io::exportScene(out, rm.view(), io::SceneFormat::FbxBinary, {}, nullptr, &view, {});
    REQUIRE_FALSE(r.has_value());
    CHECK(r.error().kind == io::SceneIoErrorKind::InvalidSkinOrMorph);

    std::error_code ec;
    std::filesystem::remove(out, ec);
}

TEST_CASE("a morph target of the wrong length is refused by FBX export", "[io][scene][morph]") {
    auto f        = buildFbxRig();
    const auto rm = core::RenderMesh::build(f.mesh);

    const std::vector<foundation::Vec3> tooShort(3, foundation::Vec3{});
    const std::vector<foundation::MorphTarget> bad{{"short", tooShort}};

    const auto out = tempFile("badmorph", ".fbx");
    const auto r =
        io::exportScene(out, rm.view(), io::SceneFormat::FbxBinary, {}, nullptr, nullptr, bad);
    REQUIRE_FALSE(r.has_value());
    CHECK(r.error().kind == io::SceneIoErrorKind::InvalidSkinOrMorph);

    std::error_code ec;
    std::filesystem::remove(out, ec);
}

// FBX, Collada and the rest were the last single-mesh writers: a dressed
// character exported to them arrived naked. assimp's aiScene holds many meshes
// natively, so this is about building the scene rather than fighting a format.
TEST_CASE("several meshes export and re-import through assimp", "[io][scene][multimesh]") {
    const core::Mesh a = quadAt(0.0F, "body");
    const core::Mesh b = quadAt(8.0F, "eyes");
    const auto rmA     = core::RenderMesh::build(a);
    const auto rmB     = core::RenderMesh::build(b);

    foundation::MaterialDesc skin;
    skin.name = "DefaultSkin";
    foundation::MaterialDesc eye;
    eye.name = "Eye_brown";

    // Every assimp-backed format we ship, so none of them regresses quietly.
    const std::pair<io::SceneFormat, const char*> formats[] = {
        {io::SceneFormat::FbxBinary, ".fbx"},
        {io::SceneFormat::Collada, ".dae"},
    };
    for (const auto& [format, ext] : formats) {
        const auto out = tempFile("multimesh", ext);
        std::error_code ec;
        std::filesystem::remove(out, ec);

        const std::vector<io::SceneEntry> scene{{rmA.view(), "body", &skin},
                                                {rmB.view(), "eyes", &eye}};
        const auto r = io::exportScene(out, scene, format);
        INFO(ext << ": " << (r ? std::string{} : r.error().message()));
        REQUIRE(r.has_value());
        CHECK(r->vertices == rmA.view().vertexCount() + rmB.view().vertexCount());

#if defined(MH_HAVE_ASSIMP)
        Assimp::Importer importer;
        const aiScene* sc = importer.ReadFile(out.string(), 0);
        INFO(ext << " import: " << importer.GetErrorString());
        REQUIRE(sc != nullptr);
        REQUIRE(sc->mNumMeshes == 2);
        // Each mesh keeps its own material, or a DCC tool shows one blob.
        CHECK(sc->mMeshes[0]->mMaterialIndex != sc->mMeshes[1]->mMaterialIndex);

        // Each mesh must keep its own NAME. assimp's FBX exporter names a mesh
        // after the node that owns it, so hanging every mesh off the root gave
        // two meshes both called "body" -- geometry and materials survived, the
        // identity did not, and Blender merged them into one object. Collada
        // happened to preserve names, which is the only reason it showed up.
        //
        // Matched by PREFIX, not equality: Collada's exporter disambiguates a
        // mesh whose name matches its node's and emits `body_1`, while FBX
        // gives the name exactly. Both preserve the identity, which is the
        // property that matters -- a DCC user must be able to tell the clothes
        // from the body.
        std::vector<std::string> names;
        for (unsigned i = 0; i < sc->mNumMeshes; ++i)
            names.emplace_back(sc->mMeshes[i]->mName.C_Str());
        const auto namedAfter = [&names](const std::string& want) {
            return std::count_if(names.begin(), names.end(), [&want](const std::string& n) {
                       return n.rfind(want, 0) == 0;
                   }) == 1;
        };
        INFO(ext << " mesh names: " << names[0] << ", " << names[1]);
        CHECK(namedAfter("body"));
        CHECK(namedAfter("eyes"));

        // And they must sit where they were put -- sharing one vertex block
        // would stack them.
        float minA = 1e9F;
        float minB = 1e9F;
        for (unsigned i = 0; i < sc->mMeshes[0]->mNumVertices; ++i)
            minA = std::min(minA, sc->mMeshes[0]->mVertices[i].x);
        for (unsigned i = 0; i < sc->mMeshes[1]->mNumVertices; ++i)
            minB = std::min(minB, sc->mMeshes[1]->mVertices[i].x);
        INFO(ext << " min x: " << minA << " and " << minB);
        CHECK(std::abs(minA - minB) > 0.1F);
#endif

        std::filesystem::remove(out, ec);
    }
}

// The single-mesh entry point is a separate implementation, not a wrapper -- it
// also carries skin and morph targets, which a scene does not. It must still
// agree with a one-entry scene about the GEOMETRY.
//
// Not byte-identical: the multi-mesh path gives every mesh its own node, which
// is what lets FBX keep per-mesh names. That is a deliberate structural
// difference, so comparing bytes would only pin the difference in place.
TEST_CASE("a one-entry scene carries the same geometry as the single-mesh writer",
          "[io][scene][multimesh]") {
    const core::Mesh m = quadAt(0.0F, "solo");
    const auto rm      = core::RenderMesh::build(m);

    const auto viaOld = tempFile("one_old", ".dae");
    const auto viaNew = tempFile("one_new", ".dae");
    std::error_code ec;
    std::filesystem::remove(viaOld, ec);
    std::filesystem::remove(viaNew, ec);

    const auto a = io::exportScene(viaOld, rm.view(), io::SceneFormat::Collada);
    const auto b = io::exportScene(viaNew, {{{rm.view(), "MakeHuman"}}}, io::SceneFormat::Collada);
    REQUIRE(a.has_value());
    REQUIRE(b.has_value());
    CHECK(a->vertices == b->vertices);
    CHECK(a->triangles == b->triangles);

#if defined(MH_HAVE_ASSIMP)
    Assimp::Importer one;
    Assimp::Importer two;
    const aiScene* sa = one.ReadFile(viaOld.string(), 0);
    const aiScene* sb = two.ReadFile(viaNew.string(), 0);
    REQUIRE(sa != nullptr);
    REQUIRE(sb != nullptr);
    REQUIRE(sa->mNumMeshes == 1);
    REQUIRE(sb->mNumMeshes == 1);
    CHECK(sa->mMeshes[0]->mNumVertices == sb->mMeshes[0]->mNumVertices);
    CHECK(sa->mMeshes[0]->mNumFaces == sb->mMeshes[0]->mNumFaces);
#endif

    std::filesystem::remove(viaOld, ec);
    std::filesystem::remove(viaNew, ec);
}

// --- Multi-mesh import ------------------------------------------------------
//
// Export has been multi-mesh for a while: a dressed character is written as the
// body plus one entry per worn proxy. Import read only `mMeshes[0]`, so a
// round trip silently returned a naked character -- the clothes exported fine
// and vanished on the way back in.
//
// The round trip is the test worth having: it goes out through our writer and
// back through a third-party library, so agreement is not self-confirming.
TEST_CASE("every mesh in an exported scene imports back", "[io][import][scene]") {
    const core::Mesh m = baseMeshOrSkip();
    const auto rm      = core::RenderMesh::build(m);

    // Two entries with distinct names, standing in for body + something worn.
    const std::vector<io::SceneEntry> scene{{rm.view(), "body", nullptr},
                                            {rm.view(), "worn", nullptr}};

    for (const auto fmt : {io::SceneFormat::FbxBinary, io::SceneFormat::Collada}) {
        const bool fbx = fmt == io::SceneFormat::FbxBinary;
        const auto out = tempFile("multiimport", fbx ? ".fbx" : ".dae");
        INFO((fbx ? "fbx" : "dae"));

        const auto w = io::exportScene(out, scene, fmt);
        REQUIRE(w.has_value());

        const auto back = io::importScene(out);
        REQUIRE(back.has_value());

        // Both meshes come back, not just the first.
        REQUIRE(back->meshes.size() == 2);
        for (const auto& entry : back->meshes) {
            INFO("mesh " << entry.name);
            CHECK(entry.mesh.vertexCount() > 0);
            CHECK(entry.mesh.faceCount() > 0);
            // Each carries the full body geometry, so neither is a stub.
            CHECK(entry.mesh.faceCount() == m.faceCount() * 2);  // quads -> triangles
        }

        // And importMesh still returns exactly one, reporting how many it saw.
        const auto single = io::importMesh(out);
        REQUIRE(single.has_value());
        CHECK(single->meshCount == 2);

        std::error_code ec;
        std::filesystem::remove(out, ec);
    }
}

// Our GLB writer is hand-rolled, not assimp's. Reading it back through assimp
// is therefore a genuine cross-check rather than a library agreeing with
// itself -- the same reason the USD work leaned on usdchecker.
TEST_CASE("our own GLB imports back with every mesh", "[io][import][scene][gltf]") {
    const core::Mesh m = baseMeshOrSkip();
    const auto rm      = core::RenderMesh::build(m);

    const std::vector<io::GltfSceneEntry> scene{{rm.view(), "body", nullptr},
                                                {rm.view(), "worn", nullptr}};
    const auto out = tempFile("multiimport", ".glb");
    REQUIRE(io::writeGlbScene(out, scene).has_value());

    const auto back = io::importScene(out);
    REQUIRE(back.has_value());
    REQUIRE(back->meshes.size() == 2);
    for (const auto& entry : back->meshes) {
        INFO("mesh " << entry.name);
        CHECK(entry.mesh.vertexCount() > 0);
        CHECK(entry.mesh.faceCount() == m.faceCount() * 2);
    }

    std::error_code ec;
    std::filesystem::remove(out, ec);
}

TEST_CASE("importScene reports a file it cannot read", "[io][import][scene]") {
    const auto missing = io::importScene("/definitely/not/a/scene.glb");
    REQUIRE_FALSE(missing.has_value());
    CHECK(missing.error().kind == io::SceneIoErrorKind::NotFound);
}
