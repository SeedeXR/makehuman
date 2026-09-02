// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Writes the base mesh out through every exporter, for tools/blender_validate.py
// to check independently. Separate from the test binary because it produces
// files for another process rather than asserting anything itself.
#include "makehuman/core/Blendshape.h"
#include "makehuman/core/ObjReader.h"
#include "makehuman/core/RenderMesh.h"
#include "makehuman/core/Target.h"
#include "makehuman/core/TargetIndex.h"
#include "makehuman/io/GltfWriter.h"
#include "makehuman/io/ObjWriter.h"
#include "makehuman/io/SceneIO.h"
#include "makehuman/io/UsdWriter.h"
#include "makehuman/rig/Skeleton.h"
#include "makehuman/rig/Skinning.h"
#include "makehuman/rig/VertexWeights.h"

#include <array>
#include <cstdio>
#include <filesystem>
#include <vector>

int main(int argc, char** argv) {
    const std::filesystem::path out = (argc > 1) ? argv[1] : ".";
    std::error_code ec;
    std::filesystem::create_directories(out, ec);

    const auto mesh = mh::core::loadObj(std::filesystem::path(MH_DATA_DIR) / "3dobjs" / "base.obj");
    if (!mesh) {
        std::fprintf(stderr, "cannot load base mesh: %s\n", mesh.error().message().c_str());
        return 1;
    }
    const auto rm = mh::core::RenderMesh::build(*mesh);

    if (const auto r = mh::io::writeObj(out / "base.obj", mesh->view()); !r) {
        std::fprintf(stderr, "obj: %s\n", r.error().message().c_str());
        return 1;
    }
    if (const auto r = mh::io::writeGlb(out / "base.glb", rm.view()); !r) {
        std::fprintf(stderr, "glb: %s\n", r.error().message().c_str());
        return 1;
    }
    if (const auto r =
            mh::io::exportScene(out / "base.fbx", rm.view(), mh::io::SceneFormat::FbxBinary);
        !r) {
        std::fprintf(stderr, "fbx: %s\n", r.error().message().c_str());
        return 1;
    }

    // A RIGGED export, so Blender can check the armature and weights rather
    // than only the geometry.
    auto skel =
        mh::rig::loadSkeleton(std::filesystem::path(MH_DATA_DIR) / "rigs" / "default.mhskel");
    if (!skel) {
        std::fprintf(stderr, "skeleton: %s\n", skel.error().message().c_str());
        return 1;
    }
    if (!skel->updateJoints(mesh->coord()) || !skel->buildRestMatrices()) {
        std::fprintf(stderr, "skeleton: could not place the rig\n");
        return 1;
    }
    auto vw = mh::rig::loadWeights(
        std::filesystem::path(MH_DATA_DIR) / "rigs" / "default_weights.mhw", mesh->vertexCount());
    if (!vw) {
        std::fprintf(stderr, "weights: %s\n", vw.error().message().c_str());
        return 1;
    }
    const auto compiled = vw->compile(*skel, mh::io::kGltfInfluences);
    const auto skin     = mh::rig::buildSkinData(*skel, compiled, rm.vmap());
    if (skin.jointNames.empty()) {
        std::fprintf(stderr, "skin: could not expand weights onto render vertices\n");
        return 1;
    }
    const auto skinView = skin.view();
    if (const auto r = mh::io::writeGlb(out / "rigged.glb", rm.view(), {}, nullptr, &skinView);
        !r) {
        std::fprintf(stderr, "rigged glb: %s\n", r.error().message().c_str());
        return 1;
    }
    std::printf("rigged.glb: %zu joints, %zu influences/vertex\n", skin.globalRest.size(),
                static_cast<size_t>(skin.influences));

    // A MORPHED export. The reference's blendshape export is dead in every
    // format (project_context.md 8), so there is no oracle here -- Blender is
    // the primary check rather than a cross-check.
    const std::array<const char*, 3> targetFiles{
        "head/head-oval.target",
        "head/head-trans-backward.target",
        "nose/nose-base-up.target",
    };
    std::vector<std::vector<mh::foundation::Vec3>> deltaStore;
    std::vector<mh::foundation::MorphTarget> morphs;
    deltaStore.reserve(targetFiles.size());
    morphs.reserve(targetFiles.size());

    for (const char* rel : targetFiles) {
        const auto tpath = std::filesystem::path(MH_DATA_DIR) / "targets" / rel;
        auto t           = mh::core::loadTarget(tpath);
        if (!t) {
            std::fprintf(stderr, "target %s: %s\n", rel, t.error().message().c_str());
            return 1;
        }
        std::vector<mh::foundation::Vec3> deltas;
        if (!mh::core::expandTargetToRenderVertices(*t, rm.vmap(), mesh->vertexCount(), deltas)) {
            std::fprintf(stderr, "target %s: does not fit the mesh\n", rel);
            return 1;
        }
        deltaStore.push_back(std::move(deltas));
        morphs.push_back(mh::foundation::MorphTarget{t->name, deltaStore.back()});
    }

    if (const auto r =
            mh::io::writeGlb(out / "morphed.glb", rm.view(), {}, nullptr, nullptr, morphs);
        !r) {
        std::fprintf(stderr, "morphed glb: %s\n", r.error().message().c_str());
        return 1;
    }
    std::printf("morphed.glb: %zu morph targets\n", morphs.size());

    // The SHIPPED blendshape set, as `--blendshapes` writes it: the 34
    // expression units, each blended across the three ethnicities. Separate
    // from morphed.glb, which carries three hand-picked shape targets -- this
    // one exists so Blender checks the path the app actually takes.
    const mh::core::TargetIndex index =
        mh::core::TargetIndex::build(std::filesystem::path(MH_DATA_DIR) / "targets");
    const mh::core::MacroFactors factors;
    const auto shapes =
        mh::core::buildExpressionBlendshapes(index, factors, rm.vmap(), mesh->vertexCount());
    std::vector<mh::foundation::MorphTarget> units;
    units.reserve(shapes.size());
    for (const auto& sh : shapes)
        units.push_back({sh.name, sh.deltas});

    if (const auto r =
            mh::io::writeGlb(out / "expressions.glb", rm.view(), {}, nullptr, nullptr, units);
        !r) {
        std::fprintf(stderr, "expressions glb: %s\n", r.error().message().c_str());
        return 1;
    }
    std::printf("expressions.glb: %zu blendshapes\n", units.size());

    // The same set through the assimp SCENE path -- two entries, only the first
    // carrying morphs. That is the shape `--blendshapes --export x.fbx` writes
    // for a dressed character, and the field io::SceneEntry did not have until
    // this session.
    const std::vector<mh::io::SceneEntry> exprScene{
        {rm.view(), "body", nullptr, nullptr, units},
    };
    if (const auto r =
            mh::io::exportScene(out / "expressions.fbx", exprScene, mh::io::SceneFormat::FbxBinary);
        !r) {
        std::fprintf(stderr, "expressions fbx: %s\n", r.error().message().c_str());
        return 1;
    }
    std::printf("expressions.fbx: %zu blendshapes\n", units.size());

    // And through UsdSkel, with NO skeleton -- the case the application cannot
    // produce (it always builds a rig) and the one that matters: usdchecker
    // rejects SkelBindingAPI on a prim not rooted at a SkelRoot even when there
    // is no skeleton at all, and Blender imports that invalid stage without
    // complaint. run_blender_validation.sh runs usdchecker on this.
    const std::vector<mh::io::UsdSceneEntry> exprStage{
        {rm.view(), "body", nullptr, units},
    };
    if (const auto r = mh::io::writeUsdaScene(out / "expressions.usda", exprStage); !r) {
        std::fprintf(stderr, "expressions usda: %s\n", r.error().message().c_str());
        return 1;
    }
    std::printf("expressions.usda: %zu blendshapes, no skeleton\n", units.size());

    // The same rig and morphs through FBX. assimp's FBX writer was verified to
    // carry both, and Blender to read them back, before this was written.
    if (const auto r =
            mh::io::exportScene(out / "rigged.fbx", rm.view(), mh::io::SceneFormat::FbxBinary, {},
                                nullptr, &skinView, morphs);
        !r) {
        std::fprintf(stderr, "rigged fbx: %s\n", r.error().message().c_str());
        return 1;
    }
    std::printf("rigged.fbx: %zu joints, %zu morph targets\n", skin.globalRest.size(),
                morphs.size());

    if (const auto r = mh::io::writeUsda(out / "base.usda", rm.view()); !r) {
        std::fprintf(stderr, "usda: %s\n", r.error().message().c_str());
        return 1;
    }
    std::printf("base.usda: written\n");

    std::printf(
        "wrote base.obj / base.glb / base.fbx / rigged.glb / morphed.glb / rigged.fbx / base.usda "
        "to %s\n",
        out.c_str());
    return 0;
}
