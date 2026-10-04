// SPDX-License-Identifier: Apache-2.0
#include "makehuman/foundation/FocalShift.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <numbers>
#include <vector>

namespace mh::foundation {

namespace {

/// MoGe solves on a 64x64 downsample. Kept, because it is not an optimisation:
/// the solve is a global fit to a smooth field, and a million points only make
/// it slower and more sensitive to the noisiest ones.
constexpr int kSolveSide = 64;

struct Sample {
    double u, v, x, y, z;
};

/// MoGe's `normalized_view_plane_uv`: normalised by the DIAGONAL, so a square
/// image spans ±0.7071 and the corners always sit at distance 1 from centre.
void uvSpans(int width, int height, double& spanX, double& spanY) {
    const double aspect = static_cast<double>(width) / height;
    const double norm   = std::sqrt(1.0 + aspect * aspect);
    spanX               = aspect / norm;
    spanY               = 1.0 / norm;
}

/// The objective at one shift, and the focal that minimises it there.
///
/// `focal` is NOT searched: at a given shift the least-squares focal is the
/// closed form below, which is what turns a two-variable fit into a
/// one-variable one.
double objectiveAt(const std::vector<Sample>& s, double shift, double& focalOut) {
    double num = 0.0;
    double den = 0.0;
    for (const Sample& p : s) {
        const double d  = p.z + shift;
        const double px = p.x / d;
        const double py = p.y / d;
        num += px * p.u + py * p.v;
        den += px * px + py * py;
    }
    if (den <= 0.0) {
        focalOut = 1.0;
        return std::numeric_limits<double>::infinity();
    }
    const double focal = num / den;
    focalOut           = focal;

    double err = 0.0;
    for (const Sample& p : s) {
        const double d  = p.z + shift;
        const double ex = focal * (p.x / d) - p.u;
        const double ey = focal * (p.y / d) - p.v;
        err += ex * ex + ey * ey;
    }
    return err;
}

}  // namespace

FocalShiftResult recoverFocalShift(std::span<const float> points, int width, int height,
                                   std::span<const uint8_t> mask) {
    FocalShiftResult out;
    if (width < 2 || height < 2) return out;
    const auto needed = static_cast<size_t>(width) * static_cast<size_t>(height);
    if (points.size() < needed * 3) return out;
    const bool masked = mask.size() >= needed;

    double spanX = 0.0;
    double spanY = 0.0;
    uvSpans(width, height, spanX, spanY);

    // Nearest-neighbour downsample. MoGe uses a linear resize when there is no
    // mask; on a 64x64 reduction of a smooth point map the two agree to far
    // better than the fit cares about, and nearest never invents a point
    // between a subject and whatever is behind it.
    std::vector<Sample> samples;
    samples.reserve(static_cast<size_t>(kSolveSide) * kSolveSide);

    // u and v depend only on the column and the row, so build them once. The
    // endpoints are inset by (n-1)/n, which is MoGe's convention: a pixel's uv
    // is its CENTRE, not the edge of the image.
    const auto axis = [](int index, int count, double span) {
        const double extent = span * (count - 1) / count;
        return -extent + 2.0 * extent * index / (count - 1);
    };

    for (int j = 0; j < kSolveSide; ++j) {
        const int y    = std::min(height - 1, j * height / kSolveSide);
        const double v = axis(y, height, spanY);
        for (int i = 0; i < kSolveSide; ++i) {
            const int x = std::min(width - 1, i * width / kSolveSide);
            const auto at =
                static_cast<size_t>(y) * static_cast<size_t>(width) + static_cast<size_t>(x);
            if (masked && mask[at] == 0) continue;
            const double px = static_cast<double>(points[at * 3 + 0]);
            const double py = static_cast<double>(points[at * 3 + 1]);
            const double pz = static_cast<double>(points[at * 3 + 2]);
            if (!std::isfinite(px) || !std::isfinite(py) || !std::isfinite(pz)) continue;
            samples.push_back(
                Sample{.u = axis(x, width, spanX), .v = v, .x = px, .y = py, .z = pz});
        }
    }
    // Two points cannot determine two unknowns plus a residual direction.
    if (samples.size() < 16) return out;

    const auto [lo, hi] =
        std::ranges::minmax_element(samples, {}, [](const Sample& s) { return s.z; });
    const double zMin = lo->z;
    const double zMax = hi->z;

    // The bracket. `z + shift` must stay strictly positive -- a point behind
    // the camera has no projection and the objective has a pole there -- so the
    // search starts just past it. The far end is generous rather than tuned: a
    // shift much larger than the depth range flattens the scene, and the
    // objective is monotone long before then.
    const double span = std::max(1e-3, zMax - zMin);
    double a          = -zMin + 1e-3 * span;
    double b          = -zMin + 40.0 * span;

    // Golden section. See the header for why this and not the reference's
    // Levenberg-Marquardt from zero.
    constexpr double kInvPhi = 0.618033988749894848;
    double c                 = b - kInvPhi * (b - a);
    double d                 = a + kInvPhi * (b - a);
    double focalC            = 1.0;
    double focalD            = 1.0;
    double fc                = objectiveAt(samples, c, focalC);
    double fd                = objectiveAt(samples, d, focalD);
    for (int iter = 0; iter < 200 && (b - a) > 1e-7 * span; ++iter) {
        if (fc < fd) {
            b      = d;
            d      = c;
            fd     = fc;
            focalD = focalC;
            c      = b - kInvPhi * (b - a);
            fc     = objectiveAt(samples, c, focalC);
        } else {
            a      = c;
            c      = d;
            fc     = fd;
            focalC = focalD;
            d      = a + kInvPhi * (b - a);
            fd     = objectiveAt(samples, d, focalD);
        }
    }

    const double shift = 0.5 * (a + b);
    double focal       = 1.0;
    const double err   = objectiveAt(samples, shift, focal);
    if (!std::isfinite(err) || !std::isfinite(focal) || focal <= 0.0) return out;

    out.focal     = static_cast<float>(focal);
    out.shift     = static_cast<float>(shift);
    out.recovered = true;
    return out;
}

float verticalFovDegrees(float focal, int width, int height) {
    if (focal <= 0.0F || width < 1 || height < 1) return 0.0F;
    double spanX = 0.0;
    double spanY = 0.0;
    uvSpans(width, height, spanX, spanY);
    // The top edge of the image sits at v = spanY, and uv = focal * y / z, so
    // the half-angle is atan(spanY / focal).
    const double half = std::atan(spanY / static_cast<double>(focal));
    return static_cast<float>(2.0 * half * 180.0 / std::numbers::pi);
}

}  // namespace mh::foundation
