// SPDX-License-Identifier: AGPL-3.0-or-later
//
// See the header for what runs per frame and why everything that can fail
// happens at bind.
#include "makehuman/rig/CorrectiveRuntime.h"

#include "makehuman/foundation/Rbf.h"

#include <cmath>
#include <string>

namespace mh::rig {

std::string CorrectiveBindError::message() const {
    std::string s;
    switch (kind) {
        case CorrectiveBindErrorKind::TopologyMismatch:
            s = "these correctives were authored against a different base mesh";
            break;
        case CorrectiveBindErrorKind::UnknownJoint:
            s = "a driving joint is not in this skeleton";
            break;
        case CorrectiveBindErrorKind::VertexOutOfRange:
            s = "a delta indexes past the end of the mesh";
            break;
    }
    if (!detail.empty()) s += ": " + detail;
    return s;
}

std::expected<CorrectiveRuntime, CorrectiveBindError> CorrectiveRuntime::bind(
    const core::CompiledCorrectives& blob, const Skeleton& skeleton, uint64_t meshTopologyHash,
    std::span<const foundation::Vec3> rest) {
    // First, because it is the one that explains all the others: a corrective
    // set built for another mesh will also have joints that do not resolve and
    // indices that do not fit, and "wrong base mesh" is the useful answer.
    if (blob.topologyHash != meshTopologyHash) {
        return std::unexpected(CorrectiveBindError{CorrectiveBindErrorKind::TopologyMismatch, {}});
    }

    auto plan = planPoseSignal(skeleton, blob.drivers);
    if (!plan) {
        return std::unexpected(
            CorrectiveBindError{CorrectiveBindErrorKind::UnknownJoint, plan.error().detail});
    }

    const auto vertexCount = static_cast<uint32_t>(rest.size());
    for (size_t i = 0; i < blob.deltas.size(); ++i) {
        if (!blob.deltas[i].empty() && blob.deltas[i].maxVertexIndex >= vertexCount) {
            return std::unexpected(CorrectiveBindError{
                CorrectiveBindErrorKind::VertexOutOfRange,
                std::string(i < blob.poseNames.size() ? blob.poseNames[i] : "")});
        }
    }

    CorrectiveRuntime rt;
    rt.plan_         = std::move(*plan);
    rt.coefficients_ = blob.coefficients;
    rt.deltas_       = blob.deltas;
    rt.buffer_.setRest(rest);
    rt.signal_.resize(rt.plan_.dimension);
    rt.weights_.resize(blob.poseCount);
    rt.perPose_.resize(blob.poseCount);
    return rt;
}

bool CorrectiveRuntime::setRest(std::span<const foundation::Vec3> rest) {
    if (rest.size() != buffer_.positions().size()) return false;
    buffer_.setRest(rest);
    return true;
}

bool CorrectiveRuntime::update(std::span<const foundation::Mat4> localPose) {
    if (!evaluatePoseSignal(plan_, localPose, signal_)) return false;
    if (!foundation::rbfEvaluate(coefficients_, signal_, weights_)) return false;

    // Narrowed to float here rather than widening the buffer: a corrective
    // weight is a blend factor and the geometry it scales is float anyway.
    //
    // AND a negligible weight is forced to exactly zero, which is what makes
    // the dirty list mean anything. A Gaussian RBF never returns 0: measured on
    // the three-pose fixture, a pose that is fully OFF comes out at 2.7e-16,
    // not 0. `CorrectiveBuffer` skips only exact zeros -- deliberately, since a
    // magnitude threshold is a policy with a visible consequence and did not
    // belong in a buffer -- so without this every corrective is "active" every
    // frame and the dirty list is the whole mesh.
    //
    // 1e-6 is chosen against what a float vertex can represent, not by taste: a
    // delta of a few decimetres scaled by 1e-6 moves a coordinate by 1e-7 dm,
    // which is below the float ulp at that magnitude (about 1e-6 dm near 17).
    // So nothing this drops could have moved anything, and there is no
    // threshold to pop across.
    constexpr double kNegligible = 1e-6;
    for (size_t i = 0; i < weights_.size(); ++i) {
        perPose_[i] = std::abs(weights_[i]) < kNegligible ? 0.0F : static_cast<float>(weights_[i]);
    }
    return buffer_.apply(deltas_, perPose_);
}

}  // namespace mh::rig
