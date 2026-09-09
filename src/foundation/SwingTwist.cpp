// SPDX-License-Identifier: Apache-2.0
//
// See the header for why this is not in Transform.cpp and what it is not a
// port of.
#include "makehuman/foundation/SwingTwist.h"

#include <array>
#include <cmath>
#include <optional>

namespace mh::foundation {

namespace {

/// Matches Transform.cpp's tolerance, so the two files agree about when a
/// vector is too short to have a direction.
constexpr double kEps = 2.220446049250313e-16 * 4.0;

/// @p v normalised in double precision, or nothing if it is too short to name
/// a direction.
///
/// -Wdouble-promotion is on, so every float->double widening is explicit, and
/// doing that once here is the reason this is a function: both entry points
/// need the same six lines and the same zero-length answer.
std::optional<std::array<double, 3>> unitAxis(const Vec3& v) {
    const std::array<double, 3> d{static_cast<double>(v.x), static_cast<double>(v.y),
                                  static_cast<double>(v.z)};
    const double len = std::sqrt(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
    if (len < kEps) return std::nullopt;
    return std::array<double, 3>{d[0] / len, d[1] / len, d[2] / len};
}

}  // namespace

SwingTwist swingTwist(const Quat& q, const Vec3& twistAxis) {
    const auto axis = unitAxis(twistAxis);
    if (!axis) return {.swing = q, .twist = Quat{}};
    const auto [nx, ny, nz] = *axis;

    // The twist is the part of the rotation whose axis IS the twist axis, so
    // its vector part is q's vector part projected onto that axis. The scalar
    // part carries over unchanged: a rotation about the axis and the same
    // rotation composed with a swing agree on how far round the axis they went.
    const double proj = q.x * nx + q.y * ny + q.z * nz;
    Quat twist{q.w, proj * nx, proj * ny, proj * nz};

    const double norm =
        std::sqrt(twist.w * twist.w + twist.x * twist.x + twist.y * twist.y + twist.z * twist.z);
    if (norm < kEps) {
        // The singularity: a half turn about an axis perpendicular to the twist
        // axis leaves both the scalar part and the projection at zero, and then
        // EVERY twist angle recomposes to the same rotation -- the twist is not
        // hard to compute here, it does not exist. Reported as no twist, which
        // is what the twist tends to as the half turn is approached.
        return {.swing = q, .twist = Quat{}};
    }
    twist = Quat{twist.w / norm, twist.x / norm, twist.y / norm, twist.z / norm};

    // swing = q * twist^-1, which makes q = swing * twist. twist is a unit
    // quaternion, so its inverse is its conjugate.
    const Quat swing = quaternionMultiply(q, Quat{twist.w, -twist.x, -twist.y, -twist.z});
    return {.swing = swing, .twist = twist};
}

double twistAngle(const Quat& twist, const Vec3& twistAxis) {
    const auto axis = unitAxis(twistAxis);
    if (!axis) return 0.0;

    const double proj = twist.x * (*axis)[0] + twist.y * (*axis)[1] + twist.z * (*axis)[2];

    // atan2, not 2*acos(w). This is the number an RBF keys on and small angles
    // are exactly where a pose near rest lives, which is where acos falls
    // apart -- measured on this machine, relative error of 2*acos(w) against
    // the angle the quaternion was built from:
    //
    //     1e-1 rad -> 1.5e-14      1e-5 rad -> 4.1e-8
    //     1e-3 rad -> 1.7e-10      1e-7 rad -> 1.2e-2      1e-8 rad -> 1.0
    //
    // At a hundredth of a degree acos is 1% wrong and below that it returns
    // zero. atan2 was exact to the last bit at every one of those angles, and
    // it gives the sign for free.
    //
    // Negating both arguments when w < 0 is the q/-q double cover: the same
    // rotation must read the same, and it puts the result on (-pi, pi].
    return twist.w < 0.0 ? 2.0 * std::atan2(-proj, -twist.w) : 2.0 * std::atan2(proj, twist.w);
}

std::array<double, 3> rotationVector(const Quat& q) {
    // Shortest path: q and -q are the same rotation, and taking the one with a
    // non-negative scalar part puts the angle on [0, pi].
    const Quat p = q.w < 0.0 ? Quat{-q.w, -q.x, -q.y, -q.z} : q;

    const double vlen = std::sqrt(p.x * p.x + p.y * p.y + p.z * p.z);
    if (vlen < kEps) return {0.0, 0.0, 0.0};

    // atan2 for the same reason as above, and with the same freedom from a
    // clamp: `acos` is NaN for an argument even slightly above 1, so it needs
    // one, whereas atan2 has no domain to leave. (Squaring unit quaternions
    // across a full turn got w to exactly 1.0 here and no further, so this is
    // the cheaper guarantee rather than an observed failure.)
    const double angle = 2.0 * std::atan2(vlen, p.w);
    const double s     = angle / vlen;
    return {p.x * s, p.y * s, p.z * s};
}

}  // namespace mh::foundation
