#include <cstdio>
#include <span>
#include <string>
#include <utility>
#include <vector>
#include "makehuman/core/Mesh.h"
#include "makehuman/core/ObjReader.h"
#include "makehuman/core/RenderMesh.h"
#include "makehuman/foundation/Geometry.h"
#include "makehuman/io/FbxWriter.h"

int main(int argc, char** argv) {
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
    // A rig too, when asked: `mh_fbx_probe out.fbx --rigged`. The whole reason
    // for writing our own FBX is the LIVE rig, so the probe has to be able to
    // produce one for Maya to judge.
    const bool rigged = argc > 2 && std::string(argv[2]) == "--rigged";
    std::vector<mh::foundation::Mat4> rest;
    std::vector<mh::foundation::Mat4> posed;
    std::vector<std::string> names;
    std::vector<int32_t> parents;
    std::vector<uint32_t> joints;
    std::vector<float> weights;
    mh::foundation::SkinView skin;
    if (rigged) {
        // Two joints down the body's height, the child rotated by the pose.
        // Deliberately simple: this is a test of the FILE, not of our skinning,
        // and a rig whose effect is obvious is one whose absence is obvious.
        names             = {"root", "spine"};
        parents           = {-1, 0};
        auto childRest    = mh::foundation::Mat4::identity();
        childRest.m[1][3] = 8.0F;
        rest              = {mh::foundation::Mat4::identity(), childRest};
        auto childPose    = childRest;
        childPose.m[0][3] = 6.0F;
        posed             = {mh::foundation::Mat4::identity(), childPose};

        const size_t n = rm.view().vertexCount();
        joints.assign(n * 4, 0);
        weights.assign(n * 4, 0.0F);
        for (size_t i = 0; i < n; ++i) {
            // Everything above the waist follows the child joint, so the pose
            // visibly bends the figure rather than translating all of it.
            const bool upper = rm.view().coord[i].y > 0.0F;
            joints[i * 4]    = upper ? 1U : 0U;
            weights[i * 4]   = 1.0F;
        }
        skin = mh::foundation::SkinView{.jointNames   = names,
                                        .jointParents = parents,
                                        .globalRest   = rest,
                                        .globalPose   = posed,
                                        .joints       = joints,
                                        .weights      = weights,
                                        .influences   = 4};
        if (!skin.valid()) {
            std::fprintf(stderr, "skin invalid\n");
            return 1;
        }
    }
    // Three blend shapes when asked, so a DCC has something to list and drive.
    // Named after the shipped expression units, since that is what the real
    // export carries.
    const bool morphed = argc > 2 && std::string(argv[2]) == "--morphed";
    std::vector<std::vector<mh::foundation::Vec3>> deltaStore;
    std::vector<mh::foundation::MorphTarget> targets;
    if (morphed) {
        const size_t n = rm.view().vertexCount();
        for (const auto& [name, axis] : std::vector<std::pair<const char*, int>>{
                 {"mouth-open", 1}, {"head-oval", 0}, {"nose-base-up", 2}}) {
            std::vector<mh::foundation::Vec3> d(n, mh::foundation::Vec3{0, 0, 0});
            // A band of the body moves, not all of it: a target that displaces
            // every vertex is indistinguishable from a translation.
            for (size_t i = 0; i < n; ++i) {
                if (rm.view().coord[i].y < 6.0F) continue;
                (&d[i].x)[axis] = 0.3F;
            }
            deltaStore.push_back(std::move(d));
            targets.push_back({name, deltaStore.back()});
        }
    }
    auto r = mh::io::writeFbx(argv[1], rm.view(), {}, &desc, rigged ? &skin : nullptr,
                              morphed ? std::span<const mh::foundation::MorphTarget>(targets)
                                      : std::span<const mh::foundation::MorphTarget>{});
    if (!r) {
        std::fprintf(stderr, "%s\n", r.error().message().c_str());
        return 1;
    }
    std::printf("wrote %s: %zu verts, %zu polys, %zu bytes\n", argv[1], r->vertices, r->polygons,
                r->bytes);
    return 0;
}
