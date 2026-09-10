// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include "makehuman/core/Mesh.h"
#include "makehuman/rig/CorrectiveRuntime.h"
#include "makehuman/rig/Skeleton.h"
#include "makehuman/rig/VertexWeights.h"

#include <cstdint>
#include <expected>
#include <vector>

namespace mh::rig {

/// Everything needed to re-pose a morphed mesh.
///
/// Held together because the rig has to be re-fitted after each morph:
/// `updateJoints` makes the skeleton follow the body, and skinning a changed
/// mesh with a stale rig rotates it about joints that have moved.
struct PoseRig {
    Skeleton skeleton;
    CompiledWeights weights;
    std::vector<foundation::Mat4> localPose;

    /// The mesh as it was BEFORE posing, and where the joints ended up after.
    /// Both empty unless `poseMesh` ran and applied a pose.
    ///
    /// A **live rig** export needs exactly this pair: rest vertices with bind
    /// matrices from the rest skeleton, and joint nodes at `globalPose`. The
    /// consumer then computes the deformation itself instead of receiving it
    /// pre-applied.
    std::vector<foundation::Vec3> restCoords;
    std::vector<foundation::Mat4> globalPose;

    /// The skeleton and weights are loaded. Independent of `posed()`: a rig
    /// with no pose is still a rig, and it is exactly what an export wants --
    /// the bind pose plus a usable skeleton.
    ///
    /// Derived rather than stored. A `loaded` flag beside `skeleton` is a
    /// second copy of the same fact and a second thing to forget to set.
    [[nodiscard]] bool loaded() const { return !skeleton.bones.empty(); }

    /// There is a pose to apply; `localPose` is empty otherwise.
    [[nodiscard]] bool posed() const { return !localPose.empty(); }
};

/// How a pose is blended onto the mesh.
///
/// Its own enum rather than `ui::Skinning`: `ui` is Apache-2.0 and must never
/// be depended on by an AGPL module (LICENSING.md 4). The application owns the
/// translation, as it already does for every other signal that crosses that
/// line.
enum class SkinningMethod : uint8_t { Linear, DualQuaternion };

struct PoseOptions {
    SkinningMethod method{SkinningMethod::Linear};

    /// False draws the character UNPOSED with the rig still loaded -- the
    /// reference's `_posed` (`shared/animation.py:986-991`), driven by the
    /// toolbar's Pose toggle.
    bool apply{true};

    /// Pose-space correctives, or null for none. Not owned; must outlive the
    /// call, along with the blob its deltas point into.
    ///
    /// Passed IN rather than applied by the caller beforehand, and the reason
    /// is the ORDER. `poseMesh` re-fits the skeleton to the mesh it is given,
    /// and a corrective is a pose-driven bulge rather than body shape: fitting
    /// the rig to it would move the joints the corrective is driven BY, so a
    /// shoulder bulge would shift the shoulder, which would change the signal,
    /// which would change the bulge. The corrective goes on AFTER the re-fit
    /// and before the skinning (directive 12.2: pre-skin, in rest space).
    ///
    /// `restCoords` stays uncorrected, so a LIVE-RIG export does not carry
    /// correctives -- a consumer with no pose-space runtime cannot evaluate
    /// them, and baking a raised-arm deltoid into something labelled "rest"
    /// would carry it into a lowered arm. A baked export does carry them.
    CorrectiveRuntime* correctives{nullptr};
};

enum class PoseError : uint8_t {
    RefitFailed,     ///< updateJoints or buildRestMatrices refused the mesh
    SkinningFailed,  ///< the weights and the skinning matrices disagree
    StoreFailed,     ///< the posed vertex count no longer matches the mesh
    /// The corrective runtime refused the mesh or the pose -- a vertex count
    /// that no longer matches what it was bound to, or a pose that is not one
    /// matrix per bone.
    CorrectiveFailed,
};

/// Applies @p rig's pose to @p mesh in place. A no-op when no pose is loaded.
///
/// **The mesh must already be at its morph base.** Posing is not idempotent:
/// this deforms whatever it is given, and the caller's rebuild -- for us
/// `Human::applyStack` -- is what puts the rest positions back first. That is
/// also why the result is written with `Mesh::changeCoords` and not
/// `setCoords`: the latter would make the POSED mesh the base that the next
/// rebuild resets to, which shipped as a 70 cm defect (session fifteen).
///
/// This lives here rather than in the application because it could not be
/// tested there: `main.cpp` is not linkable, and the ONE mutation that survived
/// that chunk was reverting the `changeCoords` above.
[[nodiscard]] std::expected<void, PoseError> poseMesh(core::Mesh& mesh, PoseRig& rig,
                                                      PoseOptions options);

}  // namespace mh::rig
