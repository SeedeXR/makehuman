// SPDX-License-Identifier: AGPL-3.0-or-later
//
// The pose-signal evaluator: owner directive 12.3's "one pose-signal evaluator,
// several consumers".
//
// This is the link between the rig and the correctives. A posed skeleton comes
// in; a point in signal space comes out, laid out in the order the manifest's
// drivers are listed. `foundation::rbfEvaluate` turns that point into a weight
// vector and `core::CorrectiveBuffer` turns the weights into moved geometry.
//
// The several consumers directive 12.3 names -- geometry correctives, wrinkle
// maps, future muscle and tension masks -- all read the SAME signal. Wrinkle
// maps are "the texture-space sibling of PSD", a different consumer rather than
// a second parallel system with its own keying convention.
//
// **The twist axis is not per-bone data.** `Skeleton::buildRestMatrices` writes
// each bone's axes as COLUMNS with the normalised bone direction in column 1,
// and `poseToBoneLocal` conjugates a pose into that frame. So in a bone's own
// frame the bone points along +Y, always, and the twist axis is the constant
// (0,1,0). Measured across the shipped rig: all 163 bones, worst L1 error
// 1.2e-7. The first case in tests/unit/test_pose_signal.cpp re-measures it,
// because a silent change to that convention would leave every signal finite,
// plausible and about the wrong axis.
//
// Split in two for the same reason as everything else in this pipeline: names
// are resolved ONCE, when a corrective set is bound to a skeleton, and the
// per-frame call cannot fail on a name. A corrective bound to a rig that lacks
// its driving joint must say so at bind time, not mid-animation.
#pragma once

#include "makehuman/core/CorrectiveManifest.h"
#include "makehuman/foundation/Types.h"
#include "makehuman/rig/Skeleton.h"

#include <cstddef>
#include <cstdint>
#include <expected>
#include <span>
#include <string>
#include <vector>

namespace mh::rig {

enum class PoseSignalErrorKind : uint8_t {
    /// The skeleton has no bone of that name.
    UnknownJoint,
    /// An empty driver list describes no signal at all.
    NoDrivers,
};

struct PoseSignalError {
    PoseSignalErrorKind kind{};
    /// Which driver.
    std::string detail;

    [[nodiscard]] std::string message() const;
};

/// Driver names resolved against one skeleton.
///
/// Built once and reused every frame. `bones` and `components` are parallel to
/// the driver list it was planned from.
struct PoseSignalPlan {
    std::vector<uint32_t> bones;
    std::vector<core::DriverComponent> components;
    /// The skeleton this was planned against, so `evaluatePoseSignal` can
    /// insist on one matrix per bone. Checking only the DRIVER bones would
    /// accept a pose array from a different skeleton, whose indices refer to
    /// different bones entirely -- a signal that is finite, plausible and about
    /// the wrong joints.
    size_t boneCount{};
    /// Three numbers per swing, one per twist -- the same rule the manifest
    /// derives its own dimension from, so a plan and a manifest cannot disagree
    /// about the shape of a signal.
    size_t dimension{};
};

/// Resolves @p drivers against @p skeleton.
[[nodiscard]] std::expected<PoseSignalPlan, PoseSignalError> planPoseSignal(
    const Skeleton& skeleton, std::span<const core::Driver> drivers);

/// Writes `plan.dimension` values into @p out, in driver order.
///
/// @p localPose is one matrix per bone in the bone's OWN rest frame -- what
/// `poseToBoneLocal` produces and what `computeSkinningMatrices` consumes. An
/// identity there is the rest pose, and the signal is then exactly zero, which
/// is what puts the rest pose at the origin of the RBF's space.
///
/// The hot path: no allocation, and only `plan.dimension` values are written,
/// so one buffer can serve several plans.
///
/// @return false, having written nothing, if @p localPose is not one matrix per
///         bone or @p out cannot hold the signal.
[[nodiscard]] bool evaluatePoseSignal(const PoseSignalPlan& plan,
                                      std::span<const foundation::Mat4> localPose,
                                      std::span<double> out);

}  // namespace mh::rig
