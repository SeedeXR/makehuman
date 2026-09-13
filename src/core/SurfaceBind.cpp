// SPDX-License-Identifier: AGPL-3.0-or-later
#include "makehuman/core/SurfaceBind.h"

#include "makehuman/core/Mesh.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <vector>

namespace mh::core {

namespace {

using foundation::Vec3;

using foundation::dot;

Vec3 sub(const Vec3& a, const Vec3& b) {
    return Vec3{a.x - b.x, a.y - b.y, a.z - b.z};
}

/// Closest point on triangle (a,b,c) to @p p, as barycentric weights.
///
/// Ericson, Real-Time Collision Detection 5.1.5: the seven Voronoi regions of a
/// triangle, tested in order. Clamping to the triangle rather than solving the
/// unclamped plane projection is what keeps a point beyond an edge bound to
/// THAT edge instead of to weights outside 0..1, which `fitProxy` would happily
/// extrapolate into a position nobody authored.
std::array<float, 3> closestBarycentric(const Vec3& p, const Vec3& a, const Vec3& b,
                                        const Vec3& c) {
    const Vec3 ab  = sub(b, a);
    const Vec3 ac  = sub(c, a);
    const Vec3 ap  = sub(p, a);
    const float d1 = dot(ab, ap);
    const float d2 = dot(ac, ap);
    if (d1 <= 0.0F && d2 <= 0.0F) return {1.0F, 0.0F, 0.0F};

    const Vec3 bp  = sub(p, b);
    const float d3 = dot(ab, bp);
    const float d4 = dot(ac, bp);
    if (d3 >= 0.0F && d4 <= d3) return {0.0F, 1.0F, 0.0F};

    const float vc = d1 * d4 - d3 * d2;
    if (vc <= 0.0F && d1 >= 0.0F && d3 <= 0.0F) {
        const float den = d1 - d3;
        const float v   = den != 0.0F ? d1 / den : 0.0F;
        return {1.0F - v, v, 0.0F};
    }

    const Vec3 cp  = sub(p, c);
    const float d5 = dot(ab, cp);
    const float d6 = dot(ac, cp);
    if (d6 >= 0.0F && d5 <= d6) return {0.0F, 0.0F, 1.0F};

    const float vb = d5 * d2 - d1 * d6;
    if (vb <= 0.0F && d2 >= 0.0F && d6 <= 0.0F) {
        const float den = d2 - d6;
        const float w   = den != 0.0F ? d2 / den : 0.0F;
        return {1.0F - w, 0.0F, w};
    }

    const float va = d3 * d6 - d5 * d4;
    if (va <= 0.0F && (d4 - d3) >= 0.0F && (d5 - d6) >= 0.0F) {
        const float den = (d4 - d3) + (d5 - d6);
        const float w   = den != 0.0F ? (d4 - d3) / den : 0.0F;
        return {0.0F, 1.0F - w, w};
    }

    const float denom = va + vb + vc;
    if (denom == 0.0F) return {1.0F, 0.0F, 0.0F};
    const float v = vb / denom;
    const float w = vc / denom;
    return {1.0F - v - w, v, w};
}

}  // namespace

std::optional<SurfaceBinding> bindToSurface(const Mesh& mesh, std::span<const uint32_t> region,
                                            foundation::Vec3 point) {
    if (region.empty()) return std::nullopt;

    std::vector<uint8_t> inRegion(mesh.vertexCount(), 0U);
    for (const uint32_t v : region) {
        if (v < inRegion.size()) inRegion[v] = 1U;
    }

    const auto coords                  = mesh.coord();
    const std::span<const uint32_t> fv = mesh.fvert();
    const size_t stride                = mesh.vertsPerPrimitive();
    if (stride < 3) return std::nullopt;

    // ponytail: a linear scan of every face, per point. MEASURED: the base mesh
    // has 18,486 quads, so one bind tests 36,972 triangles and a 3,000-vertex
    // style costs about 111M tests -- seconds in a generator that runs when an
    // asset is authored, and never in the application. If a style ever wants
    // tens of thousands of vertices, restrict the scan to the region's own
    // faces or put them in a grid; neither is worth writing before then.
    float best = std::numeric_limits<float>::infinity();
    std::optional<SurfaceBinding> found;

    for (size_t f = 0; f + stride <= fv.size(); f += stride) {
        // Quads split on the first corner, because a barycentric weight is only
        // meaningful on a triangle.
        for (size_t t = 1; t + 1 < stride; ++t) {
            const std::array<uint32_t, 3> tri{fv[f], fv[f + t], fv[f + t + 1]};
            if (std::any_of(tri.begin(), tri.end(), [&](uint32_t v) {
                    return v >= inRegion.size() || inRegion[v] == 0U;
                })) {
                continue;
            }
            const auto w =
                closestBarycentric(point, coords[tri[0]], coords[tri[1]], coords[tri[2]]);
            const Vec3 on{
                coords[tri[0]].x * w[0] + coords[tri[1]].x * w[1] + coords[tri[2]].x * w[2],
                coords[tri[0]].y * w[0] + coords[tri[1]].y * w[1] + coords[tri[2]].y * w[2],
                coords[tri[0]].z * w[0] + coords[tri[1]].z * w[1] + coords[tri[2]].z * w[2]};
            const Vec3 away = sub(point, on);
            const float d2  = dot(away, away);
            if (d2 < best) {
                best  = d2;
                found = SurfaceBinding{tri, w, away};
            }
        }
    }
    return found;
}

}  // namespace mh::core
