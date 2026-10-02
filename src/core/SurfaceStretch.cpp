// SPDX-License-Identifier: AGPL-3.0-or-later
#include "makehuman/core/SurfaceStretch.h"

#include "makehuman/core/Mesh.h"

#include <cmath>

namespace mh::core {

std::vector<float> surfaceStretch(const Mesh& rest, std::span<const foundation::Vec3> posed) {
    const size_t n = rest.vertexCount();
    if (posed.size() != n) return {};

    const auto coord    = rest.coord();
    const auto fv       = rest.fvert();
    const size_t stride = rest.vertsPerPrimitive();
    if (stride < 2) return {};

    // Summed ratio and the count it came from, so the mean needs one pass.
    std::vector<float> total(n, 0.0F);
    std::vector<uint32_t> seen(n, 0U);

    const auto edge = [&](uint32_t a, uint32_t b) {
        if (a >= n || b >= n) return;
        const auto& ra       = coord[a];
        const auto& rb       = coord[b];
        const double rx      = static_cast<double>(rb.x) - static_cast<double>(ra.x);
        const double ry      = static_cast<double>(rb.y) - static_cast<double>(ra.y);
        const double rz      = static_cast<double>(rb.z) - static_cast<double>(ra.z);
        const double restLen = std::sqrt(rx * rx + ry * ry + rz * rz);
        // A zero-length rest edge has no ratio to give. The base mesh has
        // coincident vertices where helper cages meet, and dividing by that
        // would put an infinity into a buffer the GPU reads.
        if (!(restLen > 1e-9)) return;

        const auto& pa        = posed[a];
        const auto& pb        = posed[b];
        const double px       = static_cast<double>(pb.x) - static_cast<double>(pa.x);
        const double py       = static_cast<double>(pb.y) - static_cast<double>(pa.y);
        const double pz       = static_cast<double>(pb.z) - static_cast<double>(pa.z);
        const double posedLen = std::sqrt(px * px + py * py + pz * pz);

        const auto ratio = static_cast<float>(posedLen / restLen);
        total[a] += ratio;
        total[b] += ratio;
        ++seen[a];
        ++seen[b];
    };

    // Every face's perimeter. A quad contributes its four sides and not its
    // diagonals, which is what makes this a measure of the SURFACE rather than
    // of the solid: the diagonals of a quad change under a fold that leaves the
    // skin itself unstretched.
    for (size_t f = 0; f + stride <= fv.size(); f += stride) {
        for (size_t k = 0; k < stride; ++k) {
            edge(fv[f + k], fv[f + ((k + 1) % stride)]);
        }
    }

    std::vector<float> out(n, 1.0F);
    for (size_t i = 0; i < n; ++i) {
        // No incident edge means no evidence, and 1.0 means "unchanged" --
        // which is the honest answer. Zero would read as total compression and
        // light a loose vertex as a deep crease.
        if (seen[i] != 0U) out[i] = total[i] / static_cast<float>(seen[i]);
    }
    return out;
}

}  // namespace mh::core
