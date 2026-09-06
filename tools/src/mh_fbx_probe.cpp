#include <cstdio>
#include "makehuman/core/Mesh.h"
#include "makehuman/core/ObjReader.h"
#include "makehuman/core/RenderMesh.h"
#include "makehuman/foundation/Geometry.h"
#include "makehuman/io/FbxWriter.h"

int main(int, char** argv) {
    auto m = mh::core::loadObj(std::filesystem::path(MH_DATA_DIR) / "3dobjs" / "base.obj");
    if (!m) {
        std::fprintf(stderr, "mesh\n");
        return 1;
    }
    m->buildAdjacency();
    m->calcNormals();
    auto rm         = mh::core::RenderMesh::build(*m);
    const auto mask = m->staticFaceMask();
    rm.setFaceMask(*m, mask);
    // A real material, so the file exercises UVs, colours and a texture --
    // which is what a DCC actually has to agree with us about.
    mh::foundation::MaterialDesc desc;
    desc.name           = "Skin";
    desc.diffuse        = {0.76F, 0.62F, 0.53F};
    desc.specular       = {0.1F, 0.1F, 0.1F};
    desc.shininess      = 0.25F;
    desc.diffuseTexture = "textures/skin/african_deep.png";
    auto r              = mh::io::writeFbx(argv[1], rm.view(), {}, &desc);
    if (!r) {
        std::fprintf(stderr, "%s\n", r.error().message().c_str());
        return 1;
    }
    std::printf("wrote %s: %zu verts, %zu polys, %zu bytes\n", argv[1], r->vertices, r->polygons,
                r->bytes);
    return 0;
}
