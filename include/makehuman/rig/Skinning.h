// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include "makehuman/foundation/Geometry.h"
#include "makehuman/foundation/Types.h"
#include "makehuman/rig/Skeleton.h"
#include "makehuman/rig/VertexWeights.h"

#include <span>
#include <string>
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

/// Owns the arrays a foundation::SkinView points at.
///
/// The view is non-owning by design, so something has to hold the storage; this
/// is that something. Keep it alive for as long as the view is used.
struct SkinData {
    std::vector<std::string> jointNames;
    std::vector<int32_t> jointParents;
    std::vector<Mat4> globalRest;
    std::vector<uint32_t> joints;
    std::vector<float> weights;
    uint8_t influences{4};

    [[nodiscard]] foundation::SkinView view() const {
        return foundation::SkinView{jointNames, jointParents, globalRest,
                                    joints,     weights,      influences};
    }
};

/// Builds export-ready skin data by expanding per-MESH-vertex weights onto the
/// unwelded render vertices.
///
/// The two vertex counts differ -- 19,158 mesh vertices become 21,833 render
/// vertices once UV seams are split -- and glTF's JOINTS_0/WEIGHTS_0 are vertex
/// ATTRIBUTES, so they must be indexed like positions. @p vmap is
/// RenderMesh::vmap(): render vertex -> mesh vertex. Skipping this expansion
/// leaves every vertex past the first seam weighted to the wrong bone.
///
/// @return empty jointNames if @p weights does not describe the mesh @p vmap
///         indexes, or if the influence count is not 4.
[[nodiscard]] SkinData buildSkinData(const Skeleton& skeleton, const CompiledWeights& weights,
                                     std::span<const uint32_t> vmap);

}  // namespace mh::rig
