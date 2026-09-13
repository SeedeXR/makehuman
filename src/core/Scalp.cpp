// SPDX-License-Identifier: AGPL-3.0-or-later
#include "makehuman/core/Scalp.h"

#include "makehuman/core/Mesh.h"

#include <cmath>
#include <numbers>

namespace mh::core {

float hairlineElevation(float azimuthDeg) {
    return -19.0F + 31.0F * std::cos(azimuthDeg * std::numbers::pi_v<float> / 180.0F);
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
