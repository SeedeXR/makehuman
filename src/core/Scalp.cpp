// SPDX-License-Identifier: AGPL-3.0-or-later
#include "makehuman/core/Scalp.h"

#include "makehuman/core/Mesh.h"

#include <cmath>
#include <numbers>

namespace mh::core {

float hairlineElevation(float azimuthDeg) {
    // FITTED TO ALL THREE MEASURED ANCHORS, which the previous form could not
    // be. `Scalp.h` has recorded the measurement all along -- forehead +9
    // degrees, ears -25, nape -50 -- while `-19 + 31 cos` returned +12 and -19
    // at the first two. It hit the nape exactly and sat ABOVE the hairline at
    // the forehead and the temples, which is a forehead 5 mm taller than the
    // mesh's own anatomy and a bare patch over each temple.
    //
    // A single cosine cannot pass through three points: fitting the forehead
    // and the ears forces the nape to -59. Adding a cos^2 term gives the third
    // degree of freedom, and the coefficients are then determined rather than
    // chosen -- a = -25 from the ears, b = 29.5 and c = 4.5 from the other two.
    // THE EAR TERM IS -23, NOT THE MEASURED -25, and the two degrees are the
    // MESH's limit rather than the anatomy's. `hairBearingScalp` must be ONE
    // walkable island -- a braid is routed over it by `pathOverSurface`, and a
    // disconnected scrap is somewhere a rope can be rooted and never combed.
    // MEASURED by walking the region's adjacency at each value: one island down
    // to -23, two vertices cut off at -24, eight at -25. So this takes the
    // lowest value the base mesh actually supports and the forehead and nape
    // stay exactly on their measurements.
    const float c = std::cos(azimuthDeg * std::numbers::pi_v<float> / 180.0F);
    return -23.0F + 29.5F * c + 2.5F * c * c;
}

std::vector<uint32_t> hairBearingScalp(const Mesh& mesh) {
    std::vector<uint32_t> scalp;
    const auto body = mesh.findFaceGroup("body");
    if (!body) return scalp;

    const auto fvert    = mesh.fvert();
    const auto fgroup   = mesh.group();
    const size_t stride = mesh.vertsPerPrimitive();
    std::vector<uint8_t> onBody(mesh.vertexCount(), 0U);
    for (size_t f = 0; f < fgroup.size(); ++f) {
        if (fgroup[f] != *body) continue;
        for (size_t c = 0; c < stride; ++c) {
            const uint32_t v = fvert[f * stride + c];
            if (v < onBody.size()) onBody[v] = 1U;
        }
    }

    constexpr float kDeg = 180.0F / std::numbers::pi_v<float>;
    const auto coords    = mesh.coord();
    for (uint32_t v = 0; v < coords.size(); ++v) {
        if (onBody[v] == 0U) continue;
        const float dx        = coords[v].x;
        const float dy        = coords[v].y - kCraniumY;
        const float dz        = coords[v].z - kCraniumZ;
        const float elevation = std::atan2(dy, std::hypot(dx, dz)) * kDeg;
        const float azimuth   = std::atan2(dx, dz) * kDeg;
        if (elevation >= hairlineElevation(azimuth)) scalp.push_back(v);
    }
    return scalp;
}

}  // namespace mh::core
