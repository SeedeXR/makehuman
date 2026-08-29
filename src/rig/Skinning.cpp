// SPDX-License-Identifier: AGPL-3.0-or-later
#include "makehuman/rig/Skinning.h"

namespace mh::rig {

std::vector<Mat4> computeSkinningMatrices(const Skeleton& skeleton,
                                          std::span<const Mat4> localPose) {
    const size_t n = skeleton.bones.size();

    // matPoseGlobal is only ever read by the next bone in the chain, so it
    // stays local.
    std::vector<Mat4> global(n);
    std::vector<Mat4> skinning(n);

    const Mat4 identity = Mat4::identity();

    for (size_t i = 0; i < n; ++i) {
        const Bone& b       = skeleton.bones[i];
        const Mat4& matPose = (i < localPose.size()) ? localPose[i] : identity;

        const Mat4 relPosed = b.matRestRelative * matPose;

        // Parents precede children (loadSkeleton guarantees it), so the
        // parent's global matrix is already final here.
        global[i] = (b.parent < 0) ? relPosed : global[static_cast<size_t>(b.parent)] * relPosed;

        skinning[i] = global[i] * foundation::rigidInverse(b.matRestGlobal);
    }
    return skinning;
}

bool skinPositions(std::span<const foundation::Vec3> rest, const CompiledWeights& weights,
                   std::span<const Mat4> skinning, std::vector<foundation::Vec3>& out) {
    const size_t n = rest.size();
    if (weights.vertexCount() != n) return false;

    const size_t infl = weights.influences;
    if (infl == 0) return false;

    for (const uint32_t b : weights.boneIndex) {
        if (b >= skinning.size()) return false;
    }

    out.assign(n, foundation::Vec3{});

    for (size_t v = 0; v < n; ++v) {
        // Blend the MATRICES, then apply once. Only the top 3 rows matter: the
        // fourth is (0,0,0,1) for every affine transform involved.
        float acc[3][4] = {};
        for (size_t i = 0; i < infl; ++i) {
            const float w = weights.weight[v * infl + i];
            if (w == 0.0F) continue;
            const Mat4& m = skinning[weights.boneIndex[v * infl + i]];
            for (size_t r = 0; r < 3; ++r) {
                for (size_t c = 0; c < 4; ++c)
                    acc[r][c] += w * m.m[r][c];
            }
        }

        const foundation::Vec3& p = rest[v];
        // Homogeneous w = 1: translation applies. Directions would use 0.
        out[v] = foundation::Vec3{acc[0][0] * p.x + acc[0][1] * p.y + acc[0][2] * p.z + acc[0][3],
                                  acc[1][0] * p.x + acc[1][1] * p.y + acc[1][2] * p.z + acc[1][3],
                                  acc[2][0] * p.x + acc[2][1] * p.y + acc[2][2] * p.z + acc[2][3]};
    }
    return true;
}

}  // namespace mh::rig
