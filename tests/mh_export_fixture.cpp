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

    std::printf("wrote base.obj / base.glb / base.fbx to %s\n", out.c_str());
    return 0;
}
