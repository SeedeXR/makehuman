// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Translation-capable pose blending (M9).
//
// `PoseUnits::blend` turned every unit into a QUATERNION and back, so the
// translation part of a unit was discarded without a word. A jaw slide or a lip
// purse is a translation; neither was expressible, however it was authored.
//
// The shipped 60 face units are unaffected, and that is measured rather than
// assumed: every position channel in `face-poseunits.bvh` is zero on every
// frame, so this changes no expression that exists today. What it changes is
// what CAN be authored tomorrow.
#include "makehuman/core/Mesh.h"
#include "makehuman/core/ObjReader.h"
#include "makehuman/foundation/Geometry.h"
#include "makehuman/io/GltfWriter.h"
#include "makehuman/rig/PoseUnits.h"
#include "makehuman/rig/Skeleton.h"
#include "makehuman/rig/Skinning.h"
#include "makehuman/rig/VertexWeights.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <filesystem>
#include <numbers>
#include <optional>
#include <vector>

using namespace mh;
using Catch::Matchers::WithinAbs;

namespace {

/// A rigid transform: rotation about Y by @p degrees, then @p t.
foundation::Mat4 rigid(double degrees, foundation::Vec3 t) {
    const double r     = degrees * std::numbers::pi / 180.0;
    foundation::Mat4 m = foundation::Mat4::identity();
    m.m[0][0]          = static_cast<float>(std::cos(r));
    m.m[0][2]          = static_cast<float>(std::sin(r));
    m.m[2][0]          = static_cast<float>(-std::sin(r));
    m.m[2][2]          = static_cast<float>(std::cos(r));
    m.m[0][3]          = t.x;
    m.m[1][3]          = t.y;
    m.m[2][3]          = t.z;
    return m;
}

/// Two single-bone units, so a blend's arithmetic is readable.
rig::PoseUnits twoUnits(const foundation::Mat4& a, const foundation::Mat4& b) {
    rig::PoseUnits u;
    u.names     = {"a", "b"};
    u.boneCount = 1;
    u.data      = {a, b};
    return u;
}

}  // namespace

TEST_CASE("a unit's translation survives the blend, scaled by its weight", "[poseblend]") {
    const rig::PoseUnits units =
        twoUnits(rigid(0.0, {2.0F, 0.0F, 0.0F}), foundation::Mat4::identity());

    const std::array<size_t, 1> which{0};
    const std::array<float, 1> half{0.5F};
    const auto out = units.blend(which, half);
    REQUIRE(out.size() == 1);

    // Half of a 2 dm slide is 1 dm. Linear in the weight, which is what
    // "half of this expression" has to mean for a translation.
    CHECK_THAT(out[0].m[0][3], WithinAbs(1.0, 1e-5));
    CHECK_THAT(out[0].m[1][3], WithinAbs(0.0, 1e-5));

    const std::array<float, 1> none{0.0F};
    const auto zero = units.blend(which, none);
    REQUIRE(zero.size() == 1);
    CHECK_THAT(zero[0].m[0][3], WithinAbs(0.0, 1e-5));
}

TEST_CASE("blending composes rigid transforms, not rotations beside translations", "[poseblend]") {
    // One unit rotates, the other slides. Composed at full weight the answer is
    // the matrix product -- the second unit's rotation carries the first's
    // translation with it. Adding the translations independently of the
    // rotations would give a different, plausible, wrong answer, so this
    // compares against the product rather than against a hand-written vector.
    const foundation::Mat4 slide = rigid(0.0, {3.0F, 0.0F, 0.0F});
    const foundation::Mat4 turn  = rigid(90.0, {0.0F, 0.0F, 0.0F});
    const rig::PoseUnits units   = twoUnits(slide, turn);
    const std::array<size_t, 2> which{0, 1};
    const std::array<float, 2> full{1.0F, 1.0F};

    const auto out = units.blend(which, full);
    REQUIRE(out.size() == 1);

    // `blend` left-multiplies each later unit, so this is turn * slide.
    const foundation::Mat4 expected = turn * slide;
    for (size_t r = 0; r < 4; ++r) {
        for (size_t c = 0; c < 4; ++c) {
            CAPTURE(r, c);
            CHECK_THAT(out[0].m[r][c], WithinAbs(static_cast<double>(expected.m[r][c]), 1e-5));
        }
    }

    // ...and the reverse order gives the other product, which is the
    // order-dependence the reference has and this must keep.
    const std::array<size_t, 2> reversed{1, 0};
    const auto other = units.blend(reversed, full);
    REQUIRE(other.size() == 1);
    const foundation::Mat4 alsoExpected = slide * turn;
    CHECK_THAT(other[0].m[0][3], WithinAbs(static_cast<double>(alsoExpected.m[0][3]), 1e-5));
    CHECK(std::abs(other[0].m[2][3] - out[0].m[2][3]) > 1.0F);
}

TEST_CASE("a rotation-only blend is exactly what it was before", "[poseblend]") {
    // The regression that matters: the 60 shipped face units carry no
    // translation at all, so every expression the application can build today
    // must come out of the new code unchanged -- zero in, zero out.
    const rig::PoseUnits units =
        twoUnits(rigid(30.0, {0.0F, 0.0F, 0.0F}), rigid(-70.0, {0.0F, 0.0F, 0.0F}));
    const std::array<size_t, 2> which{0, 1};
    const std::array<float, 2> weights{0.6F, 0.9F};

    const auto out = units.blend(which, weights);
    REQUIRE(out.size() == 1);
    CHECK_THAT(out[0].m[0][3], WithinAbs(0.0, 1e-9));
    CHECK_THAT(out[0].m[1][3], WithinAbs(0.0, 1e-9));
    CHECK_THAT(out[0].m[2][3], WithinAbs(0.0, 1e-9));
    // Still a rotation: the bottom row is untouched and the basis stays unit.
    CHECK_THAT(out[0].m[3][3], WithinAbs(1.0, 1e-9));
    const double len = std::sqrt(static_cast<double>(out[0].m[0][0] * out[0].m[0][0] +
                                                     out[0].m[1][0] * out[0].m[1][0] +
                                                     out[0].m[2][0] * out[0].m[2][0]));
    CHECK_THAT(len, WithinAbs(1.0, 1e-5));
}

// The point of all this, end to end: a translating unit must MOVE THE MESH.
// Everything above is matrix arithmetic; this is the property the milestone
// item actually asks for -- "jaw slide and lip pursing are currently
// inexpressible" -- and it is only true if the translation survives the blend,
// the skinning matrices and the skinning itself.
TEST_CASE("a blended translation reaches the vertices", "[poseblend]") {
    auto mesh = core::loadObj(std::filesystem::path(MH_DATA_DIR) / "3dobjs" / "base.obj");
    REQUIRE(mesh.has_value());
    auto skel = rig::loadSkeleton(std::filesystem::path(MH_DATA_DIR) / "rigs" / "default.mhskel");
    REQUIRE(skel.has_value());
    REQUIRE(skel->updateJoints(mesh->coord()));
    REQUIRE(skel->buildRestMatrices());
    auto weights = rig::loadWeights(
        std::filesystem::path(MH_DATA_DIR) / "rigs" / "default_weights.mhw", mesh->vertexCount());
    REQUIRE(weights.has_value());
    const rig::CompiledWeights compiled = weights->compile(*skel, io::kGltfInfluences);

    // One unit that slides a single bone along X and does nothing else.
    // The bone with the most vertices of its own, so the movement is
    // unmistakable and does not depend on which name the shipped rig uses.
    std::vector<size_t> owned(skel->boneCount(), 0);
    for (size_t v = 0; v < mesh->vertexCount(); ++v) {
        for (uint8_t i = 0; i < compiled.influences; ++i) {
            const size_t at = v * compiled.influences + i;
            if (compiled.weight[at] > 0.5F) ++owned[compiled.boneIndex[at]];
        }
    }
    const size_t bone = static_cast<size_t>(std::ranges::max_element(owned) - owned.begin());
    REQUIRE(owned[bone] > 100);

    rig::PoseUnits units;
    units.names     = {"slide"};
    units.boneCount = skel->boneCount();
    units.data.assign(units.boneCount, foundation::Mat4::identity());
    units.data[bone].m[0][3] = 1.0F;  // 1 dm, far larger than any real slide

    const std::array<size_t, 1> which{0};
    const std::array<float, 1> full{1.0F};
    const auto pose = units.blend(which, full);
    REQUIRE(pose.size() == units.boneCount);

    const auto skinning = rig::computeSkinningMatrices(*skel, pose);
    std::vector<foundation::Vec3> posed;
    REQUIRE(rig::skinPositions(mesh->coord(), compiled, skinning, posed));
    REQUIRE(posed.size() == mesh->vertexCount());

    // The vertices this bone owns moved, and by close to the full slide where
    // it owns them outright. Before this change the whole mesh sat still.
    float worst = 0.0F;
    for (size_t v = 0; v < posed.size(); ++v) {
        const foundation::Vec3 d{posed[v].x - mesh->coord()[v].x, posed[v].y - mesh->coord()[v].y,
                                 posed[v].z - mesh->coord()[v].z};
        worst = std::max(worst, std::sqrt(d.x * d.x + d.y * d.y + d.z * d.z));
    }
    INFO("largest vertex movement: " << worst);
    CHECK(worst > 0.9F);

    // ...and they move TOGETHER. Every vertex owned outright by this bone must
    // shift by the same vector, or the translation is being applied in a space
    // that varies per vertex -- which is what tearing looks like, and what a
    // "did anything move?" assertion cannot see.
    std::optional<foundation::Vec3> first;
    float spread = 0.0F;
    size_t solid = 0;
    for (size_t v = 0; v < posed.size(); ++v) {
        bool owned100 = false;
        for (uint8_t i = 0; i < compiled.influences; ++i) {
            const size_t at = v * compiled.influences + i;
            if (compiled.boneIndex[at] == bone && compiled.weight[at] > 0.999F) owned100 = true;
        }
        if (!owned100) continue;
        ++solid;
        const foundation::Vec3 d{posed[v].x - mesh->coord()[v].x, posed[v].y - mesh->coord()[v].y,
                                 posed[v].z - mesh->coord()[v].z};
        if (!first) {
            first = d;
            continue;
        }
        spread = std::max(
            {spread, std::abs(d.x - first->x), std::abs(d.y - first->y), std::abs(d.z - first->z)});
    }
    REQUIRE(solid > 50);
    INFO("vertices owned outright: " << solid << ", spread " << spread);
    CHECK(spread < 1e-4F);
}
