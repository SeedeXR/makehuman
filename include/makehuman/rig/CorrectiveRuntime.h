// SPDX-License-Identifier: AGPL-3.0-or-later
//
// The whole corrective chain, bound to one character.
//
// Every piece of owner directive 12's pose-space deformer existed before this
// and none of them met. What runs per frame:
//
//   rig::evaluatePoseSignal   posed skeleton -> a point in signal space
//   foundation::rbfEvaluate   that point     -> a weight per example pose
//   core::CorrectiveBuffer    those weights  -> moved rest-space vertices
//
// with the deltas read in place out of a compiled blob. Correctives apply
// PRE-SKIN, in rest space (directive 12.2), so the caller skins `positions()`
// rather than the rest mesh.
//
// **This is where the topology-hash guard finally does something.** The
// manifest has recorded which base mesh a corrective was authored against since
// it was written, and nothing compared it. A `.target` is a list of VERTEX
// INDICES: bind a corrective authored against one mesh to a renumbered one and
// every delta lands somewhere else with nothing to notice. `bind` compares, and
// refuses (directive 12.7).
//
// Everything that can fail happens at BIND: an unknown driving joint, a
// mismatched topology, a delta that reaches past the mesh. `update` can only
// fail on a pose of the wrong shape, because a corrective that breaks
// mid-animation is the worst time to find out.
#pragma once

#include "makehuman/core/CorrectiveBlob.h"
#include "makehuman/core/Correctives.h"
#include "makehuman/core/Types.h"
#include "makehuman/rig/PoseSignal.h"
#include "makehuman/rig/Skeleton.h"

#include <cstdint>
#include <expected>
#include <span>
#include <string>
#include <vector>

namespace mh::rig {

enum class CorrectiveBindErrorKind : uint8_t {
    /// The blob was authored against a different base mesh.
    TopologyMismatch,
    /// A driver names a bone this skeleton does not have.
    UnknownJoint,
    /// A delta indexes past the end of the rest mesh. Normally the topology
    /// hash catches a mismatched mesh first; this is the case where the hash
    /// agreed and the payload did not.
    VertexOutOfRange,
};

struct CorrectiveBindError {
    CorrectiveBindErrorKind kind{};
    std::string detail;

    [[nodiscard]] std::string message() const;
};

/// A compiled corrective set, bound to a skeleton and a shaped rest mesh.
///
/// **Holds views into the blob's bytes**, through the delta spans that
/// `core::CompiledCorrectives` hands out. Keep the blob's buffer alive for as
/// long as this is used -- that is the whole point of the mappable layout, and
/// the price of not copying the bulk.
class CorrectiveRuntime {
public:
    /// @param meshTopologyHash `core::topologyHash` of the mesh being deformed.
    /// @param rest             the shaped rest mesh, which is what the deltas
    ///                         are added to and what gets skinned afterwards.
    [[nodiscard]] static std::expected<CorrectiveRuntime, CorrectiveBindError> bind(
        const core::CompiledCorrectives& blob, const Skeleton& skeleton, uint64_t meshTopologyHash,
        std::span<const foundation::Vec3> rest);

    /// One frame: signal, weights, deltas.
    ///
    /// @param localPose one matrix per bone in the bone's own rest frame --
    ///                  what `poseToBoneLocal` produces.
    /// @return false if @p localPose is not one matrix per bone.
    [[nodiscard]] bool update(std::span<const foundation::Mat4> localPose);

    /// The deformed rest-space positions. Skin these.
    [[nodiscard]] std::span<const foundation::Vec3> positions() const noexcept {
        return buffer_.positions();
    }

    /// The weight per example pose from the last `update`, in the blob's pose
    /// order. Exposed because it is the one place the chain can be inspected
    /// without geometry, which is what makes an end-to-end test readable.
    ///
    /// RAW, before the negligible-weight threshold `update` applies on the way
    /// into the buffer -- so a pose that is fully off reads here as the 1e-16
    /// the RBF actually produced rather than as a rounded zero.
    [[nodiscard]] std::span<const double> weights() const noexcept { return weights_; }

    /// How many vertices the last `update` moved.
    [[nodiscard]] size_t touched() const noexcept { return buffer_.touched(); }

private:
    PoseSignalPlan plan_;
    foundation::RbfCoefficients coefficients_;
    std::vector<core::TargetView> deltas_;
    core::CorrectiveBuffer buffer_;
    /// Scratch, reused every frame so the hot path allocates nothing.
    std::vector<double> signal_;
    std::vector<double> weights_;
    std::vector<float> perPose_;
};

}  // namespace mh::rig
