// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Rebuilding a POSED scene has to give the same mesh every time, and it did
// not. `--pose tpose` plus ANY second rebuild -- moving a slider, changing
// units, switching skinning method -- re-morphed and re-posed on top of the
// previous result: the arms swung up, then the torso tore open along the
// helper seams. Every one of the 638 tests stayed green, because none of them
// ever built the same scene twice. It was only ever visible in the picture.
//
// Root cause: `Mesh::setCoords` redefines the morph base (`origCoord_`,
// mirroring `module3d.py:532`), and the app stored the POSED vertices through
// it -- so the posed mesh became the base that `Human::applyStack` resets to.
// The reference splits the two deliberately and poses through `changeCoords`
// (`module3d.py:591`, used for skinning at `shared/animation.py:1092` and for
// proxy fitting at `shared/proxy.py:227`), which writes positions and leaves
// `orig_coord` alone. So do we now.
#include "makehuman/core/Mesh.h"
#include "makehuman/core/ObjReader.h"
#include "makehuman/io/GltfWriter.h"
#include "makehuman/rig/PoseUnits.h"
#include "makehuman/rig/Skeleton.h"
#include "makehuman/rig/Skinning.h"
#include "makehuman/rig/VertexWeights.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <span>
#include <vector>

using namespace mh;

namespace {

std::filesystem::path data(const char* rel) {
    return std::filesystem::path(MH_DATA_DIR) / rel;
}

float maxDistance(std::span<const foundation::Vec3> a, std::span<const foundation::Vec3> b) {
    REQUIRE(a.size() == b.size());
    float worst = 0.0F;
    for (size_t i = 0; i < a.size(); ++i) {
        const float dx = a[i].x - b[i].x;
        const float dy = a[i].y - b[i].y;
        const float dz = a[i].z - b[i].z;
        worst          = std::max(worst, std::sqrt(dx * dx + dy * dy + dz * dz));
    }
    return worst;
}

}  // namespace

// The app's rebuild, reduced to the two calls that matter: back to the morph
// base, then pose. `Human::applyStack` starts with exactly this reset
// (`Modifier.cpp:331`), so an empty modifier stack makes the two identical.
TEST_CASE("rebuilding a posed scene twice gives the same mesh", "[regression][pose]") {
    auto mesh = core::loadObj(data("3dobjs/base.obj"));
    REQUIRE(mesh.has_value());
    const std::vector<foundation::Vec3> rest(mesh->coord().begin(), mesh->coord().end());

    auto skel = rig::loadSkeleton(data("rigs/default.mhskel"));
    REQUIRE(skel.has_value());
    REQUIRE(skel->updateJoints(mesh->coord()));
    REQUIRE(skel->buildRestMatrices());

    auto weights = rig::loadWeights(data("rigs/default_weights.mhw"), mesh->vertexCount());
    REQUIRE(weights.has_value());
    const rig::CompiledWeights compiled = weights->compile(*skel, io::kGltfInfluences);

    const auto bodyPose = rig::loadBodyPose(data("poses/tpose.bvh"), *skel);
    REQUIRE(bodyPose.has_value());
    const std::vector<foundation::Mat4> localPose = rig::poseToBoneLocal(*skel, *bodyPose);

    const auto rebuild = [&] {
        mesh->resetToOriginal();
        REQUIRE(skel->updateJoints(mesh->coord()));
        REQUIRE(skel->buildRestMatrices());
        const auto skinning = rig::computeSkinningMatrices(*skel, localPose);
        std::vector<foundation::Vec3> posed;
        REQUIRE(rig::skinPositions(mesh->coord(), compiled, skinning, posed));
        REQUIRE(mesh->changeCoords(std::move(posed)).has_value());
    };

    rebuild();
    const std::vector<foundation::Vec3> first(mesh->coord().begin(), mesh->coord().end());
    // The pose has to DO something, or every assertion below passes on a rig
    // that never moved a vertex.
    CHECK(maxDistance(rest, first) > 1.0F);

    rebuild();
    // Bit-for-bit: the same inputs through the same code. Anything else means
    // the second pass started somewhere other than the morph base.
    CHECK(maxDistance(first, mesh->coord()) == 0.0F);

    rebuild();
    CHECK(maxDistance(first, mesh->coord()) == 0.0F);

    // The other half of the same fact, stated where the app depends on it:
    // applyStack resets to the morph base before replaying the stack, so if
    // posing has moved that base, every modifier is re-applied to a posed mesh.
    mesh->resetToOriginal();
    CHECK(maxDistance(rest, mesh->coord()) == 0.0F);
}
