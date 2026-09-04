// SPDX-License-Identifier: Apache-2.0
//
// Multi-format export and import. FBX is the headline: the Python reference's
// FBX exporter writes version 7300 (FBX 2013) with no animation and no
// blendshapes, and has a verified 10x unit bug; this path writes 7500 (2016)
// and round-trips.

#include "makehuman/core/RenderMesh.h"
#include "makehuman/core/Target.h"
#include "makehuman/io/GltfWriter.h"
#include "makehuman/io/SceneIO.h"
#include "makehuman/rig/Skeleton.h"
#include "makehuman/rig/Skinning.h"
#include "makehuman/rig/VertexWeights.h"

#include "makehuman/core/ObjReader.h"
#include "makehuman/io/GltfWriter.h"
#include "makehuman/rig/Skinning.h"

#if defined(MH_HAVE_ASSIMP)
#include <assimp/postprocess.h>
#include <assimp/scene.h>
#include <assimp/Importer.hpp>
#endif

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <algorithm>
#include <catch2/catch_approx.hpp>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <limits>
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

// A DRESSED character has to keep its rig too.
//
// The single-mesh overload carried a skin from the start; the scene overload
// did not even take one, so the moment a character wore anything its FBX and
// Collada exports became statues -- and FBX is the format a rigged character
// is usually handed over in. Only the body is skinned: worn proxies follow it
// by being re-fitted, not skinned, which is also why exactly one entry may
// carry a skin.
TEST_CASE("a dressed character keeps its rig through the scene path",
          "[io][scene][skin][multimesh]") {
    auto f                   = buildFbxRig();
    const auto rm            = core::RenderMesh::build(f.mesh);
    const core::Mesh eyeMesh = quadAt(8.0F, "eyes");
    const auto rmEye         = core::RenderMesh::build(eyeMesh);
    const auto skinView      = f.skin.view();

    const std::pair<io::SceneFormat, const char*> formats[] = {
        {io::SceneFormat::FbxBinary, ".fbx"},
        {io::SceneFormat::Collada, ".dae"},
    };
    for (const auto& [format, ext] : formats) {
        const auto out = tempFile("dressedrig", ext);
        std::error_code ec;
        std::filesystem::remove(out, ec);

        std::vector<io::SceneEntry> scene{{rm.view(), "body", nullptr, &skinView},
                                          {rmEye.view(), "eyes", nullptr, nullptr}};
        const auto r = io::exportScene(out, scene, format);
        INFO(ext << ": " << (r ? std::string{} : r.error().message()));
        REQUIRE(r.has_value());

#if defined(MH_HAVE_ASSIMP)
        Assimp::Importer importer;
        const aiScene* sc = importer.ReadFile(out.string(), 0);
        INFO(ext << " import: " << importer.GetErrorString());
        REQUIRE(sc != nullptr);
        REQUIRE(sc->mNumMeshes == 2);

        // The bones must land on the BODY, not on whichever mesh happens to be
        // first. Matched by name, because the two formats order meshes
        // differently.
        unsigned bonesOnBody = 0;
        unsigned bonesOnEyes = 0;
        for (unsigned m = 0; m < sc->mNumMeshes; ++m) {
            const std::string name = sc->mMeshes[m]->mName.C_Str();
            if (name.rfind("body", 0) == 0) bonesOnBody = sc->mMeshes[m]->mNumBones;
            if (name.rfind("eyes", 0) == 0) bonesOnEyes = sc->mMeshes[m]->mNumBones;
        }
        // Not every joint of the 163-bone rig is weighted: `default_weights.mhw`
        // names 139. FBX writes all 163 bones, Collada writes only the weighted
        // 139 -- assimp's exporter prunes the empty ones. Both keep a usable
        // rig, so the assertion is the weighted set, with the format-specific
        // extra allowed rather than demanded.
        std::vector<bool> used(f.skin.jointNames.size(), false);
        for (size_t k = 0; k < f.skin.weights.size(); ++k)
            if (f.skin.weights[k] > 0.0F) used[f.skin.joints[k]] = true;
        const size_t weighted = static_cast<size_t>(std::count(used.begin(), used.end(), true));
        INFO(ext << " bones: body " << bonesOnBody << " eyes " << bonesOnEyes << " weighted "
                 << weighted);
        CHECK(weighted > 100);  // the fixture must exercise a real rig
        CHECK(bonesOnBody >= weighted);
        CHECK(bonesOnBody <= f.skin.jointNames.size());
        CHECK(bonesOnEyes == 0);

        // ...and the joint NODES must survive alongside the two mesh nodes. An
        // aiBone with no node of its own name is what made the FBX writer fail
        // outright, so their presence is the half that cannot be assumed.
        size_t jointNodes                             = 0;
        const std::function<void(const aiNode*)> walk = [&](const aiNode* n) {
            const std::string name = n->mName.C_Str();
            for (const std::string& j : f.skin.jointNames) {
                if (name == j) {
                    ++jointNodes;
                    break;
                }
            }
            for (unsigned c = 0; c < n->mNumChildren; ++c)
                walk(n->mChildren[c]);
        };
        walk(sc->mRootNode);
        INFO(ext << " joint nodes: " << jointNodes);
        CHECK(jointNodes == f.skin.jointNames.size());
#endif
        std::filesystem::remove(out, ec);
    }
}

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

// --- Node transforms --------------------------------------------------------
//
// A glTF/FBX/DAE scene places meshes with a NODE GRAPH: the mesh data is in
// local space and the node carries the transform. Reading `mMeshes` directly
// and never walking `mRootNode` returns every mesh at the origin -- silently,
// with no error, so a whole imported scene collapses into one pile.
//
// tests/golden/scene/two_cubes.glb exists for exactly this: two IDENTICAL cubes
// whose only difference is a node translation of x = -5 and x = +5. Before this
// was fixed, both imported with x in [-1, 1].
TEST_CASE("node transforms place imported meshes", "[io][import][scene][transform]") {
    const auto file =
        std::filesystem::path(MH_DATA_DIR) / ".." / "tests" / "golden" / "scene" / "two_cubes.glb";
    if (!std::filesystem::exists(file)) SKIP("scene fixture not present");

    const auto scene = io::importScene(file);
    REQUIRE(scene.has_value());
    REQUIRE(scene->meshes.size() == 2);

    // Each cube is 2 units wide, so a correctly placed pair spans about
    // [-6,-4] and [+4,+6]; a broken one gives [-1,1] twice.
    std::vector<float> centres;
    for (const auto& m : scene->meshes) {
        REQUIRE_FALSE(m.mesh.coord.empty());
        float lo = std::numeric_limits<float>::infinity();
        float hi = -lo;
        for (const auto& v : m.mesh.coord) {
            lo = std::min(lo, v.x);
            hi = std::max(hi, v.x);
        }
        INFO("mesh " << m.name << " x range [" << lo << ", " << hi << "]");
        CHECK(hi - lo == Catch::Approx(2.0F).margin(0.01F));  // still a unit cube
        centres.push_back((lo + hi) * 0.5F);
    }

    std::ranges::sort(centres);
    INFO("centres " << centres[0] << " and " << centres[1]);
    CHECK(centres[0] == Catch::Approx(-5.0F).margin(0.01F));
    CHECK(centres[1] == Catch::Approx(5.0F).margin(0.01F));
    // The whole point: they are NOT in the same place.
    CHECK(centres[1] - centres[0] == Catch::Approx(10.0F).margin(0.02F));
}

// --- Materials on import ----------------------------------------------------
//
// Measured, not assumed. Round-tripping a fully specified material shows what
// each format actually keeps:
//
//   |            | name | diffuse | specular | opacity | textures |
//   |------------|------|---------|----------|---------|----------|
//   | FBX        | yes  | yes     | NO       | yes     | written, not read back |
//   | Collada    | yes  | replaced by texture | yes | yes | yes |
//
// Two of those need explaining rather than asserting around:
//
//  * **FBX textures ARE written** -- the path appears three times in the
//    exported bytes -- but assimp's FBX *importer* does not read material
//    textures back. That is a reader limitation, not a missing export, and a
//    DCC tool opening the file gets the reference. So the FBX case is checked
//    in the FILE, not through the round trip.
//  * **Collada replaces the diffuse colour with the texture** when one is
//    present (`<diffuse><texture/></diffuse>`), which is the format's own
//    semantics, not a loss on our side.
//
// Shininess USED to be "deliberately not asserted: the conventions differ per
// format". They did not differ -- the exporter simply never wrote the key.
// Measured: a 0.96 skin came back as **0.2** from FBX (our own struct default,
// the file carrying nothing) and as **10** from Collada (assimp's exporter
// substituting a fixed exponent). It is written and read as an exponent now,
// and both round-trip exactly.
TEST_CASE("materials survive an export and import", "[io][import][material]") {
    const core::Mesh m = baseMeshOrSkip();
    const auto rm      = core::RenderMesh::build(m);

    foundation::MaterialDesc want;
    want.name           = "TestSkin";
    want.diffuse        = {0.80F, 0.60F, 0.50F};
    want.specular       = {0.30F, 0.30F, 0.30F};
    want.opacity        = 0.75F;
    want.shininess      = 0.96F;  // DefaultSkin's own value
    want.diffuseTexture = "skin_albedo.png";
    want.normalTexture  = "skin_normal.png";

    const std::vector<io::SceneEntry> scene{{rm.view(), "body", &want}};

    SECTION("FBX keeps name, colour and opacity") {
        const auto out = tempFile("matrt", ".fbx");
        REQUIRE(io::exportScene(out, scene, io::SceneFormat::FbxBinary).has_value());

        const auto back = io::importScene(out);
        REQUIRE(back.has_value());
        REQUIRE(back->meshes.size() == 1);
        REQUIRE(back->meshes[0].material.has_value());
        const auto& got = *back->meshes[0].material;

        CHECK(got.name == "TestSkin");
        CHECK(got.diffuse.x == Catch::Approx(0.80F).margin(0.01F));
        CHECK(got.diffuse.y == Catch::Approx(0.60F).margin(0.01F));
        CHECK(got.opacity == Catch::Approx(0.75F).margin(0.01F));
        CHECK(got.transparent);  // opacity < 1 implies it
        CHECK(got.shininess == Catch::Approx(0.96F).margin(0.01F));

        // The texture reference IS in the file even though assimp will not read
        // it back, so this is checked where it exists rather than skipped.
        std::ifstream f(out, std::ios::binary);
        const std::string bytes((std::istreambuf_iterator<char>(f)),
                                std::istreambuf_iterator<char>());
        CHECK(bytes.find("skin_albedo.png") != std::string::npos);

        std::error_code ec;
        std::filesystem::remove(out, ec);
    }

    SECTION("Collada keeps the texture paths") {
        const auto out = tempFile("matrt", ".dae");
        REQUIRE(io::exportScene(out, scene, io::SceneFormat::Collada).has_value());

        const auto back = io::importScene(out);
        REQUIRE(back.has_value());
        REQUIRE(back->meshes[0].material.has_value());
        const auto& got = *back->meshes[0].material;

        CHECK(got.name == "TestSkin");
        CHECK(got.diffuseTexture == "skin_albedo.png");
        CHECK(got.normalTexture == "skin_normal.png");
        CHECK(got.specular.x == Catch::Approx(0.30F).margin(0.01F));
        CHECK(got.opacity == Catch::Approx(0.75F).margin(0.01F));
        CHECK(got.shininess == Catch::Approx(0.96F).margin(0.01F));

        std::error_code ec;
        std::filesystem::remove(out, ec);
    }
}

// What is actually IN the FBX, read with assimp rather than through our own
// importer -- the round-trip tests above would pass just as happily if both
// ends were wrong in the same way.
//
// Third-party check, Blender 5.2 on `makehuman --export x.fbx`:
//   before  DefaultSkin  roughness 0.0000  metallic 1.0000   (a chrome mirror)
//   after   DefaultSkin  roughness 0.0000  metallic 0.0000
// The GLB of the same character reads 0.0400 / 0.0000 and always did, so the
// two exports of one material disagreed about whether skin is metal.
//
// Roughness stays 0 in Blender and that is Blender's own curve, not a loss on
// our side: it reads FBX Shininess as 0..100 through `1 - sqrt(S)/10`
// (import_fbx.py:2083, whose comment calls it "totally empirical"), so any
// shininess above 0.78 clamps. Our exponent is on OpenGL's documented 0..128
// GL_SHININESS scale and round-trips exactly through assimp.
TEST_CASE("an FBX states the specular exponent and states it is not metal", "[io][material][fbx]") {
#if defined(MH_HAVE_ASSIMP)
    const core::Mesh m = baseMeshOrSkip();
    const auto rm      = core::RenderMesh::build(m);

    foundation::MaterialDesc want;
    want.name      = "TestSkin";
    want.shininess = 0.96F;

    const auto out = tempFile("fbxmat", ".fbx");
    const std::vector<io::SceneEntry> scene{{rm.view(), "body", &want}};
    REQUIRE(io::exportScene(out, scene, io::SceneFormat::FbxBinary).has_value());

    Assimp::Importer importer;
    const aiScene* sc = importer.ReadFile(out.string(), 0);
    INFO("fbx import: " << importer.GetErrorString());
    REQUIRE(sc != nullptr);
    REQUIRE(sc->mNumMaterials >= 1);

    float shine = -1.0F;
    float refl  = -1.0F;
    CHECK(sc->mMaterials[0]->Get(AI_MATKEY_SHININESS, shine) == AI_SUCCESS);
    CHECK(shine == Catch::Approx(122.88F).margin(0.01F));
    CHECK(sc->mMaterials[0]->Get(AI_MATKEY_REFLECTIVITY, refl) == AI_SUCCESS);
    CHECK(refl == 0.0F);

    std::error_code ec;
    std::filesystem::remove(out, ec);
#endif
}

TEST_CASE("shininess and the specular exponent are inverses", "[io][material]") {
    // The pair has to stay a pair: a Blinn-Phong round trip leaves through one
    // and returns through the other.
    for (const float s : {0.0F, 0.2F, 0.5F, 0.96F, 1.0F}) {
        CHECK(foundation::shininessFromExponent(foundation::specularExponentOf(s)) ==
              Catch::Approx(s).margin(1e-6F));
    }
    CHECK(foundation::specularExponentOf(0.96F) == Catch::Approx(122.88F).margin(0.01F));

    // Clamped, because the exponent comes from a file. assimp's own Collada
    // exporter writes 10 when given nothing, and 10 in a 0..1 field is what
    // made `1 - shininess` go negative.
    CHECK(foundation::shininessFromExponent(10.0F) == Catch::Approx(0.078F).margin(0.001F));
    CHECK(foundation::shininessFromExponent(1000.0F) == 1.0F);
    CHECK(foundation::shininessFromExponent(-5.0F) == 0.0F);
}

// The Blinn-Phong -> metallic-roughness conversion, in ONE place.
//
// It was written out twice -- `GltfWriter.cpp` and `UsdWriter.cpp` -- with the
// reasoning for `metallic = 0` living in only one of the two comments and the
// other deferring to it by reference. Two writers computing the same thing
// separately is how they end up disagreeing about one character.
TEST_CASE("Blinn-Phong converts to metallic-roughness the same way everywhere",
          "[io][material][pbr]") {
    foundation::MaterialDesc m;

    // Roughness is the complement of shininess.
    m.shininess = 0.96F;  // the shipped skin
    CHECK(foundation::metallicRoughnessOf(m).roughness == Catch::Approx(0.04F).margin(1e-5F));
    m.shininess = 0.0F;
    CHECK(foundation::metallicRoughnessOf(m).roughness == 1.0F);
    m.shininess = 1.0F;
    CHECK(foundation::metallicRoughnessOf(m).roughness == 0.0F);

    // Metallic is always zero, and that is a modelling statement: skin, cloth,
    // hair and eyes are all dielectric, and `.mhmat` has no field that could
    // say otherwise. A writer inventing its own value is the bug this prevents.
    for (const float s : {0.0F, 0.5F, 1.0F, 42.0F, -3.0F}) {
        m.shininess = s;
        CHECK(foundation::metallicRoughnessOf(m).metallic == 0.0F);
    }

    // Clamped, because a MaterialDesc can be built by hand as well as parsed.
    // An unscaled Collada exponent of 10 lands here as shininess 10 and would
    // otherwise ask for roughness -9.
    m.shininess = 10.0F;
    CHECK(foundation::metallicRoughnessOf(m).roughness == 0.0F);
    m.shininess = -5.0F;
    CHECK(foundation::metallicRoughnessOf(m).roughness == 1.0F);
}

// The compounding failure, end to end: import a Collada file and re-export it
// as glTF. glTF roughness is `1 - shininess`, so an unscaled exponent arriving
// from the importer does not merely look wrong -- it asks for a NEGATIVE
// roughness, which clamps to 0 and turns every surface into a mirror.
//
// Measured before the fix: Collada came back with shininess 10, so this GLB
// carried `"roughnessFactor":0`. It is 0.04 now, which is what 0.96 means.
TEST_CASE("a Collada round trip does not turn the skin into a mirror",
          "[io][import][material][gltf]") {
    const core::Mesh m = baseMeshOrSkip();
    const auto rm      = core::RenderMesh::build(m);

    foundation::MaterialDesc want;
    want.name      = "TestSkin";
    want.shininess = 0.96F;  // no textures: GLB embeds them and these do not exist

    const auto dae = tempFile("mirror", ".dae");
    const std::vector<io::SceneEntry> out{{rm.view(), "body", &want}};
    REQUIRE(io::exportScene(dae, out, io::SceneFormat::Collada).has_value());

    const auto back = io::importScene(dae);
    REQUIRE(back.has_value());
    REQUIRE(back->meshes[0].material.has_value());

    const auto glb = tempFile("mirror", ".glb");
    const std::vector<io::GltfSceneEntry> scene{{rm.view(), "body", &*back->meshes[0].material}};
    REQUIRE(io::writeGlbScene(glb, scene).has_value());

    std::ifstream f(glb, std::ios::binary);
    const std::string bytes((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    CHECK(bytes.find(R"("roughnessFactor":0.04)") != std::string::npos);
    CHECK(bytes.find(R"("roughnessFactor":0,)") == std::string::npos);

    std::error_code ec;
    std::filesystem::remove(dae, ec);
    std::filesystem::remove(glb, ec);
}

TEST_CASE("a mesh with no material reports none", "[io][import][material]") {
    // Absent must mean "the file had none", not "it had a default" -- a caller
    // substituting its own default has to be able to tell.
    const core::Mesh m = baseMeshOrSkip();
    const auto rm      = core::RenderMesh::build(m);
    const auto out     = tempFile("nomat", ".glb");

    const std::vector<io::GltfSceneEntry> scene{{rm.view(), "body", nullptr}};
    REQUIRE(io::writeGlbScene(out, scene).has_value());

    const auto back = io::importScene(out);
    REQUIRE(back.has_value());
    REQUIRE(back->meshes.size() == 1);
    INFO("material present: " << back->meshes[0].material.has_value());
    // glTF without a material index leaves this empty; if the writer emits a
    // default one, the name tells us it is not ours.
    if (back->meshes[0].material) {
        CHECK(back->meshes[0].material->name != "TestSkin");
    }

    std::error_code ec;
    std::filesystem::remove(out, ec);
}

// --- Skins on import --------------------------------------------------------
//
// The last piece: a rigged export must come back rigged. Without this, a
// character round-tripped through glTF loses its skeleton binding entirely --
// the geometry and bones both survive, and nothing connects them.
TEST_CASE("a rigged export imports back with its skin", "[io][import][skin]") {
    const auto rigPath = std::filesystem::path(MH_DATA_DIR) / "rigs" / "default.mhskel";
    if (!std::filesystem::exists(rigPath)) SKIP("rig not present");

    core::Mesh mesh = baseMeshOrSkip();
    auto skel       = rig::loadSkeleton(rigPath);
    REQUIRE(skel.has_value());
    REQUIRE(skel->updateJoints(mesh.coord()));
    REQUIRE(skel->buildRestMatrices());

    auto vw = rig::loadWeights(std::filesystem::path(MH_DATA_DIR) / "rigs" / "default_weights.mhw",
                               mesh.vertexCount());
    REQUIRE(vw.has_value());

    const auto rm       = core::RenderMesh::build(mesh);
    const auto compiled = vw->compile(*skel, io::kGltfInfluences);
    const auto skin     = rig::buildSkinData(*skel, compiled, rm.vmap());
    REQUIRE_FALSE(skin.jointNames.empty());
    const auto skinView = skin.view();

    const auto out = tempFile("riggedimport", ".glb");
    REQUIRE(io::writeGlb(out, rm.view(), {}, nullptr, &skinView).has_value());

    const auto back = io::importScene(out);
    REQUIRE(back.has_value());
    REQUIRE(back->meshes.size() == 1);
    REQUIRE(back->meshes[0].skin.has_value());
    const auto& got = *back->meshes[0].skin;

    // Every joint that actually influences something comes back. glTF stores
    // only the joints a skin references, so this is "no bone was lost", not
    // "the count matches the whole 163-bone rig".
    INFO("exported " << skin.jointNames.size() << " joints, imported " << got.bones.size());
    CHECK(got.bones.size() > 100);
    CHECK(got.bones.size() <= skin.jointNames.size());

    // Names survive, which is what makes a re-bind possible at all.
    const bool named =
        std::ranges::all_of(got.bones, [](const auto& b) { return !b.name.empty(); });
    CHECK(named);

    // Weights are a partition of unity per vertex, or the mesh deforms wrongly
    // -- and this is the property a bad vertex-id remap would break.
    std::vector<float> perVertex(back->meshes[0].mesh.coord.size(), 0.0F);
    size_t outOfRange = 0;
    for (const auto& b : got.bones) {
        REQUIRE(b.verts.size() == b.weights.size());
        for (size_t i = 0; i < b.verts.size(); ++i) {
            if (b.verts[i] >= perVertex.size()) {
                ++outOfRange;
                continue;
            }
            CHECK(b.weights[i] >= 0.0F);
            CHECK(b.weights[i] <= 1.0F + 1e-5F);
            perVertex[b.verts[i]] += b.weights[i];
        }
    }
    CHECK(outOfRange == 0);

    size_t weighted = 0;
    size_t badSum   = 0;
    for (const float sum : perVertex) {
        if (sum <= 0.0F) continue;
        ++weighted;
        if (std::abs(sum - 1.0F) > 1e-3F) ++badSum;
    }
    INFO("weighted vertices " << weighted << ", not summing to 1: " << badSum);
    CHECK(weighted > 1000);
    CHECK(badSum == 0);

    std::error_code ec;
    std::filesystem::remove(out, ec);
}

TEST_CASE("an unrigged mesh reports no skin", "[io][import][skin]") {
    const core::Mesh m = baseMeshOrSkip();
    const auto rm      = core::RenderMesh::build(m);
    const auto out     = tempFile("noskin", ".glb");
    const std::vector<io::GltfSceneEntry> scene{{rm.view(), "body", nullptr}};
    REQUIRE(io::writeGlbScene(out, scene).has_value());

    const auto back = io::importScene(out);
    REQUIRE(back.has_value());
    CHECK_FALSE(back->meshes[0].skin.has_value());

    std::error_code ec;
    std::filesystem::remove(out, ec);
}

// --- Blendshapes in a multi-mesh scene --------------------------------------
//
// The scene overload carried no morph targets at all, so a dressed character's
// FBX and Collada lost every expression: `--blendshapes` reached the file only
// through GLB, and the moment the character wore anything even the single-mesh
// FBX path was unreachable. The same shape as the skin gap fixed earlier -- a
// field the scene struct simply did not have.
//
// One entry may carry morphs, exactly as one entry may carry a skin. A worn
// proxy is re-fitted to the body rather than blended, so a second morph set is
// a scene shape nothing needs and nothing tests.
TEST_CASE("a multi-mesh scene carries morph targets on the right mesh",
          "[io][scene][multimesh][morph]") {
    const core::Mesh body  = quadAt(0.0F, "body");
    const core::Mesh shirt = quadAt(3.0F, "shirt");
    const auto rmBody      = core::RenderMesh::build(body);
    const auto rmShirt     = core::RenderMesh::build(shirt);

    // A delta big enough that a dropped or zeroed morph cannot look like this.
    std::vector<foundation::Vec3> deltas(rmBody.view().vertexCount(),
                                         foundation::Vec3{0.0F, 2.0F, 0.0F});
    const std::vector<foundation::MorphTarget> morphs{{"smile", deltas}};

    std::vector<io::SceneEntry> scene{{rmBody.view(), "body", nullptr, nullptr, morphs},
                                      {rmShirt.view(), "shirt", nullptr, nullptr, {}}};

    const auto out = tempFile("scene_morph", ".fbx");
    std::error_code ec;
    std::filesystem::remove(out, ec);
    REQUIRE(io::exportScene(out, scene, io::SceneFormat::FbxBinary).has_value());

#if defined(MH_HAVE_ASSIMP)
    Assimp::Importer importer;
    const aiScene* sc = importer.ReadFile(out.string(), 0);
    REQUIRE(sc != nullptr);
    REQUIRE(sc->mNumMeshes == 2);

    const aiMesh* withMorph = nullptr;
    const aiMesh* without   = nullptr;
    for (unsigned i = 0; i < sc->mNumMeshes; ++i) {
        (sc->mMeshes[i]->mNumAnimMeshes > 0 ? withMorph : without) = sc->mMeshes[i];
    }
    REQUIRE(withMorph != nullptr);
    REQUIRE(without != nullptr);  // the morph did NOT leak onto the other mesh

    REQUIRE(withMorph->mNumAnimMeshes == 1);
    // "smile.smile", not "smile": assimp's FBX round trip names the channel
    // after the deformer that holds it, so the name comes back prefixed. Ours
    // is the part before the dot -- matched as a prefix, the same way the mesh
    // names are matched above, rather than pinning assimp's naming.
    CHECK(std::string(withMorph->mAnimMeshes[0]->mName.C_Str()).rfind("smile", 0) == 0);
    REQUIRE(withMorph->mAnimMeshes[0]->mNumVertices == withMorph->mNumVertices);

    // The export scale is derived, not assumed: the source quad is 2 units
    // wide, so whatever `SceneExportOptions::unit` defaults to (centimetres
    // today, hence x10) this still reads the delta correctly.
    float minX = withMorph->mVertices[0].x;
    float maxX = minX;
    for (unsigned v = 0; v < withMorph->mNumVertices; ++v) {
        minX = std::min(minX, withMorph->mVertices[v].x);
        maxX = std::max(maxX, withMorph->mVertices[v].x);
    }
    const float scale = (maxX - minX) / 2.0F;
    REQUIRE(scale > 0.0F);

    // aiAnimMesh holds ABSOLUTE positions, so the key must sit 2 scaled units
    // above the base -- not at the base (delta dropped) and not at 2*scale
    // (delta written as if it were the position).
    const aiVector3D base = withMorph->mVertices[0];
    const aiVector3D key  = withMorph->mAnimMeshes[0]->mVertices[0];
    INFO("scale " << scale << ", base.y " << base.y << ", key.y " << key.y);
    CHECK(key.y == Catch::Approx(base.y + 2.0F * scale).margin(1e-3));
    CHECK(key.x == Catch::Approx(base.x).margin(1e-3));
#endif
    std::filesystem::remove(out, ec);
}

TEST_CASE("a scene refuses a second morph set, and a mismatched one",
          "[io][scene][multimesh][morph]") {
    const core::Mesh body  = quadAt(0.0F, "body");
    const core::Mesh shirt = quadAt(3.0F, "shirt");
    const auto rmBody      = core::RenderMesh::build(body);
    const auto rmShirt     = core::RenderMesh::build(shirt);

    const std::vector<foundation::Vec3> ok(rmBody.view().vertexCount(),
                                           foundation::Vec3{0.0F, 1.0F, 0.0F});
    const std::vector<foundation::MorphTarget> a{{"a", ok}};

    SECTION("two entries with morphs") {
        const std::vector<foundation::Vec3> ok2(rmShirt.view().vertexCount(),
                                                foundation::Vec3{0.0F, 1.0F, 0.0F});
        const std::vector<foundation::MorphTarget> b{{"b", ok2}};
        const std::vector<io::SceneEntry> scene{{rmBody.view(), "body", nullptr, nullptr, a},
                                                {rmShirt.view(), "shirt", nullptr, nullptr, b}};
        const auto out = tempFile("scene_two_morph", ".fbx");
        const auto r   = io::exportScene(out, scene, io::SceneFormat::FbxBinary);
        REQUIRE_FALSE(r.has_value());
        CHECK(r.error().kind == io::SceneIoErrorKind::InvalidSkinOrMorph);
        std::error_code ec;
        std::filesystem::remove(out, ec);
    }

    SECTION("deltas that do not describe the entry's mesh") {
        // Refused rather than written: a short delta array read past its end,
        // and a long one silently moves the wrong vertices.
        const std::vector<foundation::Vec3> tooShort(2, foundation::Vec3{});
        const std::vector<foundation::MorphTarget> bad{{"bad", tooShort}};
        const std::vector<io::SceneEntry> scene{{rmBody.view(), "body", nullptr, nullptr, bad}};
        const auto out = tempFile("scene_bad_morph", ".fbx");
        const auto r   = io::exportScene(out, scene, io::SceneFormat::FbxBinary);
        REQUIRE_FALSE(r.has_value());
        CHECK(r.error().kind == io::SceneIoErrorKind::InvalidSkinOrMorph);
        std::error_code ec;
        std::filesystem::remove(out, ec);
    }
}
