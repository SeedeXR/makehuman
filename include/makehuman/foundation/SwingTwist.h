// SPDX-License-Identifier: Apache-2.0
//
// Swing-twist decomposition of a rotation, and the two readings of it that a
// pose-space deformer keys on.
//
// NOT ported from anything. `transformations.py` -- which Transform.h is ported
// from, and which is why that one file is BSD-3-Clause -- has no swing-twist,
// and neither does any other part of the Python reference. This is the standard
// derivation, written from the definition: project the rotation's vector part
// onto the twist axis to get the twist, then remove it to get the swing.
//
// Kept out of Transform.h deliberately. That file's header says it is a port of
// transformations.py, and putting a function there that is not one would make
// the licence note wrong about its own contents.
#pragma once

#include "makehuman/foundation/Transform.h"
#include "makehuman/foundation/Types.h"

#include <array>

namespace mh::foundation {

/// A rotation split into how far a bone is bent and how far it is rotated
/// about its own length.
///
/// The two compose back as `quaternionMultiply(swing, twist)` -- twist first,
/// in the joint's own frame, then swing. Both halves are unit quaternions.
struct SwingTwist {
    Quat swing;  ///< Rotation about an axis PERPENDICULAR to the twist axis.
    Quat twist;  ///< Rotation about the twist axis itself.
};

/// Splits @p q about @p twistAxis, which need not be normalised.
///
/// Owner directive 12.3 requires this split rather than Euler angles, because
/// three sequential angles gimbal-lock: near the lock the twist reading jumps
/// while the rotation barely moves, and an RBF keyed on it sees a
/// discontinuity that is not in the pose. This has exactly one degenerate
/// case, and it is a real ambiguity rather than a coordinate artefact: a half
/// turn about an axis perpendicular to @p twistAxis, where every twist angle
/// recomposes to the same rotation. Resolved as no twist, the continuous
/// choice as the half turn is approached.
///
/// A zero-length @p twistAxis names no twist direction, so there is nothing to
/// extract: the whole rotation is returned as swing. That is the same
/// convention `rotationMatrix` uses for a zero axis.
[[nodiscard]] SwingTwist swingTwist(const Quat& q, const Vec3& twistAxis);

/// The signed twist angle in radians, on `(-pi, pi]`.
///
/// Signed by the right-hand rule about @p twistAxis, because a forearm
/// pronated needs a different corrective from one supinated and an unsigned
/// magnitude would put both at the same RBF coordinate. Reversing the axis
/// reverses the sign of the same rotation.
///
/// Takes the shortest path, so 350 degrees reads as -10: an interpolator asked
/// to blend between example poses has to see the small number, or it travels
/// the long way round. `q` and `-q` are the same rotation and read the same.
///
/// @p twist would normally be `swingTwist(...).twist`, but any rotation is
/// accepted and only its component about @p twistAxis is measured.
[[nodiscard]] double twistAngle(const Quat& twist, const Vec3& twistAxis);

/// The rotation vector (axis-angle as one vector, the quaternion log map):
/// direction is the rotation axis, length is the angle in radians on `[0, pi]`.
///
/// This is the swing half of the pose signal, and the reason to use it rather
/// than the quaternion itself is that identity maps to the ORIGIN -- so the
/// rest pose is the natural centre of the RBF, and distance from it is an
/// angle rather than four numbers with a redundant degree of freedom.
///
/// Takes the shortest path exactly as `twistAngle` does, so the two halves of
/// the signal agree about which way round a rotation went.
[[nodiscard]] std::array<double, 3> rotationVector(const Quat& q);

}  // namespace mh::foundation
