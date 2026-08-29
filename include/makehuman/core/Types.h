// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <cstdint>
#include <string>

namespace mh::core {

/// MakeHuman's internal length unit is the **decimetre**.
/// Verified: legacy-python/apps/human.py:694-699 computes
/// `heightCm = 10 * (bboxMaxY - bboxMinY)`.
inline constexpr float kDecimetresToCentimetres = 10.0F;

/// World orientation: Y-up, model faces +Z, right-handed.
/// Verified: legacy-python/shared/skeleton.py:1141-1153 converts to/from
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

/// A named subset of faces. The base mesh has 172 of them; they are the unit of
/// picking and of per-group material tinting.
/// Reference: legacy-python/core/module3d.py:47-108
struct FaceGroup {
    std::string name;
    uint16_t idx{};
};

}  // namespace mh::core
