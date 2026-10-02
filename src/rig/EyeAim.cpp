// SPDX-License-Identifier: AGPL-3.0-or-later
//
// See the header for why this is a pure swing and why the limits exist.
#include "makehuman/rig/EyeAim.h"

#include "makehuman/foundation/Transform.h"

#include <algorithm>
#include <cmath>
#include <numbers>
#include <optional>

namespace mh::rig {

namespace {

std::optional<size_t> indexOf(const Skeleton& skeleton, std::string_view name) {
    for (size_t i = 0; i < skeleton.bones.size(); ++i)
        if (skeleton.bones[i].name == name) return i;
    return std::nullopt;
}

/// The rotation part of @p m, transposed, applied to @p v.
///
/// `matRestGlobal`'s rotation columns are the bone's orthonormal axes --
/// verified on the shipped rig: eye.L's X.Y is 6e-5 and X cross Y equals its Z
/// column -- so the transpose IS the inverse and there is no general inverse to
/// compute.
foundation::Vec3 rotateByTranspose(const foundation::Mat4& m, foundation::Vec3 v) {
    return foundation::Vec3{m.m[0][0] * v.x + m.m[1][0] * v.y + m.m[2][0] * v.z,
                            m.m[0][1] * v.x + m.m[1][1] * v.y + m.m[2][1] * v.z,
                            m.m[0][2] * v.x + m.m[1][2] * v.y + m.m[2][2] * v.z};
}

/// The minimal rotation taking the bone's rest axis (0,1,0) to @p to.
///
/// Minimal is what makes it a pure swing: the axis is perpendicular to both
/// vectors, so there is no component along the bone and therefore no twist.
///
/// @p to must be a UNIT vector with a non-negative y -- `to.y` is read as the
/// cosine of the angle, and an antiparallel @p to has no rotation axis at all.
/// The caller guarantees both by clamping the limits to 90 degrees and building
/// @p to from the clamped angles.
foundation::Mat4 swingTo(foundation::Vec3 to) {
    const double dot = std::clamp(static_cast<double>(to.y), -1.0, 1.0);
    // The cross product of (0,1,0) with `to`, which is perpendicular to both.
    const foundation::Vec3 axis{to.z, 0.0F, -to.x};
    const double ax      = static_cast<double>(axis.x);
    const double az      = static_cast<double>(axis.z);
    const double axisLen = std::sqrt(ax * ax + az * az);
    // Already aligned. Not a tolerance judgement: a zero-length axis cannot be
    // normalised, and the rotation it would describe is the identity anyway.
    if (axisLen == 0.0) return foundation::Mat4::identity();
    return foundation::rotationMatrix(
        std::acos(dot),
        foundation::Vec3{static_cast<float>(ax / axisLen), 0.0F, static_cast<float>(az / axisLen)});
}

}  // namespace

std::string EyeAimError::message() const {
    std::string s;
    switch (kind) {
        case EyeAimErrorKind::NoEyeBones: s = "the skeleton has no eye bones"; break;
        case EyeAimErrorKind::PoseSize: s = "the pose is not one matrix per bone"; break;
        case EyeAimErrorKind::TargetAtEye: s = "the target sits on an eye"; break;
        case EyeAimErrorKind::BadLimits: s = "an eye limit is not a usable angle"; break;
    }
    if (!detail.empty()) s += ": " + detail;
    return s;
}

namespace {

/// A limit that is a usable angle. Shared, because `aimEyes` and
/// `eyeAimAngles` must refuse exactly the same inputs.
constexpr bool usableLimit(double d) {
    return d >= 0.0 && d <= 90.0;
}

/// One eye's clamped angles, in RADIANS, in that bone's own rest frame.
///
/// Factored out so `aimEyes` and `eyeAimAngles` cannot drift: the lids are
/// driven from these numbers and an eye that clamps while the lid does not
/// would be worse than no lid follow at all.
struct OneEye {
    double elevation{};
    double heading{};
    bool clamped{false};
};

std::expected<OneEye, EyeAimError> oneEyeAngles(const Bone& bone, foundation::Vec3 target,
                                                double hLimit, double vLimit) {
    const double tx       = static_cast<double>(target.x) - static_cast<double>(bone.head.x);
    const double ty       = static_cast<double>(target.y) - static_cast<double>(bone.head.y);
    const double tz       = static_cast<double>(target.z) - static_cast<double>(bone.head.z);
    const double distance = std::sqrt(tx * tx + ty * ty + tz * tz);
    if (distance == 0.0) {
        return std::unexpected(EyeAimError{EyeAimErrorKind::TargetAtEye, bone.name});
    }
    const foundation::Vec3 world{static_cast<float>(tx / distance),
                                 static_cast<float>(ty / distance),
                                 static_cast<float>(tz / distance)};
    const foundation::Vec3 local = rotateByTranspose(bone.matRestGlobal, world);

    const double elevation = std::asin(std::clamp(static_cast<double>(local.z), -1.0, 1.0));
    const double heading   = std::atan2(static_cast<double>(local.x), static_cast<double>(local.y));
    OneEye out;
    out.elevation = std::clamp(elevation, -vLimit, vLimit);
    out.heading   = std::clamp(heading, -hLimit, hLimit);
    out.clamped   = out.elevation != elevation || out.heading != heading;
    return out;
}

}  // namespace

std::expected<EyeAimReport, EyeAimError> aimEyes(const Skeleton& skeleton, foundation::Vec3 target,
                                                 std::span<foundation::Mat4> localPose,
                                                 EyeAimLimits limits) {
    // `!(x >= 0 && x <= 90)` rather than the negation spelled out, so a NaN
    // limit is refused too: every comparison against NaN is false, and a NaN
    // limit clamps everything to NaN.
    //
    // The upper bound is not cosmetic. Above 90 degrees the aimed direction
    // leaves the forward hemisphere, and `swingTo`'s minimal-rotation
    // construction stops being well defined -- at exactly antiparallel the
    // rotation axis is the zero vector, and it would hand back the identity: an
    // eye told to look directly backwards and quietly looking straight ahead.
    // With the human defaults the aimed y never drops below
    // cos(35) * cos(25) = 0.742, so the case is unreachable through them and
    // perfectly reachable through the parameter.
    if (!usableLimit(limits.horizontalDegrees) || !usableLimit(limits.verticalDegrees)) {
        return std::unexpected(EyeAimError{EyeAimErrorKind::BadLimits, {}});
    }
    if (localPose.size() != skeleton.bones.size()) {
        return std::unexpected(EyeAimError{EyeAimErrorKind::PoseSize, {}});
    }
    const auto left  = indexOf(skeleton, "eye.L");
    const auto right = indexOf(skeleton, "eye.R");
    if (!left || !right) {
        return std::unexpected(EyeAimError{EyeAimErrorKind::NoEyeBones, {}});
    }

    EyeAimReport report;
    const double hLimit = limits.horizontalDegrees * std::numbers::pi / 180.0;
    const double vLimit = limits.verticalDegrees * std::numbers::pi / 180.0;

    // Checked BEFORE anything is written, and for BOTH eyes: the aim replaces
    // whatever was there, and a caller that combined it with a FACS gaze unit
    // deserves to hear so. See `EyeAimReport::replacedExistingPose`.
    const auto rotated = [](const foundation::Mat4& m) {
        // A TOLERANCE, not an exact compare, and the difference is a real
        // warning rather than a style point. A blend writes every bone, so a
        // bone no unit touches comes back identity to float precision and not
        // bit-exactly -- quaternion multiply and renormalise leave a few ulp.
        // MEASURED: adding the eyelid follow, which rotates two orbicularis
        // bones and nothing else, made a plain `--look-at` announce that it had
        // "replaced an eye rotation already in the pose". It had not; the eye
        // entries were identity to within 1e-7.
        //
        // 1e-5 sits two orders above that noise and well below anything a
        // caller could mean: a rotation of a hundredth of a degree already puts
        // 1.7e-4 in an off-diagonal term.
        constexpr float kIdentityTolerance = 1e-5F;
        const foundation::Mat4 id          = foundation::Mat4::identity();
        for (size_t r = 0; r < 3; ++r) {
            for (size_t c = 0; c < 3; ++c) {
                if (std::abs(m.m[r][c] - id.m[r][c]) > kIdentityTolerance) return true;
            }
        }
        return false;
    };
    report.replacedExistingPose = rotated(localPose[*left]) || rotated(localPose[*right]);

    for (const bool isLeft : {true, false}) {
        const size_t at  = isLeft ? *left : *right;
        const Bone& bone = skeleton.bones[at];
        // Split into the two angles a limit is expressed in, then clamp each and
        // rebuild. Clamping the ANGLES rather than the resulting rotation is
        // what makes a horizontal and a vertical limit independent -- clamping
        // one total angle would let a target 30 degrees up and 30 across through
        // while refusing one 30 degrees up alone.
        //
        // In the eye bone's frame, +Y is forward and +Z is up -- measured on the
        // shipped rig: eye.L's Z column is (-0.0051, 0.9996, -0.0288), which is
        // world up. So elevation is asin(local.z) and the horizontal turn is
        // atan2 in the remaining plane. `oneEyeAngles` holds that, and the lid
        // follow reads the same function so the two cannot disagree.
        const auto angles = oneEyeAngles(bone, target, hLimit, vLimit);
        if (!angles) return std::unexpected(angles.error());
        const double clampedElevation = angles->elevation;
        const double clampedHeading   = angles->heading;
        if (angles->clamped) report.clamped = true;

        const foundation::Vec3 aimed{
            static_cast<float>(std::sin(clampedHeading) * std::cos(clampedElevation)),
            static_cast<float>(std::cos(clampedHeading) * std::cos(clampedElevation)),
            static_cast<float>(std::sin(clampedElevation))};

        localPose[at] = swingTo(aimed);

        const double turn = std::acos(std::clamp(static_cast<double>(aimed.y), -1.0, 1.0)) * 180.0 /
                            std::numbers::pi;
        if (isLeft) {
            report.leftDegrees = turn;
        } else {
            report.rightDegrees = turn;
        }
    }
    return report;
}

std::expected<EyeAimAngles, EyeAimError> eyeAimAngles(const Skeleton& skeleton,
                                                      foundation::Vec3 target,
                                                      EyeAimLimits limits) {
    if (!usableLimit(limits.horizontalDegrees) || !usableLimit(limits.verticalDegrees)) {
        return std::unexpected(EyeAimError{EyeAimErrorKind::BadLimits, {}});
    }
    const auto left  = indexOf(skeleton, "eye.L");
    const auto right = indexOf(skeleton, "eye.R");
    if (!left || !right) {
        return std::unexpected(EyeAimError{EyeAimErrorKind::NoEyeBones, {}});
    }
    const double hLimit = limits.horizontalDegrees * std::numbers::pi / 180.0;
    const double vLimit = limits.verticalDegrees * std::numbers::pi / 180.0;
    const auto l        = oneEyeAngles(skeleton.bones[*left], target, hLimit, vLimit);
    if (!l) return std::unexpected(l.error());
    const auto r = oneEyeAngles(skeleton.bones[*right], target, hLimit, vLimit);
    if (!r) return std::unexpected(r.error());

    constexpr double kDeg = 180.0 / std::numbers::pi;
    return EyeAimAngles{.leftElevationDegrees  = l->elevation * kDeg,
                        .rightElevationDegrees = r->elevation * kDeg,
                        .leftHeadingDegrees    = l->heading * kDeg,
                        .rightHeadingDegrees   = r->heading * kDeg,
                        .clamped               = l->clamped || r->clamped};
}

std::vector<WeightedUnit> lidFollowUnits(const EyeAimAngles& angles, EyeAimLimits limits) {
    // Anatomy, not taste. The upper lid tracks about two thirds of the eye's
    // vertical rotation -- it is dragged by the globe it rests on -- and the
    // lower lid about a fifth, which is why a downward glance closes the
    // aperture from above while the lower lid barely moves.
    constexpr double kUpperGain = 0.67;
    constexpr double kLowerGain = 0.20;

    std::vector<WeightedUnit> units;
    if (!(limits.verticalDegrees > 0.0)) return units;

    const auto add = [&](const char* name, double weight) {
        // Below a thousandth the blend cannot show it and the unit is noise in
        // every exported .mhpose that records the pose.
        if (weight > 1e-3) {
            units.push_back(WeightedUnit{name, static_cast<float>(std::min(weight, 1.0))});
        }
    };

    for (const bool isLeft : {true, false}) {
        const double elev = isLeft ? angles.leftElevationDegrees : angles.rightElevationDegrees;
        // Normalised against the LIMIT, so a rig with a wider vertical range
        // does not drive the lids past the shapes that were authored.
        const double t = std::clamp(std::abs(elev) / limits.verticalDegrees, 0.0, 1.0);
        if (elev < 0.0) {
            // Looking down: the upper lid comes with it and the lower rises.
            add(isLeft ? "LeftUpperLidClosed" : "RightUpperLidClosed", t * kUpperGain);
            add(isLeft ? "LeftLowerLidUp" : "RightLowerLidUp", t * kLowerGain);
        } else if (elev > 0.0) {
            // Looking up: the lid retracts and the lower one stays where it is.
            add(isLeft ? "LeftUpperLidOpen" : "RightUpperLidOpen", t * kUpperGain);
        }
    }
    return units;
}

}  // namespace mh::rig
