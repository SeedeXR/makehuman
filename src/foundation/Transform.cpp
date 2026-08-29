// SPDX-License-Identifier: BSD-3-Clause
//
// Ported from legacy/python/core/transformations.py, which is Christoph
// Gohlke's BSD-3-Clause library.
//
// BSD's first condition is that "redistributions of source code must retain
// the above copyright notice", so it is repeated here rather than deferred to
// the header -- this file is itself a redistribution. The full text of the
// conditions and disclaimer is in foundation/Transform.h.
//
// Copyright (c) 2006-2012, Christoph Gohlke
// Copyright (c) 2006-2012, The Regents of the University of California
// Produced at the Laboratory for Fluorescence Dynamics
// All rights reserved.
#include "makehuman/foundation/Transform.h"

#include <cmath>

namespace mh::foundation {
namespace {

/// transformations.py `_NEXT_AXIS`.
constexpr std::array<int, 4> kNextAxis{1, 2, 0, 1};

/// transformations.py `_AXES2TUPLE`, in its own key order. The names are load
/// bearing: "s" is a static frame and "r" a rotating one, and the three letters
/// are the axis sequence.
struct NamedOrder {
    std::string_view name;
    EulerOrder order;
};

constexpr std::array<NamedOrder, 24> kOrders{{
    {"sxyz", {0, 0, 0, 0}}, {"sxyx", {0, 0, 1, 0}}, {"sxzy", {0, 1, 0, 0}}, {"sxzx", {0, 1, 1, 0}},
    {"syzx", {1, 0, 0, 0}}, {"syzy", {1, 0, 1, 0}}, {"syxz", {1, 1, 0, 0}}, {"syxy", {1, 1, 1, 0}},
    {"szxy", {2, 0, 0, 0}}, {"szxz", {2, 0, 1, 0}}, {"szyx", {2, 1, 0, 0}}, {"szyz", {2, 1, 1, 0}},
    {"rzyx", {0, 0, 0, 1}}, {"rxyx", {0, 0, 1, 1}}, {"ryzx", {0, 1, 0, 1}}, {"rxzx", {0, 1, 1, 1}},
    {"rxzy", {1, 0, 0, 1}}, {"ryzy", {1, 0, 1, 1}}, {"rzxy", {1, 1, 0, 1}}, {"ryxy", {1, 1, 1, 1}},
    {"ryxz", {2, 0, 0, 1}}, {"rzxz", {2, 0, 1, 1}}, {"rxyz", {2, 1, 0, 1}}, {"rzyz", {2, 1, 1, 1}},
}};

/// transformations.py `_EPS`: numpy.finfo(float).eps * 4.0.
constexpr double kEps = 2.220446049250313e-16 * 4.0;

void axesOf(const EulerOrder& o, int& i, int& j, int& k) {
    i = o.firstAxis;
    j = kNextAxis[static_cast<size_t>(i + o.parity)];
    k = kNextAxis[static_cast<size_t>(i - o.parity + 1)];
}

}  // namespace

std::optional<EulerOrder> eulerOrderFromString(std::string_view axes) {
    for (const auto& e : kOrders) {
        if (e.name == axes) return e.order;
    }
    return std::nullopt;
}

std::string_view eulerOrderName(const EulerOrder& order) {
    for (const auto& e : kOrders) {
        if (e.order == order) return e.name;
    }
    return {};
}

std::array<std::string_view, 24> eulerOrderNames() {
    std::array<std::string_view, 24> out{};
    for (size_t n = 0; n < kOrders.size(); ++n)
        out[n] = kOrders[n].name;
    return out;
}

Mat4 eulerMatrix(double ai, double aj, double ak, const EulerOrder& order) {
    int i = 0;
    int j = 0;
    int k = 0;
    axesOf(order, i, j, k);

    // A rotating frame swaps the outer two angles; odd parity negates all three.
    if (order.frame != 0) std::swap(ai, ak);
    if (order.parity != 0) {
        ai = -ai;
        aj = -aj;
        ak = -ak;
    }

    const double si = std::sin(ai);
    const double sj = std::sin(aj);
    const double sk = std::sin(ak);
    const double ci = std::cos(ai);
    const double cj = std::cos(aj);
    const double ck = std::cos(ak);
    const double cc = ci * ck;
    const double cs = ci * sk;
    const double sc = si * ck;
    const double ss = si * sk;

    Mat4 m         = Mat4::identity();
    const auto set = [&](int r, int c, double v) {
        m.m[static_cast<size_t>(r)][static_cast<size_t>(c)] = static_cast<float>(v);
    };

    if (order.repetition != 0) {
        set(i, i, cj);
        set(i, j, sj * si);
        set(i, k, sj * ci);
        set(j, i, sj * sk);
        set(j, j, -cj * ss + cc);
        set(j, k, -cj * cs - sc);
        set(k, i, -sj * ck);
        set(k, j, cj * sc + cs);
        set(k, k, cj * cc - ss);
    } else {
        set(i, i, cj * ck);
        set(i, j, sj * sc - cs);
        set(i, k, sj * cc + ss);
        set(j, i, cj * sk);
        set(j, j, sj * ss + cc);
        set(j, k, sj * cs - sc);
        set(k, i, -sj);
        set(k, j, cj * si);
        set(k, k, cj * ci);
    }
    return m;
}

std::array<double, 3> eulerFromMatrix(const Mat4& mat, const EulerOrder& order) {
    int i = 0;
    int j = 0;
    int k = 0;
    axesOf(order, i, j, k);

    const auto M = [&](int r, int c) {
        return static_cast<double>(mat.m[static_cast<size_t>(r)][static_cast<size_t>(c)]);
    };

    double ax = 0.0;
    double ay = 0.0;
    double az = 0.0;

    if (order.repetition != 0) {
        const double sy = std::sqrt(M(i, j) * M(i, j) + M(i, k) * M(i, k));
        if (sy > kEps) {
            ax = std::atan2(M(i, j), M(i, k));
            ay = std::atan2(sy, M(i, i));
            az = std::atan2(M(j, i), -M(k, i));
        } else {
            // Gimbal lock: the decomposition is not unique, and the reference
            // resolves it by pinning the third angle to zero.
            ax = std::atan2(-M(j, k), M(j, j));
            ay = std::atan2(sy, M(i, i));
            az = 0.0;
        }
    } else {
        const double cy = std::sqrt(M(i, i) * M(i, i) + M(j, i) * M(j, i));
        if (cy > kEps) {
            ax = std::atan2(M(k, j), M(k, k));
            ay = std::atan2(-M(k, i), cy);
            az = std::atan2(M(j, i), M(i, i));
        } else {
            ax = std::atan2(-M(j, k), M(j, j));
            ay = std::atan2(-M(k, i), cy);
            az = 0.0;
        }
    }

    if (order.parity != 0) {
        ax = -ax;
        ay = -ay;
        az = -az;
    }
    if (order.frame != 0) std::swap(ax, az);
    return {ax, ay, az};
}

Mat4 quaternionMatrix(const Quat& q) {
    const double n = q.w * q.w + q.x * q.x + q.y * q.y + q.z * q.z;
    Mat4 m         = Mat4::identity();
    if (n < kEps) return m;  // a zero quaternion is no rotation

    const double s  = 2.0 / n;
    const double xx = q.x * q.x * s, yy = q.y * q.y * s, zz = q.z * q.z * s;
    const double xy = q.x * q.y * s, xz = q.x * q.z * s, yz = q.y * q.z * s;
    const double wx = q.w * q.x * s, wy = q.w * q.y * s, wz = q.w * q.z * s;

    m.m[0][0] = static_cast<float>(1.0 - yy - zz);
    m.m[0][1] = static_cast<float>(xy - wz);
    m.m[0][2] = static_cast<float>(xz + wy);
    m.m[1][0] = static_cast<float>(xy + wz);
    m.m[1][1] = static_cast<float>(1.0 - xx - zz);
    m.m[1][2] = static_cast<float>(yz - wx);
    m.m[2][0] = static_cast<float>(xz - wy);
    m.m[2][1] = static_cast<float>(yz + wx);
    m.m[2][2] = static_cast<float>(1.0 - xx - yy);
    return m;
}

Quat quaternionFromMatrix(const Mat4& mat) {
    const auto M = [&](size_t r, size_t c) { return static_cast<double>(mat.m[r][c]); };

    const double trace = M(0, 0) + M(1, 1) + M(2, 2);
    Quat q;
    if (trace > 0.0) {
        const double s = std::sqrt(trace + 1.0) * 2.0;
        q.w            = 0.25 * s;
        q.x            = (M(2, 1) - M(1, 2)) / s;
        q.y            = (M(0, 2) - M(2, 0)) / s;
        q.z            = (M(1, 0) - M(0, 1)) / s;
    } else if (M(0, 0) > M(1, 1) && M(0, 0) > M(2, 2)) {
        const double s = std::sqrt(1.0 + M(0, 0) - M(1, 1) - M(2, 2)) * 2.0;
        q.w            = (M(2, 1) - M(1, 2)) / s;
        q.x            = 0.25 * s;
        q.y            = (M(0, 1) + M(1, 0)) / s;
        q.z            = (M(0, 2) + M(2, 0)) / s;
    } else if (M(1, 1) > M(2, 2)) {
        const double s = std::sqrt(1.0 + M(1, 1) - M(0, 0) - M(2, 2)) * 2.0;
        q.w            = (M(0, 2) - M(2, 0)) / s;
        q.x            = (M(0, 1) + M(1, 0)) / s;
        q.y            = 0.25 * s;
        q.z            = (M(1, 2) + M(2, 1)) / s;
    } else {
        const double s = std::sqrt(1.0 + M(2, 2) - M(0, 0) - M(1, 1)) * 2.0;
        q.w            = (M(1, 0) - M(0, 1)) / s;
        q.x            = (M(0, 2) + M(2, 0)) / s;
        q.y            = (M(1, 2) + M(2, 1)) / s;
        q.z            = 0.25 * s;
    }
    return q;
}

Quat quaternionMultiply(const Quat& a, const Quat& b) {
    return Quat{a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z,
                a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y,
                a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x,
                a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w};
}

Quat quaternionSlerp(const Quat& q0, const Quat& q1, double fraction) {
    if (fraction <= 0.0) return q0;
    if (fraction >= 1.0) return q1;

    double d = q0.w * q1.w + q0.x * q1.x + q0.y * q1.y + q0.z * q1.z;
    if (std::abs(std::abs(d) - 1.0) < kEps) return q0;  // identical rotations

    Quat b = q1;
    // Shortest path: q and -q are the same rotation, so flip if the pair is
    // more than a quarter turn apart. Without this, half the interpolations
    // take the long way round and a limb swings backwards.
    if (d < 0.0) {
        d = -d;
        b = Quat{-q1.w, -q1.x, -q1.y, -q1.z};
    }

    const double angle = std::acos(d);
    if (std::abs(angle) < kEps) return q0;

    const double isin = 1.0 / std::sin(angle);
    const double s0   = std::sin((1.0 - fraction) * angle) * isin;
    const double s1   = std::sin(fraction * angle) * isin;
    return Quat{q0.w * s0 + b.w * s1, q0.x * s0 + b.x * s1, q0.y * s0 + b.y * s1,
                q0.z * s0 + b.z * s1};
}

Mat4 rotationMatrix(double angle, const Vec3& axis) {
    // -Wdouble-promotion is on, so every float->double widening is explicit.
    const double ax  = static_cast<double>(axis.x);
    const double ay  = static_cast<double>(axis.y);
    const double az  = static_cast<double>(axis.z);
    const double len = std::sqrt(ax * ax + ay * ay + az * az);

    Mat4 m = Mat4::identity();
    if (len < kEps) return m;

    const double x = ax / len;
    const double y = ay / len;
    const double z = az / len;
    const double s = std::sin(angle);
    const double c = std::cos(angle);
    const double t = 1.0 - c;

    m.m[0][0] = static_cast<float>(t * x * x + c);
    m.m[0][1] = static_cast<float>(t * x * y - s * z);
    m.m[0][2] = static_cast<float>(t * x * z + s * y);
    m.m[1][0] = static_cast<float>(t * x * y + s * z);
    m.m[1][1] = static_cast<float>(t * y * y + c);
    m.m[1][2] = static_cast<float>(t * y * z - s * x);
    m.m[2][0] = static_cast<float>(t * x * z - s * y);
    m.m[2][1] = static_cast<float>(t * y * z + s * x);
    m.m[2][2] = static_cast<float>(t * z * z + c);
    return m;
}

}  // namespace mh::foundation
