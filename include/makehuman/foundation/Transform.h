// SPDX-License-Identifier: BSD-3-Clause
//
// Ported from legacy/python/core/transformations.py.
//
// That file is Christoph Gohlke's library, vendored into MakeHuman. Its actual
// licence header -- in real comments at the top of the file -- is BSD-3-Clause;
// MakeHuman additionally stamped its AGPL boilerplate into the module
// DOCSTRING, which does not relicense someone else's BSD code. This port
// therefore carries the BSD notice, and is the one file in the tree under a
// third licence. See LICENSING.md.
//
// Copyright (c) 2006-2012, Christoph Gohlke
// Copyright (c) 2006-2012, The Regents of the University of California
// Produced at the Laboratory for Fluorescence Dynamics
// All rights reserved.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are met:
//
// * Redistributions of source code must retain the above copyright
//   notice, this list of conditions and the following disclaimer.
// * Redistributions in binary form must reproduce the above copyright
//   notice, this list of conditions and the following disclaimer in the
//   documentation and/or other materials provided with the distribution.
// * Neither the name of the copyright holders nor the names of any
//   contributors may be used to endorse or promote products derived
//   from this software without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
// AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
// IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
// ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
// LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
// CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
// SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
// INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
// CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
// ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
// POSSIBILITY OF SUCH DAMAGE.
#pragma once

#include "makehuman/foundation/Types.h"

#include <array>
#include <cstdint>
#include <optional>
#include <string_view>

namespace mh::foundation {

/// A rotation quaternion, **scalar first**.
///
/// This is the reference's layout and the one the whole project uses.
/// Eigen's `.coeffs()` is `[x, y, z, w]` -- scalar LAST -- so any future
/// interop with Eigen must reorder. Getting this backwards produces a
/// quaternion that is still unit-length and still rotates, just not the way
/// anyone intended, which is why it survives a "looks fine" check.
struct Quat {
    double w{1.0};
    double x{};
    double y{};
    double z{};
};

/// One of the 24 Euler conventions, in the reference's own encoding
/// (`transformations.py` `_AXES2TUPLE`).
///
/// The 24 differ only by these four numbers, which is exactly why an
/// implementation can be correct for `sxyz` and wrong for the other 23 --
/// nothing about `sxyz` exercises parity, repetition or the rotating frame.
struct EulerOrder {
    uint8_t firstAxis{};   ///< 0 = x, 1 = y, 2 = z
    uint8_t parity{};      ///< 1 = the axis sequence is odd, so angles negate
    uint8_t repetition{};  ///< 1 = first axis repeats (e.g. sxyx), a different formula
    uint8_t frame{};       ///< 1 = rotating ("r") axes, which swaps the outer angles

    [[nodiscard]] constexpr bool operator==(const EulerOrder&) const = default;
};

/// Parses the reference's four-character names ("sxyz", "rzyx", ...).
[[nodiscard]] std::optional<EulerOrder> eulerOrderFromString(std::string_view axes);

/// The canonical name for an order, or empty if it is not one of the 24.
[[nodiscard]] std::string_view eulerOrderName(const EulerOrder& order);

/// All 24, in the reference's own key order.
[[nodiscard]] std::array<std::string_view, 24> eulerOrderNames();

/// Rotation matrix from Euler angles (`transformations.py:1064`).
[[nodiscard]] Mat4 eulerMatrix(double ai, double aj, double ak, const EulerOrder& order);

/// Euler angles from a rotation matrix (`transformations.py:1127`).
///
/// At gimbal lock -- the middle axis at +/-90 degrees -- the decomposition is
/// not unique and the reference resolves it by setting the third angle to zero.
/// Recovered angles therefore need not equal the ones that built the matrix;
/// what must hold is that they rebuild the same matrix.
[[nodiscard]] std::array<double, 3> eulerFromMatrix(const Mat4& m, const EulerOrder& order);

/// Rotation matrix from a quaternion (`transformations.py:1288`).
[[nodiscard]] Mat4 quaternionMatrix(const Quat& q);

/// Quaternion from a rotation matrix.
///
/// Uses the trace method rather than the reference's default eigenvector path.
/// Verified equivalent across all 120 captured cases: the magnitudes agree to
/// 4.4e-16, but the SIGN differs on 18 of them. `q` and `-q` are the same
/// rotation, so this is not an error -- it is why the parity test compares up
/// to sign and additionally checks that the quaternion rebuilds the same
/// matrix, which pins the rotation exactly.
[[nodiscard]] Quat quaternionFromMatrix(const Mat4& m);

/// Hamilton product (`transformations.py` quaternion_multiply).
[[nodiscard]] Quat quaternionMultiply(const Quat& a, const Quat& b);

/// Spherical linear interpolation, taking the shortest path.
[[nodiscard]] Quat quaternionSlerp(const Quat& q0, const Quat& q1, double fraction);

/// Rotation of @p angle radians about an arbitrary @p axis, which need not be
/// normalised (`transformations.py` rotation_matrix).
[[nodiscard]] Mat4 rotationMatrix(double angle, const Vec3& axis);

}  // namespace mh::foundation
