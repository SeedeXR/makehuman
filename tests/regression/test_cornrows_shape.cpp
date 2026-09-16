// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Cornrows as a SHAPE, not merely as a file that loads.
//
// The 2026-09-11 attempt shipped nothing because the silhouettes were "too
// crude to carry those names", and every count check passed while they were.
// So these assertions are about what a render would show, and each one is a
// defect that was OBSERVED on a real generated asset and measured:
//
//   1. Rows routed by walking base-mesh vertices zigzag. MEASURED max turn
//      along a path: 119.4 degrees for the vertex walk and 180 for a
//      crown-waypoint rule, against 18.6..22.0 for the analytic curve that
//      rendered correctly. Max turn is the one metric that agreed with the
//      render, so it is the gate.
//   2. Rows that do not span the scalp. `row_ends` picked back ends by lowest
//      z and its docstring claimed "every row really does run hairline to
//      nape" on that basis -- measured against the hairline afterwards, those
//      ends sat 52 to 57 degrees ABOVE the nape and the whole occiput was
//      bare, while the front ends sat 3 to 11 degrees BELOW the hairline, on
//      the forehead.
//   3. Long flat facets. Ring spacing along a row was median 0.1320 dm but ran
//      to 0.3870 -- about three tube widths -- so the sweep visibly faceted.
//   4. `fitProxy` does NO clamping (src/core/Proxy.cpp:423-441), so a
//      barycentric weight outside 0..1 silently places a vertex nobody
//      authored. Bound geometry is the first thing in this repo that can
//      produce one.
//
// The rows are recovered from the asset's own vertex order rather than from
// metadata: `ridge()` emits one ring of kSides vertices per path sample, rings
// in path order, rows concatenated. The ring assumption is CHECKED below
// rather than assumed, so a change to the sweep fails loudly here instead of
// making every later measurement meaningless.

#include "makehuman/core/Proxy.h"

#include "makehuman/core/Mesh.h"
#include "makehuman/core/ObjReader.h"
#include "makehuman/core/Scalp.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <vector>

using namespace mh::core;
using mh::foundation::Vec3;

namespace {

/// The sweep's cross-section, `tools/make_hair_styles.py` `ridge(sides=...)`.
constexpr size_t kSides = 6;

/// Rows are concatenated, so a row boundary shows as a jump between
/// consecutive ring centroids. MEASURED: within a row the gap is ~0.13 dm;
/// between the nape end of one row and the hairline start of the next it is
/// more than a whole scalp. Half a scalp width is nowhere near either.
constexpr float kRowBreak = 0.5F;

Vec3 sub(const Vec3& a, const Vec3& b) {
    return {a.x - b.x, a.y - b.y, a.z - b.z};
}

float len(const Vec3& v) {
    return std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
}

/// Distance from the cranium centre -- the frame the standoff is signed in.
float radius(const Vec3& p) {
    const float dy = p.y - kCraniumY;
    const float dz = p.z - kCraniumZ;
    return std::sqrt(p.x * p.x + dy * dy + dz * dz);
}

/// Elevation about the cranium centre, in degrees, and azimuth from the FRONT
/// (+z) -- the frame `mh::core::hairlineElevation` is defined in.
std::pair<float, float> spherical(const Vec3& p) {
    const float dx = p.x;
    const float dy = p.y - kCraniumY;
    const float dz = p.z - kCraniumZ;
    const auto deg = [](float r) { return r * 180.0F / 3.14159265F; };
    return {deg(std::atan2(dy, std::hypot(dx, dz))), deg(std::atan2(dx, dz))};
}

struct Fitted {
    Proxy proxy;
    std::vector<Vec3> points;  ///< the fitted cornrow vertices
    std::vector<Vec3> rings;   ///< one centroid per path sample
    std::vector<size_t> rows;  ///< index into `rings`, one past each row's end
};

Fitted loadCornrows() {
    const auto base = loadObj(std::filesystem::path(MH_DATA_DIR) / "3dobjs" / "base.obj");
    REQUIRE(base.has_value());
    const auto proxy = loadProxy(std::filesystem::path(MH_DATA_DIR) / "hair" / "cornrows.mhclo");
    REQUIRE(proxy.has_value());

    Fitted out{*proxy, {}, {}, {}};
    REQUIRE(fitProxy(*proxy, base->coord(), out.points));
    REQUIRE(out.points.size() == proxy->vertexCount());
    REQUIRE(out.points.size() % kSides == 0);
    // Not redundant with the above, which 0 satisfies: a stripped or empty
    // .mhclo would otherwise leave `rings` empty, and the next test reads
    // `rings[end - 1]` with `end - 1 == SIZE_MAX`. An out-of-bounds read is not
    // a failing gate.
    REQUIRE(out.points.size() > 1000);

    for (size_t i = 0; i < out.points.size(); i += kSides) {
        Vec3 c{0.0F, 0.0F, 0.0F};
        for (size_t k = 0; k < kSides; ++k) {
            c.x += out.points[i + k].x;
            c.y += out.points[i + k].y;
            c.z += out.points[i + k].z;
        }
        c.x /= kSides;
        c.y /= kSides;
        c.z /= kSides;
        // The ring assumption itself: six vertices around one braid section
        // span about a tube, not about a head.
        for (size_t k = 0; k < kSides; ++k) {
            INFO("ring " << i / kSides << " vertex " << k << " is far from its centroid");
            CHECK(len(sub(out.points[i + k], c)) < 0.25F);
        }
        out.rings.push_back(c);
    }

    for (size_t i = 1; i < out.rings.size(); ++i) {
        if (len(sub(out.rings[i], out.rings[i - 1])) > kRowBreak) out.rows.push_back(i);
    }
    out.rows.push_back(out.rings.size());
    return out;
}

}  // namespace

TEST_CASE("the cornrows are smooth rows, not a zigzag over vertices", "[asset][hair][cornrows]") {
    const Fitted f = loadCornrows();

    // Six rows, and each long enough to be a row rather than a stub.
    INFO("rows recovered: " << f.rows.size());
    CHECK(f.rows.size() == 6);

    float worstTurn = 0.0F;
    size_t begin    = 0;
    for (size_t end : f.rows) {
        INFO("row starting at ring " << begin << " has " << (end - begin) << " rings");
        CHECK(end - begin >= 20);
        for (size_t i = begin + 1; i + 1 < end; ++i) {
            const Vec3 a   = sub(f.rings[i], f.rings[i - 1]);
            const Vec3 b   = sub(f.rings[i + 1], f.rings[i]);
            const float la = len(a);
            const float lb = len(b);
            if (la < 1e-5F || lb < 1e-5F) continue;
            const float dot = (a.x * b.x + a.y * b.y + a.z * b.z) / (la * lb);
            worstTurn =
                std::max(worstTurn, std::acos(std::clamp(dot, -1.0F, 1.0F)) * 180.0F / 3.14159265F);
        }
        begin = end;
    }
    // MEASURED on the shipped asset with the formula above: 16.99, 16.99, 23.14,
    // 23.15, 25.70, 25.70 degrees -- the outer rows turn most. The vertex walk
    // this replaced measured 106.3 by the same formula.
    //
    // These are turns between RING CENTROIDS, which is what this test can see,
    // and they are larger than the turns between the generator's own path
    // points (17.7..19.8): a centroid sits 0.55*stand along a FLAT per-triangle
    // normal, so it steps sideways whenever the path crosses a facet. Quoting
    // the generator's figure here would have been quoting a different quantity.
    // 40 leaves 14.3 degrees of headroom and still fails the walk fourfold.
    INFO("largest turn between consecutive ring centroids: " << worstTurn << " deg");
    CHECK(worstTurn < 40.0F);
}

TEST_CASE("every cornrow runs from the hairline to the nape", "[asset][hair][cornrows]") {
    const Fitted f = loadCornrows();

    size_t begin = 0;
    for (size_t end : f.rows) {
        const auto front = spherical(f.rings[begin]);
        const auto back  = spherical(f.rings[end - 1]);
        // The front end starts ON the hairline, not below it on the forehead.
        // A ridge stands 0.13 dm off a ~0.8 dm scalp, which lifts its
        // elevation a little, so the tolerance is one-sided and small.
        const float frontLine = hairlineElevation(front.second);
        const float backLine  = hairlineElevation(back.second);
        INFO("row at ring " << begin << ": front elevation " << front.first << " vs hairline "
                            << frontLine << " at azimuth " << front.second << "; back elevation "
                            << back.first << " vs hairline " << backLine << " at azimuth "
                            << back.second);
        // Both ends are compared against the hairline AT THAT AZIMUTH, which is
        // the correction this test needed twice over. A single global number
        // cannot judge both ends: the hairline is +12 degrees dead front, -19
        // at the ears and -50 at the nape, so demanding -30 of every row's back
        // end asks the outermost row to reach below its own hairline, and
        // `row_ends` made the mirror-image mistake by measuring in z alone.
        //
        // MEASURED on the shipped asset: front ends sit 5.43 to 6.87 degrees
        // above the hairline, back ends 4.61 to 6.53 above it. The back gate
        // therefore has about 1.5 degrees of headroom -- deliberately tight,
        // because the asset is derived deterministically and byte-checked by
        // `hair_assets_are_generated`, so a row that moves wants looking at.
        CHECK(front.first > frontLine - 3.0F);   // not down on the forehead
        CHECK(front.first < frontLine + 12.0F);  // nor started halfway up the crown
        CHECK(back.first < backLine + 8.0F);     // and it reaches the occiput
        begin = end;
    }
}

TEST_CASE("the cornrow sweep is finely sampled and stands off the scalp",
          "[asset][hair][cornrows]") {
    const Fitted f = loadCornrows();

    float worstGap = 0.0F;
    size_t begin   = 0;
    for (size_t end : f.rows) {
        for (size_t i = begin + 1; i < end; ++i) {
            worstGap = std::max(worstGap, len(sub(f.rings[i], f.rings[i - 1])));
        }
        begin = end;
    }
    // MEASURED before the fix: median 0.1320 dm but a MAX of 0.3870, about
    // three tube widths, which renders as a few long flat facets.
    INFO("largest gap between consecutive ring centroids: " << worstGap << " dm");
    CHECK(worstGap < 0.20F);

    // The ridge stands OFF the scalp, and by a bounded amount. `offset` is
    // exactly that standoff -- the fitted point is the barycentric point on the
    // scalp plus this -- so it catches both failures at once: a ridge swept
    // inward buries itself and renders as a bald head, and one swept on the
    // radial direction instead of the surface normal runs away at the front and
    // back, where radial is nearly tangential.
    //
    // MEASURED against the region's triangles: every cornrow vertex sits
    // between 0.0130 and 0.1300 dm off the scalp, and 0.1300 is the sweep's
    // `stand` exactly -- the binder independently recovered the standoff the
    // generator authored.
    // The ridge stands OFF the scalp, and by a bounded amount -- which catches a
    // ridge swept on the radial direction instead of the surface normal, since
    // radial is nearly tangential where the scalp curves away at front and back.
    //
    // The MAGNITUDE alone is not enough, and a mutation proved it: sweeping with
    // `stand = -0.13` buries every braid inside the skull -- which renders as a
    // BALD HEAD -- and |offset| is identical either way, so the first version of
    // this check passed a mutant that deletes the feature. It is measured the
    // way `test_afro_shape.cpp` measures the afro's instead: as the change in
    // distance from the cranium centre, which is signed.
    const auto baseMesh = loadObj(std::filesystem::path(MH_DATA_DIR) / "3dobjs" / "base.obj");
    REQUIRE(baseMesh.has_value());
    const auto coords = baseMesh->coord();
    float worstOffset = 0.0F;
    for (size_t i = 0; i < f.proxy.vertexCount(); ++i) {
        const auto& w = f.proxy.weights[i];
        const auto& r = f.proxy.refVerts[i];
        Vec3 onScalp{0.0F, 0.0F, 0.0F};
        for (size_t k = 0; k < 3; ++k) {
            onScalp.x += w[k] * coords[r[k]].x;
            onScalp.y += w[k] * coords[r[k]].y;
            onScalp.z += w[k] * coords[r[k]].z;
        }
        const float stand = radius(f.points[i]) - radius(onScalp);
        INFO("proxy vertex " << i << " stands off " << stand << " dm");
        CHECK(stand > -1e-3F);
        worstOffset = std::max(worstOffset, stand);
    }
    // MEASURED on the shipped asset: standoff runs 0.011375 to 0.130000 dm,
    // median 0.0860, and 0.1300 is the sweep's `stand` exactly -- the binder
    // independently recovered the standoff the generator authored, which is a
    // cross-check that the geometry and its binding agree rather than merely
    // both existing.
    INFO("largest standoff from the scalp: " << worstOffset << " dm");
    CHECK(worstOffset > 0.10F);
    CHECK(worstOffset < 0.15F);

    // `fitProxy` does no clamping, so an out-of-range weight places a vertex
    // nobody authored. Bound geometry is the first thing here that can make one.
    for (size_t i = 0; i < f.proxy.vertexCount(); ++i) {
        for (size_t k = 0; k < 3; ++k) {
            INFO("proxy vertex " << i << " weight " << k << " = " << f.proxy.weights[i][k]);
            CHECK(f.proxy.weights[i][k] >= -1e-5F);
            CHECK(f.proxy.weights[i][k] <= 1.0F + 1e-5F);
        }
    }
}
