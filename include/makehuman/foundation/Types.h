// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <array>
#include <cstdint>
#include <string>

// Plain data and universal vector maths -- no ported algorithm lives here, so
// this header is Apache-2.0 and may sit in foundation.
//
// Audited before the move (LICENSING.md 4): the three reference citations below
// record *facts* checked against the reference -- a unit conversion, a
// coordinate convention, and what a face group holds -- not translated
// expression. Vec3 with +, -, *, dot and cross is not anyone's authorship.
//
// Mesh and Material were audited at the same time and did NOT qualify. See
// memory/todo.md.
namespace mh::foundation {

/// MakeHuman's internal length unit is the **decimetre**.
/// Verified: legacy/python/apps/human.py:694-699 computes
/// `heightCm = 10 * (bboxMaxY - bboxMinY)`.
inline constexpr float kDecimetresToCentimetres = 10.0F;

/// World orientation: Y-up, model faces +Z, right-handed.
/// Verified: legacy/python/shared/skeleton.py:1141-1153 converts to/from
/// "Blender (z is up)", so MakeHuman itself is Y-up.

struct Vec2 {
    float x{}, y{};
    friend bool operator==(const Vec2&, const Vec2&) = default;
};

struct Vec3 {
    float x{}, y{}, z{};
    friend bool operator==(const Vec3&, const Vec3&) = default;

    Vec3& operator+=(const Vec3& o) {
        x += o.x;
        y += o.y;
        z += o.z;
        return *this;
    }

    Vec3& operator-=(const Vec3& o) {
        x -= o.x;
        y -= o.y;
        z -= o.z;
        return *this;
    }

    Vec3& operator*=(float s) {
        x *= s;
        y *= s;
        z *= s;
        return *this;
    }
};

inline Vec3 operator+(Vec3 a, const Vec3& b) {
    return a += b;
}

inline Vec3 operator-(Vec3 a, const Vec3& b) {
    return a -= b;
}

inline Vec3 operator*(Vec3 a, float s) {
    return a *= s;
}

inline Vec3 cross(const Vec3& a, const Vec3& b) {
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}

/// xyz plus a scalar; used for tangents, where w carries handedness.
struct Vec4 {
    float x{}, y{}, z{}, w{};

    friend bool operator==(const Vec4&, const Vec4&) = default;
};

inline float dot(const Vec3& a, const Vec3& b) {
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

/// A named subset of faces. The base mesh has **139** of them -- base.obj has
/// 172 `g` statements but only 139 distinct names, and the reference keys its
/// dict by name (wavefront.py:120-123). They are the unit of
/// picking and of per-group material tinting.
/// Reference: legacy/python/core/module3d.py:47-108
/// A 4x4 transform.
///
/// **Convention, and it is the one that bites.** Storage is ROW-MAJOR
/// (`m[row][col]`) and vectors are COLUMN vectors, so a transform applies as
/// `v' = M * v` and the translation lives in the last COLUMN --
/// `m[0][3], m[1][3], m[2][3]`. This matches the reference's numpy layout,
/// where `mat[:3,3] = head` writes the translation and `mat[:3,0]` is the
/// local X axis (`skeleton.py` getMatrix). Transposing either half of that
/// produces matrices that look plausible and place every bone wrongly.
struct Mat4 {
    std::array<std::array<float, 4>, 4> m{};

    static constexpr Mat4 identity() noexcept {
        Mat4 r;
        for (size_t i = 0; i < 4; ++i)
            r.m[i][i] = 1.0F;
        return r;
    }

    [[nodiscard]] constexpr float at(size_t row, size_t col) const noexcept { return m[row][col]; }

    /// The local axis stored in column @p axis (0 = X, 1 = Y, 2 = Z).
    [[nodiscard]] constexpr Vec3 axis(size_t a) const noexcept {
        return Vec3{m[0][a], m[1][a], m[2][a]};
    }

    [[nodiscard]] constexpr Vec3 translation() const noexcept {
        return Vec3{m[0][3], m[1][3], m[2][3]};
    }
};

inline Mat4 operator*(const Mat4& a, const Mat4& b) noexcept {
    Mat4 r;
    for (size_t i = 0; i < 4; ++i) {
        for (size_t j = 0; j < 4; ++j) {
            float s = 0.0F;
            for (size_t k = 0; k < 4; ++k)
                s += a.m[i][k] * b.m[k][j];
            r.m[i][j] = s;
        }
    }
    return r;
}

/// Inverse of a RIGID transform (orthonormal rotation plus translation).
///
/// Not a general inverse: it assumes the upper-left 3x3 is orthonormal, so the
/// inverse rotation is its transpose. That holds for every matrix this project
/// builds from an orthonormal bone basis, and it is exact where a general
/// inversion would accumulate error. Passing a scaled or sheared matrix here
/// gives a silently wrong answer.
inline Mat4 rigidInverse(const Mat4& t) noexcept {
    Mat4 r = Mat4::identity();
    for (size_t i = 0; i < 3; ++i) {
        for (size_t j = 0; j < 3; ++j)
            r.m[i][j] = t.m[j][i];  // R^T
    }
    const Vec3 p = t.translation();
    for (size_t i = 0; i < 3; ++i)
        r.m[i][3] = -(r.m[i][0] * p.x + r.m[i][1] * p.y + r.m[i][2] * p.z);
    return r;
}

struct FaceGroup {
    std::string name;
    uint16_t idx{};
};

}  // namespace mh::foundation
