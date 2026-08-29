// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Writes the base mesh out through every exporter, for tools/blender_validate.py
// to check independently. Separate from the test binary because it produces
// files for another process rather than asserting anything itself.
#include "makehuman/core/ObjReader.h"
#include "makehuman/core/RenderMesh.h"
#include "makehuman/io/GltfWriter.h"
#include "makehuman/io/ObjWriter.h"
#include "makehuman/io/SceneIO.h"
#include "makehuman/rig/Skeleton.h"
#include "makehuman/rig/Skinning.h"
#include "makehuman/rig/VertexWeights.h"

#include <cstdio>
#include <filesystem>

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

    std::printf("wrote base.obj / base.glb / base.fbx / rigged.glb to %s\n", out.c_str());
    return 0;
}
