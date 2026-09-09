// SPDX-License-Identifier: Apache-2.0
//
// Swing-twist decomposition: the pose signal every corrective will key on.
//
// A joint's local rotation is one quaternion, and a corrective wants it split:
// how far the bone has been bent away from rest (SWING) and how far it has
// been rotated about its own length (TWIST). Those two drive completely
// different deformation -- a shoulder lifted is not a forearm pronated -- and
// an RBF keyed on the mixed quaternion cannot tell them apart.
//
// Euler angles would also split it, and owner directive 12.3 rules them out
// for the reason they always fail here: three sequential angles gimbal-lock,
// and near the lock the "twist" reading jumps by 180 degrees while the actual
// rotation moves imperceptibly. The swing-twist split has one singularity, at
// a half turn away from the twist axis, and it is a genuine ambiguity there
// rather than a coordinate artefact.
//
// THE REFERENCE HAS NONE OF THIS -- `grep -ri 'swing\|twist' legacy/python`
// returns nothing, and `transformations.py` (which the rest of Transform.h is
// ported from) has no such function. So there is no parity fixture to compare
// against and these tests are the analytic oracle instead: cases whose answer
// is known exactly from the definition, not measured from an implementation.
#include "makehuman/foundation/SwingTwist.h"

#include "makehuman/foundation/Transform.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <cmath>
#include <numbers>

using namespace mh::foundation;
using Catch::Matchers::WithinAbs;

namespace {

constexpr double kPi  = std::numbers::pi;
constexpr double kTol = 1e-12;
const double kHalfRt2 = std::sqrt(2.0) / 2.0;

/// A rotation of @p angle radians about a unit @p axis, built from the
/// definition rather than from any function under test.
Quat rotation(double angle, const Vec3& axis) {
    const double s = std::sin(angle / 2.0);
    return Quat{std::cos(angle / 2.0), static_cast<double>(axis.x) * s,
                static_cast<double>(axis.y) * s, static_cast<double>(axis.z) * s};
}

/// Same rotation, allowing for the q/-q double cover.
void checkSameRotation(const Quat& got, const Quat& want, double tol = kTol) {
    const double d = got.w * want.w + got.x * want.x + got.y * want.y + got.z * want.z;
    const Quat w   = d < 0.0 ? Quat{-want.w, -want.x, -want.y, -want.z} : want;
    CHECK_THAT(got.w, WithinAbs(w.w, tol));
    CHECK_THAT(got.x, WithinAbs(w.x, tol));
    CHECK_THAT(got.y, WithinAbs(w.y, tol));
    CHECK_THAT(got.z, WithinAbs(w.z, tol));
}

constexpr Vec3 kX{1.0F, 0.0F, 0.0F};
constexpr Vec3 kY{0.0F, 1.0F, 0.0F};
constexpr Vec3 kZ{0.0F, 0.0F, 1.0F};

}  // namespace

TEST_CASE("identity decomposes into two identities", "[foundation][swingtwist]") {
    const auto st = swingTwist(Quat{}, kY);
    checkSameRotation(st.swing, Quat{});
    checkSameRotation(st.twist, Quat{});
    CHECK_THAT(twistAngle(st.twist, kY), WithinAbs(0.0, kTol));
}

TEST_CASE("a rotation about the twist axis is all twist", "[foundation][swingtwist]") {
    // The defining case: nothing is bent, so swing must be exactly identity.
    // An implementation that leaked any of the rotation into swing would still
    // recompose correctly, so recomposition alone cannot catch it.
    for (const double angle : {0.3, 1.0, kPi / 2.0, 2.0, -0.7, -kPi / 2.0}) {
        const Quat q  = rotation(angle, kY);
        const auto st = swingTwist(q, kY);
        checkSameRotation(st.twist, q);
        checkSameRotation(st.swing, Quat{});
        CHECK_THAT(twistAngle(st.twist, kY), WithinAbs(angle, kTol));
    }
}

TEST_CASE("a rotation perpendicular to the twist axis is all swing", "[foundation][swingtwist]") {
    for (const double angle : {0.3, 1.0, kPi / 2.0, 2.0, -0.7}) {
        for (const Vec3& axis : {kX, kZ}) {
            const Quat q  = rotation(angle, axis);
            const auto st = swingTwist(q, kY);
            checkSameRotation(st.swing, q);
            checkSameRotation(st.twist, Quat{});
            CHECK_THAT(twistAngle(st.twist, kY), WithinAbs(0.0, kTol));
        }
    }
}

TEST_CASE("the composition order is swing after twist", "[foundation][swingtwist]") {
    // Hand-computed, so this pins the order rather than describing whatever the
    // implementation happens to do. Twist axis Y, twist a quarter turn about Y,
    // swing a quarter turn about Z:
    //
    //   Rz(90) = (r, 0, 0, r),  Ry(90) = (r, 0, r, 0),  r = sqrt(2)/2
    //   Rz(90) * Ry(90) = (0.5, -0.5, 0.5, 0.5)
    //
    // Decomposing THAT must give back the two factors. Had the implementation
    // meant q = twist * swing, this input would decompose into something else
    // and the halves below would belong to `Ry(90) * Rz(90)` instead.
    const Quat q{0.5, -0.5, 0.5, 0.5};
    const auto st = swingTwist(q, kY);
    checkSameRotation(st.twist, Quat{kHalfRt2, 0.0, kHalfRt2, 0.0});
    checkSameRotation(st.swing, Quat{kHalfRt2, 0.0, 0.0, kHalfRt2});
    checkSameRotation(quaternionMultiply(st.swing, st.twist), q);
    CHECK_THAT(twistAngle(st.twist, kY), WithinAbs(kPi / 2.0, kTol));
}

TEST_CASE("swing and twist recompose into the original rotation", "[foundation][swingtwist]") {
    // The invariant, over rotations that mix both components. A deterministic
    // sweep rather than random input: a decomposition that fails only for one
    // seed is not something anyone can reproduce.
    for (int i = 0; i < 12; ++i) {
        const double a = -kPi + (2.0 * kPi * static_cast<double>(i)) / 12.0;
        for (int j = 1; j < 12; ++j) {
            const double b = -kPi + (2.0 * kPi * static_cast<double>(j)) / 12.0;
            const Quat q   = quaternionMultiply(rotation(b, kZ), rotation(a, kY));
            const auto st  = swingTwist(q, kY);
            checkSameRotation(quaternionMultiply(st.swing, st.twist), q, 1e-11);
        }
    }
}

TEST_CASE("the split really is a split: swing has no component on the axis",
          "[foundation][swingtwist]") {
    // What makes it a swing-TWIST split rather than any other factorisation:
    // the swing's rotation axis is perpendicular to the twist axis, and the
    // twist's is parallel to it. Recomposition holds for infinitely many
    // factorisations; only this one is the decomposition asked for.
    for (int i = 1; i < 10; ++i) {
        const double a = (kPi * static_cast<double>(i)) / 10.0;
        for (int j = 1; j < 10; ++j) {
            const double b = (kPi * static_cast<double>(j)) / 10.0;
            const Quat q   = quaternionMultiply(rotation(b, kX), rotation(a, kY));
            const auto st  = swingTwist(q, kY);

            // swing's vector part dotted with Y.
            CHECK_THAT(st.swing.y, WithinAbs(0.0, 1e-11));
            // twist's vector part is along Y and nowhere else.
            CHECK_THAT(st.twist.x, WithinAbs(0.0, 1e-11));
            CHECK_THAT(st.twist.z, WithinAbs(0.0, 1e-11));
        }
    }
}

TEST_CASE("a half turn away from the twist axis is the one real singularity",
          "[foundation][swingtwist]") {
    // At a half turn about an axis perpendicular to the twist axis, q's scalar
    // part and its projection onto the axis are BOTH zero, so the twist is
    // genuinely undefined -- every twist angle recomposes to the same rotation.
    // Resolved as no twist at all, which is the continuous choice, and the
    // thing that must not happen is a NaN.
    //
    // Written as the EXACT quaternion, not as rotation(kPi, axis). That matters
    // and it was measured: `std::cos(kPi / 2.0)` is 6.1e-17, not zero, so the
    // built version never reaches a zero norm and normalising 6.1e-17 by itself
    // gives exactly identity -- the same answer the guard returns. Removing the
    // guard passed the whole file when this case was written that way.
    SECTION("exactly a half turn, where the norm really is zero") {
        for (const Quat& q : {Quat{0.0, 1.0, 0.0, 0.0}, Quat{0.0, 0.0, 0.0, 1.0}}) {
            const auto st = swingTwist(q, kY);
            CHECK(std::isfinite(st.twist.w));
            CHECK(std::isfinite(st.twist.x));
            CHECK(std::isfinite(st.twist.y));
            CHECK(std::isfinite(st.twist.z));
            CHECK(std::isfinite(st.swing.w));
            checkSameRotation(st.twist, Quat{});
            checkSameRotation(st.swing, q);
            checkSameRotation(quaternionMultiply(st.swing, st.twist), q);
        }
    }

    SECTION("a hair off it, where the twist is meaningless rather than absent") {
        // Both parts below the tolerance but not zero, and NOT in proportion:
        // dividing through would report a twist of well over a hundred degrees
        // from a rotation that is a half turn to within one part in 1e16. The
        // guard reports no twist instead, which is what the neighbourhood
        // converges to.
        const Quat q{1e-17, 1.0, 3e-17, 0.0};
        const auto st = swingTwist(q, kY);
        checkSameRotation(st.twist, Quat{});
        CHECK_THAT(twistAngle(st.twist, kY), WithinAbs(0.0, kTol));
    }

    SECTION("approached from either side, the twist stays near zero") {
        // Continuity, which is what makes "no twist" the right resolution
        // rather than an arbitrary one.
        for (const double d : {1e-4, 1e-6, -1e-4, -1e-6}) {
            const Quat q = rotation(kPi + d, kX);
            CHECK_THAT(twistAngle(swingTwist(q, kY).twist, kY), WithinAbs(0.0, 1e-3));
        }
    }
}

TEST_CASE("the twist axis need not be a unit vector", "[foundation][swingtwist]") {
    const Quat q     = quaternionMultiply(rotation(0.8, kZ), rotation(0.4, kY));
    const auto unit  = swingTwist(q, kY);
    const auto long_ = swingTwist(q, Vec3{0.0F, 7.5F, 0.0F});
    checkSameRotation(long_.swing, unit.swing);
    checkSameRotation(long_.twist, unit.twist);

    // A zero axis names no twist direction, so there is no twist to extract.
    // Same convention as `rotationMatrix`, which returns identity for it
    // (src/foundation/Transform.cpp) rather than dividing by zero.
    const auto none = swingTwist(q, Vec3{});
    checkSameRotation(none.twist, Quat{});
    checkSameRotation(none.swing, q);
    // twistAngle answers the same question and needs the same answer, or a
    // caller reading the scalar gets a number for an axis that names nothing.
    CHECK_THAT(twistAngle(q, Vec3{}), WithinAbs(0.0, kTol));
    CHECK_THAT(twistAngle(q, Vec3{0.0F, 7.5F, 0.0F}), WithinAbs(twistAngle(q, kY), kTol));
}

TEST_CASE("twist angle is signed by the right-hand rule about the axis",
          "[foundation][swingtwist]") {
    // The sign is the whole point: a corrective for a forearm pronated is not
    // the one for a forearm supinated, and an unsigned magnitude would drive
    // both from the same RBF coordinate.
    for (const double angle : {0.25, 1.1, 3.0, -0.25, -1.1, -3.0}) {
        CHECK_THAT(twistAngle(rotation(angle, kY), kY), WithinAbs(angle, kTol));
        // Reversing the axis reverses the reading of the same rotation.
        CHECK_THAT(twistAngle(rotation(angle, kY), Vec3{0.0F, -1.0F, 0.0F}),
                   WithinAbs(-angle, kTol));
    }

    // Beyond a half turn the shortest-path reading is the one an interpolator
    // wants: 350 degrees of twist is 10 degrees the other way, not 350.
    const double wide = (350.0 / 180.0) * kPi;
    CHECK_THAT(twistAngle(rotation(wide, kY), kY), WithinAbs(wide - 2.0 * kPi, kTol));

    // q and -q are the same rotation and must read the same.
    const Quat q = rotation(1.3, kY);
    CHECK_THAT(twistAngle(Quat{-q.w, -q.x, -q.y, -q.z}, kY), WithinAbs(1.3, kTol));
}

TEST_CASE("the rotation vector is angle times axis", "[foundation][swingtwist]") {
    // The swing half of the signal, as something an RBF can measure distance
    // in. Identity maps to the origin, which is what makes the rest pose the
    // natural centre.
    const auto zero = rotationVector(Quat{});
    CHECK_THAT(zero[0], WithinAbs(0.0, kTol));
    CHECK_THAT(zero[1], WithinAbs(0.0, kTol));
    CHECK_THAT(zero[2], WithinAbs(0.0, kTol));

    for (const double angle : {0.1, 1.0, 2.5, 3.1}) {
        for (const Vec3& axis : {kX, kY, kZ}) {
            const auto v = rotationVector(rotation(angle, axis));
            CHECK_THAT(v[0], WithinAbs(angle * static_cast<double>(axis.x), 1e-11));
            CHECK_THAT(v[1], WithinAbs(angle * static_cast<double>(axis.y), 1e-11));
            CHECK_THAT(v[2], WithinAbs(angle * static_cast<double>(axis.z), 1e-11));
        }
    }

    // An oblique axis, so a per-component implementation that only works on
    // the basis vectors does not survive.
    const Vec3 oblique{static_cast<float>(1.0 / std::sqrt(3.0)),
                       static_cast<float>(1.0 / std::sqrt(3.0)),
                       static_cast<float>(1.0 / std::sqrt(3.0))};
    const auto v = rotationVector(rotation(1.2, oblique));
    CHECK_THAT(v[0], WithinAbs(1.2 / std::sqrt(3.0), 1e-7));
    CHECK_THAT(v[1], WithinAbs(1.2 / std::sqrt(3.0), 1e-7));
    CHECK_THAT(v[2], WithinAbs(1.2 / std::sqrt(3.0), 1e-7));

    // q and -q again: the shortest-path reading, so the angle stays in [0, pi]
    // and never flips sign across the double cover.
    const Quat r   = rotation(1.4, kZ);
    const auto neg = rotationVector(Quat{-r.w, -r.x, -r.y, -r.z});
    CHECK_THAT(neg[2], WithinAbs(1.4, 1e-11));

    // Past a half turn it comes back the short way, matching twistAngle.
    const auto wide = rotationVector(rotation((350.0 / 180.0) * kPi, kZ));
    CHECK_THAT(wide[2], WithinAbs(-((10.0 / 180.0) * kPi), 1e-11));
}

TEST_CASE("the swing rotation vector lies in the plane across the twist axis",
          "[foundation][swingtwist]") {
    // Together with the twist angle this is the signal: one scalar along the
    // axis and a vector across it, which is 3 numbers for 3 degrees of
    // freedom and no redundant fourth to make two poses look different when
    // they are the same.
    for (int i = 1; i < 8; ++i) {
        const double a = (kPi * static_cast<double>(i)) / 8.0;
        const Quat q   = quaternionMultiply(rotation(a, kX), rotation(0.9, kY));
        const auto st  = swingTwist(q, kY);
        const auto v   = rotationVector(st.swing);
        CHECK_THAT(v[1], WithinAbs(0.0, 1e-11));
    }
}
