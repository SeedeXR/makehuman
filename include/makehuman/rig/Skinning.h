// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include "makehuman/foundation/Types.h"
#include "makehuman/rig/Skeleton.h"
#include "makehuman/rig/VertexWeights.h"

#include <span>
#include <vector>

namespace mh::rig {

using foundation::Mat4;

/// Builds the per-bone skinning matrices for @p skeleton under @p localPose.
///
/// Returns `matPoseGlobal * inv(matRestGlobal)` per bone (`skeleton.py:909`) --
/// the matrix that undoes a bone's rest transform and reapplies its posed one,
/// so a vertex at rest under an identity pose does not move. `matPoseGlobal`
/// itself is an intermediate and is not returned: nothing needs it, and glTF
/// skin export wants the LOCAL transform rather than the global one.
///
/// @param localPose one `matPose` per bone, in skeleton bone order. Identity
///        means "unposed". Pass an empty span for the rest pose.
///
/// Bones are visited in order, which is safe precisely because loadSkeleton
/// guarantees parents precede children -- the parent's global matrix is final
/// before any child reads it.
///
/// `inv(matRestGlobal)` uses `rigidInverse`. The reference uses a general
/// inverse and wraps it in a bare `except` because a degenerate bone makes the
/// matrix singular; ours cannot be singular, because buildRestMatrices refuses
/// a zero-length bone and produces an orthonormal basis, so the transpose-based
/// inverse is both exact and total.
[[nodiscard]] std::vector<Mat4> computeSkinningMatrices(const Skeleton& skeleton,
                                                        std::span<const Mat4> localPose);

/// Linear blend skinning, positions only.
///
/// This is **accumulated matrix skinning**: the per-bone matrices are blended
/// first and the result applied once, rather than transforming the vertex by
/// each bone and blending the positions (`animation.py:1121-1206`). For affine
/// transforms the two agree, but this costs one matrix-vector multiply per
/// vertex instead of one per influence.
///
/// @param rest      the unposed vertex positions.
/// @param weights   compiled influences; its vertex count must match @p rest.
/// @param skinning  computeSkinningMatrices()'s result, indexed by bone.
/// @param out       resized to @p rest's size.
/// @return false if the inputs disagree about the vertex count, or a weight
///         names a bone the pose does not have.
bool skinPositions(std::span<const foundation::Vec3> rest, const CompiledWeights& weights,
                   std::span<const Mat4> skinning, std::vector<foundation::Vec3>& out);

}  // namespace mh::rig
