// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Aiming the eyes at a point: the constraint half of owner directive 12.3's
// "eye and teeth rigging are NOT this".
//
// The directive is explicit that eyes are skeleton and constraint work, and
// that expressing them as correctives is "a trap". The skeleton was already
// there and measured: `eye.L` and `eye.R` in both shipped rigs, 141 base
// vertices weighted to each, and the shipped eye proxy fitted ENTIRELY onto
// those vertices -- all 96 base vertices it references carry an eye weight, so
// rotating the bone really does move the eyeball.
//
// What was missing was any way to aim them. The only route was hand-authoring a
// pose file with two rotations in it, and getting the CONVERGENCE right by hand
// is precisely what a constraint is for: the eyes share a target but not a
// position, so each one needs its own rotation.
//
// **A pure swing, no twist.** An eyeball's roll about its own line of sight is
// not observable on a sphere, so introducing one is invisible in a render and
// wrong in every export that reads the bone. That rules out the usual look-at
// built from an up-vector, which produces roll as a matter of course; the
// rotation here is the minimal one taking the bone's rest axis to the target
// direction.
#pragma once

#include "makehuman/foundation/Types.h"
#include "makehuman/rig/Skeleton.h"

#include <cstdint>
#include <expected>
#include <span>
#include <string>

namespace mh::rig {

/// How far an eye may turn from rest.
///
/// Defaults are the human range, roughly: about 35 degrees horizontally and 25
/// vertically. The limit is not cosmetic -- a bone rotation has no idea there is
/// a socket around it, so an unclamped aim at something beside the head rotates
/// the eyeballs out through the skull, and the skinning does it without
/// complaint.
/// Both are refused outside [0, 90]. Ninety is not an arbitrary ceiling: past
/// it the aimed direction leaves the forward hemisphere, where the minimal
/// rotation this uses stops being well defined -- an exactly antiparallel
/// target has no rotation axis, and the eye would quietly stay at rest. An eye
/// that turns further than 90 degrees is a caller's bug either way.
struct EyeAimLimits {
    double horizontalDegrees{35.0};
    double verticalDegrees{25.0};
};

struct EyeAimReport {
    /// How far each eye actually turned from rest, in degrees, after clamping.
    double leftDegrees{};
    double rightDegrees{};
    /// The target was outside the limits, so at least one eye is NOT looking at
    /// it. Worth surfacing: an eye that stops short looks like a bug in
    /// whatever set the target.
    bool clamped{false};
};

enum class EyeAimErrorKind : uint8_t {
    /// The skeleton has no `eye.L`/`eye.R`. A legitimate rig may not, and
    /// silently aiming nothing is how a character stares straight ahead with no
    /// explanation -- so it is refused by name at call time.
    NoEyeBones,
    /// @p localPose is not one matrix per bone.
    PoseSize,
    /// The target coincides with an eye. The direction is then undefined, and
    /// normalising it gives NaN, which propagates into the pose and skins the
    /// head to nothing.
    TargetAtEye,
    /// A limit that is not a usable angle: negative, above 90 degrees, or not a
    /// number.
    BadLimits,
};

struct EyeAimError {
    EyeAimErrorKind kind{};
    std::string detail;

    [[nodiscard]] std::string message() const;
};

/// Writes the rotations for `eye.L` and `eye.R` into @p localPose so both eyes
/// look at @p target.
///
/// @param target in MODEL space, the same space as `Bone::head` -- decimetres,
///        Y-up, the model facing +Z.
/// @param localPose one matrix per bone in the bone's OWN rest frame, the same
///        convention `evaluatePoseSignal` and `poseMesh` take. Only the two eye
///        entries are written; everything else is left exactly as it was, so
///        this composes onto a pose rather than replacing it.
///
/// The bone's axis is +Y in its own rest frame -- `buildRestMatrices` writes the
/// normalised bone direction as column 1, re-measured for the eye bones
/// specifically -- so aiming is the minimal rotation from (0,1,0) to the target
/// direction expressed in that frame.
[[nodiscard]] std::expected<EyeAimReport, EyeAimError> aimEyes(
    const Skeleton& skeleton, foundation::Vec3 target, std::span<foundation::Mat4> localPose,
    EyeAimLimits limits = {});

}  // namespace mh::rig
