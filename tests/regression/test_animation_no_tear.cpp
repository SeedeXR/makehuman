// SPDX-License-Identifier: AGPL-3.0-or-later
//
// The shipped animations must not TEAR the mesh.
//
// This gate exists because 1,458 tests were green while every shipped
// animation rendered a body with its arms torn off at the deltoid. Nothing
// caught it, and the reason is instructive: everything that looked at
// animation looked at COUNTS. "walk1 drives 59 of 179 bones" and "frames 0 and
// 7 differ in 14,444 of 14,444 vertices" were both true, both recorded as
// evidence the feature worked, and both equally true of a body being pulled
// apart. A count cannot tell motion from mutilation.
//
// The cause was one line of the retarget table: MakeHuman 1.x `UpArm_L` -- the
// humerus -- was mapped to `shoulder01.L`, the scapula bone this rig
// interposes between the clavicle and the humerus. Driving it swung the whole
// arm about a pivot ~10% of the arm's length too high, so the deltoid skin was
// dragged off the torso while the vertices weighted to `upperarm01` stayed put.
//
// WHAT IS ASSERTED: skinning is very nearly rigid at the scale of one face. A
// rotation, however large, moves neighbouring vertices together; only a pose
// that assigns two adjacent vertices to bones moving apart can stretch the
// edge between them by a large factor. So: pose the mesh, and check that no
// edge of any face grows beyond a bounded multiple of its rest length.
//
// It is deliberately a property of the MESH and not of one bone pair. A test
// asserting "upperarm01.L is the target of UpArm_L" would pin today's table
// and catch nothing else; this catches any mapping, on any rig, that pulls the
// skin apart.
#include "makehuman/core/Mesh.h"
#include "makehuman/core/ObjReader.h"
#include "makehuman/io/GltfWriter.h"
#include "makehuman/rig/PoseUnits.h"
#include "makehuman/rig/RetargetMap.h"
#include "makehuman/rig/Skeleton.h"
#include "makehuman/rig/Skinning.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <string>
#include <vector>

using namespace mh;
namespace fs = std::filesystem;

namespace {

fs::path dataDir() {
    return fs::path(MH_DATA_DIR);
}

/// The worst edge stretch over the VISIBLE mesh, as a multiple of rest length.
///
/// Every edge of every visible face, round the quad, which is the cheapest
/// complete cover of "vertices that are neighbours".
///
/// `staticFaceMask` is not a detail: `base.obj` is 139 face groups of which
/// 138 are `joint-*` markers and `helper-*` cages, 5,108 faces of 18,486, and
/// they are NEVER drawn. They are also barely weighted, so they stretch freely
/// under any pose -- MEASURED, an unmasked version of this check reported
/// 3.62x on walk1 frame 0, a frame whose render is clean. A gate that fires on
/// geometry nobody can see is a gate that gets its bar raised until it fires
/// on nothing.
///
/// Rest edges shorter than a micrometre are skipped: the base mesh has a few
/// coincident corners, and dividing by their length reports an infinity that
/// says nothing about skinning.
double worstStretch(const core::Mesh& mesh, std::span<const foundation::Vec3> posed) {
    const auto rest                    = mesh.coord();
    const std::span<const uint32_t> fv = mesh.fvert();
    const size_t stride                = mesh.vertsPerPrimitive();
    REQUIRE(stride >= 3);
    const std::vector<uint8_t> visible = mesh.staticFaceMask();
    REQUIRE(visible.size() == mesh.faceCount());

    const auto dist = [](const foundation::Vec3& a, const foundation::Vec3& b) {
        const double dx = static_cast<double>(a.x) - static_cast<double>(b.x);
        const double dy = static_cast<double>(a.y) - static_cast<double>(b.y);
        const double dz = static_cast<double>(a.z) - static_cast<double>(b.z);
        return std::sqrt(dx * dx + dy * dy + dz * dz);
    };

    double worst = 0.0;
    for (size_t f = 0; f + stride <= fv.size(); f += stride) {
        if (visible[f / stride] == 0) continue;
        for (size_t c = 0; c < stride; ++c) {
            const uint32_t a = fv[f + c];
            const uint32_t b = fv[f + (c + 1) % stride];
            const double at  = dist(rest[a], rest[b]);
            if (at < 1e-6) continue;
            worst = std::max(worst, dist(posed[a], posed[b]) / at);
        }
    }
    return worst;
}

/// The mesh posed by @p frame of @p bvh, through the same path the application
/// uses: retarget the joint names, take the frame, convert model space to bone
/// local, skin.
std::vector<foundation::Vec3> posedBy(const core::Mesh& mesh, const rig::Skeleton& skeleton,
                                      const rig::CompiledWeights& weights, const fs::path& bvh,
                                      size_t frame, const rig::RetargetMap& names) {
    const auto model = rig::loadBodyPoseFrame(bvh, skeleton, frame, &names);
    REQUIRE(model.has_value());
    const auto local = rig::poseToBoneLocal(skeleton, *model);
    REQUIRE(local.size() == skeleton.boneCount());
    const auto skinning = rig::computeSkinningMatrices(skeleton, local);
    std::vector<foundation::Vec3> posed;
    REQUIRE(rig::skinPositions(mesh.coord(), weights, skinning, posed));
    return posed;
}

struct Rigged {
    core::Mesh mesh;
    rig::Skeleton skeleton;
    rig::CompiledWeights weights;
};

Rigged rigged(const std::string& name) {
    auto mesh = core::loadObj(dataDir() / "3dobjs" / "base.obj");
    REQUIRE(mesh.has_value());
    auto skel = rig::loadSkeleton(dataDir() / "rigs" / (name + ".mhskel"));
    REQUIRE(skel.has_value());
    REQUIRE(skel->updateJoints(mesh->coord()));
    REQUIRE(skel->buildRestMatrices());
    auto weights =
        rig::loadWeights(dataDir() / "rigs" / (name + "_weights.mhw"), mesh->vertexCount());
    REQUIRE(weights.has_value());
    auto compiled = weights->compile(*skel, io::kGltfInfluences);
    return Rigged{std::move(*mesh), std::move(*skel), std::move(compiled)};
}

/// The bar, and why it is where it is.
///
/// MEASURED, worst visible-edge stretch over every frame of every shipped
/// animation, on BOTH rigs, with the arm mapping wrong and then right:
///
///     file              torn (UpArm->shoulder01)   whole (UpArm->upperarm01)
///     walk1.bvh                     6.326x                      3.814x
///     zombieWalk1.bvh               4.217x                      2.837x
///     dance1.bvh                    8.799x                      8.799x
///
/// 5.0 sits a quarter clear of walk1 on both sides. It is not a tight bound on
/// good skinning -- 3.81x is what a legitimate walk already costs -- it is a
/// bound on MUTILATION, which is what a gate nobody looks at should assert.
///
/// It catches the regression on `walk1` and passes `zombieWalk1` either way:
/// the zombie's arms are held out in front and barely rotate, so tearing them
/// only reached 4.2x. Said plainly rather than tuned around, because a second
/// per-file bar at 4.0 would be a 5% margin on a GPU-independent but
/// still-floating-point number, and a bar that thin fails for reasons that
/// have nothing to do with the defect.
constexpr double kMaxStretch = 5.0;
}  // namespace

TEST_CASE("no shipped animation tears the mesh", "[rig][retarget][animation][regression]") {
    const auto table = rig::loadRetargetMap(dataDir() / "rigs" / "makehuman1_retarget.json");
    REQUIRE(table.has_value());

    // BOTH rigs. The defect was present on both, and a gate that checked only
    // the default rig would have been green for `mixamo_superset` users.
    for (const std::string& name : {std::string("default"), std::string("mixamo_superset")}) {
        const Rigged r = rigged(name);
        // `walks/dance1.bvh` is deliberately NOT here. Its single frame is an
        // acrobatic full split -- VERIFIED by rendering it: one leg vertical,
        // the body turned, and every limb attached -- and the groin skin
        // genuinely stretches 8.80x to reach it. That is real deformation, not
        // a tear, and it measures the same whether the arm mapping is right or
        // wrong. Including it would mean a bar above 8.8, which is a bar that
        // catches nothing.
        for (const auto& [file, frames] : {std::pair{fs::path("walks/walk1.bvh"), 14},
                                           std::pair{fs::path("zombie/zombieWalk1.bvh"), 31}}) {
            const fs::path bvh = dataDir() / "animations" / file;
            // EVERY frame. The tear was present in all of them, but a mapping
            // error that only shows at full extension would hide from a spot
            // check of frame 0 -- and frame 0 of a walk is the quietest frame
            // there is.
            for (int f = 0; f < frames; ++f) {
                const auto posed =
                    posedBy(r.mesh, r.skeleton, r.weights, bvh, static_cast<size_t>(f), *table);
                const double worst = worstStretch(r.mesh, posed);
                INFO(name << " " << file.string() << " frame " << f << " stretches an edge "
                          << worst << "x");
                CHECK(worst < kMaxStretch);
            }
        }
    }
}
