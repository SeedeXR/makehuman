// SPDX-License-Identifier: AGPL-3.0-or-later
//
// See the header for why the twist axis is a constant and why this is split
// into a plan and an evaluation.
#include "makehuman/rig/PoseSignal.h"

#include "makehuman/foundation/SwingTwist.h"
#include "makehuman/foundation/Transform.h"

namespace mh::rig {

namespace {

/// The bone's own axis in its own frame.
///
/// Not a lookup: `buildRestMatrices` puts the normalised bone direction in
/// column 1 of every bone's rest matrix, and `poseToBoneLocal` conjugates into
/// that frame. See the header, and the first case of the test file, which
/// re-measures it against all 163 bones of the shipped rig.
constexpr foundation::Vec3 kBoneAxis{0.0F, 1.0F, 0.0F};

}  // namespace

std::string PoseSignalError::message() const {
    std::string s;
    switch (kind) {
        case PoseSignalErrorKind::UnknownJoint: s = "no such joint in this skeleton"; break;
        case PoseSignalErrorKind::NoDrivers: s = "no drivers, so no signal"; break;
    }
    if (!detail.empty()) s += ": " + detail;
    return s;
}

std::expected<PoseSignalPlan, PoseSignalError> planPoseSignal(
    const Skeleton& skeleton, std::span<const core::Driver> drivers) {
    if (drivers.empty()) {
        return std::unexpected(PoseSignalError{PoseSignalErrorKind::NoDrivers, {}});
    }

    PoseSignalPlan plan;
    plan.boneCount = skeleton.boneCount();
    plan.bones.reserve(drivers.size());
    plan.components.reserve(drivers.size());
    for (const core::Driver& d : drivers) {
        bool found = false;
        for (size_t i = 0; i < skeleton.bones.size(); ++i) {
            if (skeleton.bones[i].name != d.joint) continue;
            plan.bones.push_back(static_cast<uint32_t>(i));
            found = true;
            break;
        }
        if (!found) {
            return std::unexpected(PoseSignalError{PoseSignalErrorKind::UnknownJoint, d.joint});
        }
        plan.components.push_back(d.component);
        plan.dimension += core::componentDimension(d.component);
    }
    return plan;
}

bool evaluatePoseSignal(const PoseSignalPlan& plan, std::span<const foundation::Mat4> localPose,
                        std::span<double> out) {
    if (out.size() < plan.dimension) return false;
    if (localPose.size() != plan.boneCount) return false;

    size_t at = 0;
    for (size_t i = 0; i < plan.bones.size(); ++i) {
        const auto q  = foundation::quaternionFromMatrix(localPose[plan.bones[i]]);
        const auto st = foundation::swingTwist(q, kBoneAxis);
        if (plan.components[i] == core::DriverComponent::Swing) {
            // The rotation vector rather than the quaternion, so that identity
            // maps to the ORIGIN and the rest pose is the natural centre of the
            // RBF's space. Its Y component is always zero -- a swing's axis is
            // perpendicular to the twist axis -- and it is written anyway
            // rather than dropped: the manifest counts a swing as three
            // numbers, and a signal whose length depended on a runtime value
            // would be a different shape from the one the poses declare.
            const auto v = foundation::rotationVector(st.swing);
            out[at++]    = v[0];
            out[at++]    = v[1];
            out[at++]    = v[2];
        } else {
            // `st.twist`, not `q`, and the difference is smaller than it looks:
            // `twistAngle` measures only the component about the axis, so for
            // any ordinary rotation the two are IDENTICAL -- measured, worst
            // difference 2.2e-16 over a sweep of swing/twist combinations.
            //
            // They part company only where `swingTwist`'s singularity guard
            // fires: with the scalar part and the projection BOTH below about
            // 1e-15, the whole rotation gives `2*atan2` of two pieces of noise
            // -- measured 2.498 rad of twist for a rotation that has none --
            // while the twist half correctly reports zero. A float `Mat4`
            // cannot produce numbers that small, so a mutation swapping these
            // survives the suite, and that is expected rather than a gap. The
            // safer expression is kept because it costs one normalisation per
            // twist driver and removes the question entirely.
            out[at++] = foundation::twistAngle(st.twist, kBoneAxis);
        }
    }
    return true;
}

}  // namespace mh::rig
