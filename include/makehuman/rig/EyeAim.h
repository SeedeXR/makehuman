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
#include "makehuman/rig/PoseUnits.h"
#include "makehuman/rig/Skeleton.h"

#include <cstdint>
#include <expected>
#include <span>
#include <string>
#include <vector>

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
    /// Either eye already carried a rotation, and this replaced it.
    ///
    /// The eyes had a driver before this one: FACS **AU61-64** ("Eyes Turn
    /// Left/Right", "Eyes Up", "Eyes Down") rotate the same two bones through
    /// pose units. Aiming WRITES those entries, so the action unit's
    /// contribution to the eyeballs is discarded while every other bone it
    /// touches survives -- measured through the app, `--facs AU61=1.0` moves
    /// 1,721 vertices, `--look-at` moves 1,144, and together they move 1,722,
    /// which is neither. AU61 drives 6 bones and the aim replaces 2, so the
    /// eyelids follow the unit and the eyeballs ignore it.
    ///
    /// Overriding is right -- a look-at is a constraint and constraints win --
    /// but doing it silently is not.
    bool replacedExistingPose{false};
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

/// Where each eye ends up pointing, after clamping, without posing anything.
///
/// Separated from `aimEyes` because a second consumer needs the SAME numbers:
/// the eyelids. A lid that does not follow the gaze leaves a character rotating
/// its eyeballs behind a fixed lid aperture -- MEASURED on the shipped rig,
/// `--look-at` 25.5 degrees down moves the eye geometry 5.53 mm and every body
/// vertex within 0.6 dm of the eye by 0.006 mm, which is the doll stare.
///
/// Angles are in the eye bone's own rest frame, in DEGREES, signed: elevation
/// positive is up, heading positive is the bone's +X side. Both are already
/// clamped to @p limits, so a consumer sees where the eye really points rather
/// than where it was asked to.
struct EyeAimAngles {
    double leftElevationDegrees{};
    double rightElevationDegrees{};
    double leftHeadingDegrees{};
    double rightHeadingDegrees{};
    /// The limits bit, so a caller that only wants angles still learns it.
    bool clamped{false};
};

/// The clamped aim angles for both eyes. Pure: nothing is written.
[[nodiscard]] std::expected<EyeAimAngles, EyeAimError> eyeAimAngles(const Skeleton& skeleton,
                                                                    foundation::Vec3 target,
                                                                    EyeAimLimits limits = {});

/// The eyelid pose units that make the lids track @p angles, with their weights.
///
/// THE LIDS ARE DRIVEN BY AUTHORED UNITS, NOT BY A ROTATION INVENTED HERE.
/// `LeftUpperLidOpen`, `LeftUpperLidClosed` and `LeftLowerLidUp` (and their
/// right-hand twins) are shipped pose units, shaped by whoever authored the
/// face; reconstructing those shapes from a guess about the `orbicularis03/04`
/// bones' local axes would be a second, worse version of data that already
/// exists. This is a CONSTRAINT that consumes a signal and feeds the existing
/// blend -- the shape of directive 12.3, applied to the rig layer it exempts.
///
/// The gains are anatomy, not taste: the upper lid tracks roughly two thirds of
/// the eye's vertical rotation and the lower lid about a fifth, which is why a
/// downward glance narrows the aperture from above while the lower lid barely
/// moves. Returns an empty span's worth of units when the gaze is level, so a
/// horizontal look-at adds nothing to the blend.
[[nodiscard]] std::vector<WeightedUnit> lidFollowUnits(const EyeAimAngles& angles,
                                                       EyeAimLimits limits = {});

/// Writes the rotations for `eye.L` and `eye.R` into @p localPose so both eyes
/// look at @p target.
///
/// @param target in the SKELETON's space, the same space as `Bone::head` --
///        decimetres, Y-up, the model facing +Z.
///
///        THAT IS NOT THE SPACE AN EXPORT IS IN, and the distinction costs
///        real time. The skeleton is built from `base.obj`, which is centred on
///        the origin, so `eye.L` sits at y 7.284; an exported or rendered
///        character is floor-aligned and its eyes are at y 15.53. Aiming at a
///        point read off the exported mesh is therefore about 8.4 dm too high
///        and silently clamps to the upward limit -- measured while writing the
///        lid follow, where y 14.0 and y 18.0 were both chosen as "below" and
///        "above" the eye and BOTH came back at +25 degrees, clamped.
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
