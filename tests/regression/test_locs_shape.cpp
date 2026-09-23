// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Locs as a SHAPE, and specifically as the three shapes that were REVERTED.
//
// Locs took four attempts. Three were built, rendered, looked at and thrown
// away, and every one of them would have passed a file-loads-and-has-vertices
// check. So these assertions are not about locs being locs -- they are one per
// observed, measured failure, so that a regression reproduces a picture
// somebody already rejected:
//
//   1. (2026-09-11) The head was treated as a SPHERE and each rope dropped
//      straight down, burying every rope in the neck.
//   2. (2026-09-23) The rope followed the body's silhouette, and at shoulder
//      height the widest thing in the body is the ARM -- so the side ropes ran
//      out along the arms. MEASURED, with the shelf rule of `loc_path`
//      removed: max radius from the body axis 2.763 dm. With it: 1.433.
//   3. (2026-09-24) Every rope fell vertically from its own root, so ropes
//      rooted near the hairline hung straight across the forehead, eyes and
//      chin. RENDERED and rejected. The fix is the scalp leg: a loc is combed
//      BACK along the scalp to the rim before it hangs.
//
// A fourth defect is not a render but a data hazard, and it applies to every
// bound style: `fitProxy` does NO clamping (src/core/Proxy.cpp:423-441), so a
// barycentric weight outside 0..1 silently places a vertex nobody authored.
//
// Nothing here recovers rope structure from vertex order. Rope lengths differ
// -- the scalp leg is 1 to 18 steps depending on where the root sits -- so a
// fixed chunk size would be a false assumption, and the properties below are
// all statements about the point cloud.

#include "makehuman/core/Proxy.h"

#include "makehuman/core/Mesh.h"
#include "makehuman/core/ObjReader.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <vector>

using namespace mh::core;
using mh::foundation::Vec3;

namespace {

/// The cranium centre, measured and recorded in `memory/todo.md`; the frame
/// the hairline is defined in.
constexpr float kCraniumY = 7.75F;
constexpr float kCraniumZ = 0.50F;

/// The BODY's vertical axis in x,z -- the centroid of torso vertices between
/// y 2.0 and 6.0, and NOT the cranium centre. `tools/make_hair_styles.py`
/// `BODY_AXIS`.
constexpr float kAxisX = 0.0F;
constexpr float kAxisZ = 0.5481F;

/// Where the ropes are cut, `LOC_FLOOR`.
constexpr float kFloor = 5.85F;

std::vector<Vec3> loadLocs() {
    const auto base = loadObj(std::filesystem::path(MH_DATA_DIR) / "3dobjs" / "base.obj");
    REQUIRE(base.has_value());
    const auto proxy = loadProxy(std::filesystem::path(MH_DATA_DIR) / "hair" / "locs.mhclo");
    REQUIRE(proxy.has_value());

    std::vector<Vec3> pts;
    REQUIRE(fitProxy(*proxy, base->coord(), pts));
    REQUIRE(pts.size() == proxy->vertexCount());
    // Not redundant with the above, which 0 satisfies: a stripped .mhclo would
    // make every measurement below vacuous rather than failing.
    REQUIRE(pts.size() > 2000);
    return pts;
}

/// Distance from the BODY axis, which is what a rope's drape is measured in.
float bodyRadius(const Vec3& p) {
    const float dx = p.x - kAxisX;
    const float dz = p.z - kAxisZ;
    return std::sqrt(dx * dx + dz * dz);
}

/// Elevation about the cranium centre, in degrees.
float elevation(const Vec3& p) {
    const float dy = p.y - kCraniumY;
    const float dz = p.z - kCraniumZ;
    return std::atan2(dy, std::hypot(p.x, dz)) * 180.0F / 3.14159265F;
}

}  // namespace

TEST_CASE("no loc hangs across the face", "[asset][hair][locs]") {
    // Attempt 3's reason for being reverted, and it is invisible to any count.
    // A rope that falls vertically from a root on the hairline crosses the
    // forehead, the eyes and the chin, and the asset still loads, still has
    // 42 ropes and still stands off the scalp correctly.
    //
    // The face is what lies in FRONT of the cranium centre, BELOW the brow
    // (about -13 degrees of elevation in this frame) and NEAR THE MIDLINE. The
    // last clause is the one that does the work, and it was arrived at by
    // measuring rather than guessed: the shipped asset has 35 vertices in
    // front and below the brow, and every one of them is at |x| 0.68..0.73 --
    // the temple, above the ear, where hair belongs. Not one is within 0.5 dm
    // of the midline. The face proper reaches z 1.681 there on the base mesh;
    // these reach 0.910 at the widest part of the head.
    //
    // A first cut of this test bounded the furthest-forward z instead and had
    // to be corrected: the bar was taken from the rope PATH, and the asset
    // carries the tube's vertices, which stand off it by the tube's radius.
    const auto pts = loadLocs();
    size_t onFace  = 0;
    for (const auto& p : pts) {
        if (p.z > kCraniumZ && elevation(p) < -13.0F && std::abs(p.x) < 0.5F) ++onFace;
    }
    INFO("vertices over the face: " << onFace);
    CHECK(onFace == 0);
}

TEST_CASE("no loc runs out along the arm", "[asset][hair][locs]") {
    // Attempt 2's failure, and the one the shelf-versus-wall rule exists for.
    // At shoulder height the widest thing in the body is the arm, so a rope
    // that follows the silhouette is carried out along it.
    //
    // MEASURED over the 42 ropes: with the rule, max radius from the body axis
    // is 1.433 dm; with it removed and nothing else changed, 2.763. The bar
    // sits between them and near neither, so it fails on a rope that reaches
    // for the arm and passes with room on one that does not.
    const auto pts = loadLocs();
    float worst    = 0.0F;
    for (const auto& p : pts)
        worst = std::max(worst, bodyRadius(p));
    INFO("furthest any loc vertex sits from the body axis: " << worst);
    CHECK(worst < 2.0F);
}

TEST_CASE("the locs are cut level", "[asset][hair][locs]") {
    // Attempt 3 again, from the other side: the shelf rule TERMINATES a rope
    // where it lands, so ropes finished at many different heights and the
    // style read as a torn curtain -- ends spread y 4.60..5.95, sd 0.539.
    //
    // Stated as a property of the point cloud rather than per rope, because
    // rope lengths differ: if every rope reaches the floor, its whole bottom
    // ring sits there, so the number of vertices at the lowest height is a
    // multiple of the ropes. MEASURED on the shipped asset: 42 ropes x 5 sides
    // = 210. A ragged style has a handful.
    const auto pts = loadLocs();
    float lowest   = pts.front().y;
    for (const auto& p : pts)
        lowest = std::min(lowest, p.y);
    INFO("lowest loc vertex: " << lowest);
    CHECK(std::abs(lowest - kFloor) < 0.10F);

    size_t atFloor = 0;
    for (const auto& p : pts) {
        if (p.y < lowest + 0.02F) ++atFloor;
    }
    INFO("vertices at the cut line: " << atFloor);
    CHECK(atFloor >= 150);
}

TEST_CASE("every loc binding is inside its triangle", "[asset][hair][locs]") {
    // `fitProxy` does no clamping, so a weight outside 0..1 places a vertex
    // nobody authored and nothing reports it. Bound geometry is the only thing
    // in this repo that can produce one, and locs bind 42 ropes plus a cap.
    const auto proxy = loadProxy(std::filesystem::path(MH_DATA_DIR) / "hair" / "locs.mhclo");
    REQUIRE(proxy.has_value());
    REQUIRE(proxy->weights.size() == proxy->refVerts.size());

    size_t outside = 0;
    for (const auto& w : proxy->weights) {
        const float sum = w[0] + w[1] + w[2];
        if (w[0] < 0.0F || w[1] < 0.0F || w[2] < 0.0F || std::abs(sum - 1.0F) > 1e-3F) ++outside;
    }
    INFO("bindings with a weight outside 0..1 or not summing to 1: " << outside);
    CHECK(outside == 0);
}
