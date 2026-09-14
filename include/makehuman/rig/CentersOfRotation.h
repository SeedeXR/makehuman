// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Optimised centres of rotation (Le & Hodgins 2016), the rung above DQS.
//
// DQS fixed the candy wrapper by blending rotations instead of matrices, but it
// rotates every vertex about the JOINT. Real flesh near a bending joint rotates
// about a point further into the limb, which is why DQS still bulges at an
// elbow. CoR precomputes, per vertex, the point it should rotate about: a
// weighted average of the surface, weighted by how SIMILARLY each patch is
// skinned.
//
// The precompute is the cost. The runtime is one extra rotation per vertex.
#pragma once

#include "makehuman/foundation/Geometry.h"
#include "makehuman/rig/VertexWeights.h"

#include <cstdint>
#include <span>
#include <vector>

namespace mh::rig {

/// Le & Hodgins's similarity between two weight vectors.
///
///   s(p,v) = SUM over j != k of  w_pj w_pk w_vj w_vk exp( -(w_pj w_vk - w_pk w_vj)^2 / sigma^2 )
///
/// Reads as: for every PAIR of bones both vertices are attached to, how equal
/// are their two weight RATIOS. Equal ratios mean the two vertices are carried
/// through a bend identically, which is exactly when one should help locate the
/// other's centre of rotation.
///
/// **Two consequences that are not obvious and that the caller must handle.**
/// Every term needs all four of `w_pj w_pk w_vj w_vk` non-zero with `j != k`,
/// so the result is ZERO unless the two vertices share at least TWO bones. A
/// rigidly bound vertex -- one bone, weight 1 -- is therefore similar to
/// nothing at all, including itself. That is correct rather than a flaw: a
/// rigid vertex has no blending artefact to fix, so it has no centre to find,
/// and `computeCentersOfRotation` says so by leaving it at its rest position.
/// The same fact is the pruning rule that makes the precompute affordable.
///
/// @param sigma the ratio tolerance. 0.1 in the paper; larger blurs more
///        surface into each centre.
[[nodiscard]] float weightSimilarity(std::span<const uint32_t> bonesA,
                                     std::span<const float> weightsA,
                                     std::span<const uint32_t> bonesB,
                                     std::span<const float> weightsB, float sigma);

/// The centre of rotation for every vertex.
///
///   p*_i = SUM over triangles t of  s(w_i, w_t) * centroid_t * area_t
///          -------------------------------------------------------
///          SUM over triangles t of  s(w_i, w_t) * area_t
///
/// where `w_t` is the mean of the triangle's three vertex weights. Each vertex
/// asks the whole surface "which of you bends like me", and lands at the
/// centre of mass of the answer.
///
/// **A vertex with no similar surface keeps its REST position**, which is the
/// case `weightSimilarity`'s documentation warns about: a rigidly bound vertex
/// is similar to nothing, so the denominator is zero. That is not a failure to
/// report -- a rigid vertex has no blend artefact and therefore nothing to
/// correct, and rotating it about its own rest position is exactly what LBS
/// already does for it.
///
/// @param triangles three vertex indices per triangle, so `size() % 3 == 0`.
/// @return one centre per vertex, or EMPTY if the inputs disagree about the
///         vertex count, `triangles` is not a whole number of triangles, or a
///         triangle names a vertex the mesh does not have. `skinPositionsCor`
///         then refuses the empty result rather than skinning about the origin,
///         so a caller that ignores this still fails loudly.
[[nodiscard]] std::vector<foundation::Vec3> computeCentersOfRotation(
    std::span<const foundation::Vec3> rest, std::span<const uint32_t> triangles,
    const CompiledWeights& weights, float sigma = 0.1F);

/// Centre-of-rotation skinning, positions only.
///
/// Same contract as `skinPositions` and `skinPositionsDqs`, plus the centres.
///
/// The difference from DQS in one line: DQS rotates every vertex about the
/// JOINT, this rotates it about the vertex's OWN centre. That is what stops the
/// bulge DQS leaves on the inside of a bend -- the joint is the wrong pivot for
/// flesh that sits away from it.
///
///   v' = R (v - p*) + LBS(p*)
///
/// R is the vertex's bone rotations blended as quaternions and renormalised --
/// the same trick that makes DQS hold a twist -- and `LBS(p*)` is the ordinary
/// linear blend applied to the centre. So a vertex whose centre IS its rest
/// position gets exactly LBS, which is what a rigidly bound vertex should get.
///
/// @param centers one per vertex, from `computeCentersOfRotation`.
/// @return false if the inputs disagree about the vertex count, or a weight
///         names a bone the pose does not have.
bool skinPositionsCor(std::span<const foundation::Vec3> rest, const CompiledWeights& weights,
                      std::span<const foundation::Mat4> skinning,
                      std::span<const foundation::Vec3> centers,
                      std::vector<foundation::Vec3>& out);

}  // namespace mh::rig
