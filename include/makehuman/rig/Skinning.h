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
/// Rewrites a pose from model space into each bone's own rest frame.
///
/// A BVH stores rotations in the file's global axes. `computeSkinningMatrices`
/// wants `matPose`, which is that rotation expressed relative to the bone's rest
/// orientation, so every pose that comes off disk has to be conjugated first:
///
///     matPose = inv(matRestGlobal) * pose * matRestGlobal
///
/// and any translation is rotated into the same frame. This mirrors
/// `skeleton.py:566-593`, which is where the reference does it -- the BVH loader
/// hands back raw global matrices, and `Skeleton.setPose` converts them.
///
/// Skipping this does not fail. It produces a complete, smooth, entirely
/// plausible pose that is simply not the one in the file: the shipped T-pose
/// comes out with the arms only part-way raised (arm span 10.7 dm instead of
/// 16.0 dm), which looks like a stylistic difference rather than a bug.
///
/// @param globalPose one matrix per bone, in skeleton bone order.
/// @return bone-local matrices, or empty if @p globalPose is not one per bone.
[[nodiscard]] std::vector<Mat4> poseToBoneLocal(const Skeleton& skeleton,
                                                std::span<const Mat4> globalPose);

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
    /// The BIND pose: inverse-bind matrices are derived from these.
    std::vector<Mat4> globalRest;
    /// The POSED globals, or empty. See `foundation::SkinView::globalPose` --
    /// non-empty is what makes an export a live rig rather than a baked statue.
    std::vector<Mat4> globalPose;
    std::vector<uint32_t> joints;
    std::vector<float> weights;
    uint8_t influences{4};

    /// Designated initialisers on purpose: this was positional, and inserting
    /// `globalPose` in the middle silently bound three unrelated fields to the
    /// wrong members. It compiled far enough to be confusing.
    [[nodiscard]] foundation::SkinView view() const {
        return foundation::SkinView{.jointNames   = jointNames,
                                    .jointParents = jointParents,
                                    .globalRest   = globalRest,
                                    .globalPose   = globalPose,
                                    .joints       = joints,
                                    .weights      = weights,
                                    .influences   = influences};
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
