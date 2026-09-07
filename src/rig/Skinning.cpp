// SPDX-License-Identifier: AGPL-3.0-or-later
#include "makehuman/foundation/Transform.h"

#include <algorithm>
#include <cmath>
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

namespace {

/// A rigid transform as a dual quaternion: `real` rotates, `dual` carries the
/// translation as `0.5 * t * real`.
struct DualQuat {
    foundation::Quat real;
    foundation::Quat dual;
};

/// How far @p m departs from rigid: the largest error in `R * Rt == I`.
///
/// Checked rather than assumed. DQS has no spelling for scale or shear, and a
/// caller who scaled a bone deserves a refusal instead of a mesh that is
/// quietly the wrong size.
double rigidError(const Mat4& m) {
    double worst = 0.0;
    for (size_t i = 0; i < 3; ++i) {
        for (size_t j = 0; j < 3; ++j) {
            double dot = 0.0;
            for (size_t k = 0; k < 3; ++k) {
                dot += static_cast<double>(m.m[i][k]) * static_cast<double>(m.m[j][k]);
            }
            worst = std::max(worst, std::abs(dot - (i == j ? 1.0 : 0.0)));
        }
    }
    return worst;
}

DualQuat toDualQuat(const Mat4& m) {
    const foundation::Quat r = foundation::quaternionFromMatrix(m);
    // dual = 0.5 * (0, t) * real, the Hamilton product of the pure-vector
    // translation quaternion with the rotation.
    const foundation::Quat t{0.0, static_cast<double>(m.m[0][3]), static_cast<double>(m.m[1][3]),
                             static_cast<double>(m.m[2][3])};
    const foundation::Quat d = foundation::quaternionMultiply(t, r);
    return DualQuat{r, foundation::Quat{d.w * 0.5, d.x * 0.5, d.y * 0.5, d.z * 0.5}};
}

double dot(const foundation::Quat& a, const foundation::Quat& b) {
    return (a.w * b.w) + (a.x * b.x) + (a.y * b.y) + (a.z * b.z);
}

}  // namespace

bool skinPositionsDqs(std::span<const foundation::Vec3> rest, const CompiledWeights& weights,
                      std::span<const Mat4> skinning, std::vector<foundation::Vec3>& out) {
    const size_t n = rest.size();
    if (weights.vertexCount() != n) return false;

    const size_t infl = weights.influences;
    if (infl == 0) return false;

    for (const uint32_t b : weights.boneIndex) {
        if (b >= skinning.size()) return false;
    }

    // Converted once for the whole mesh rather than per vertex: a skeleton has
    // a couple of hundred bones and a mesh has tens of thousands of vertices.
    std::vector<DualQuat> bones;
    bones.reserve(skinning.size());
    for (const Mat4& m : skinning) {
        // 1e-4 rather than an epsilon: these matrices are built from float
        // rest poses, so an exactly-rigid one still drifts. A real scale is
        // orders of magnitude larger than the drift.
        if (rigidError(m) > 1e-4) return false;
        bones.push_back(toDualQuat(m));
    }

    out.assign(n, foundation::Vec3{});

    for (size_t v = 0; v < n; ++v) {
        // The PIVOT is the first bone with any weight. Every other bone's
        // quaternion is flipped to the same hemisphere before it is added,
        // because q and -q are the same rotation but blend to opposite things
        // -- without this the blend takes the long way round and produces a
        // smooth, plausible, completely wrong pose.
        const DualQuat* pivot = nullptr;
        foundation::Quat real{0.0, 0.0, 0.0, 0.0};
        foundation::Quat dual{0.0, 0.0, 0.0, 0.0};

        for (size_t i = 0; i < infl; ++i) {
            double w = static_cast<double>(weights.weight[v * infl + i]);
            if (w == 0.0) continue;
            const DualQuat& b = bones[weights.boneIndex[v * infl + i]];
            if (pivot == nullptr) {
                pivot = &b;
            } else if (dot(pivot->real, b.real) < 0.0) {
                w = -w;
            }
            real.w += w * b.real.w;
            real.x += w * b.real.x;
            real.y += w * b.real.y;
            real.z += w * b.real.z;
            dual.w += w * b.dual.w;
            dual.x += w * b.dual.x;
            dual.y += w * b.dual.y;
            dual.z += w * b.dual.z;
        }

        const foundation::Vec3& p = rest[v];
        const double length       = std::sqrt(dot(real, real));
        if (length < 1e-12) {
            // Every weight zero, or they cancelled exactly. Leaving the vertex
            // where it was is the only answer that is not invented.
            out[v] = p;
            continue;
        }

        const double inv = 1.0 / length;
        real             = {real.w * inv, real.x * inv, real.y * inv, real.z * inv};
        dual             = {dual.w * inv, dual.x * inv, dual.y * inv, dual.z * inv};

        // Rotate by the real part, then add the translation recovered from the
        // dual part: t = 2 * dual * conjugate(real).
        const foundation::Mat4 r = foundation::quaternionMatrix(real);
        const foundation::Quat conj{real.w, -real.x, -real.y, -real.z};
        const foundation::Quat t = foundation::quaternionMultiply(dual, conj);

        const double px = static_cast<double>(p.x);
        const double py = static_cast<double>(p.y);
        const double pz = static_cast<double>(p.z);
        const auto at = [&r](size_t row, size_t col) { return static_cast<double>(r.m[row][col]); };
        const double x = (at(0, 0) * px) + (at(0, 1) * py) + (at(0, 2) * pz) + (2.0 * t.x);
        const double y = (at(1, 0) * px) + (at(1, 1) * py) + (at(1, 2) * pz) + (2.0 * t.y);
        const double z = (at(2, 0) * px) + (at(2, 1) * py) + (at(2, 2) * pz) + (2.0 * t.z);
        out[v] =
            foundation::Vec3{static_cast<float>(x), static_cast<float>(y), static_cast<float>(z)};
    }
    return true;
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

SkinData buildSkinData(const Skeleton& skeleton, const CompiledWeights& weights,
                       std::span<const uint32_t> vmap) {
    SkinData out;
    if (weights.influences != 4) return out;

    out.influences = weights.influences;
    out.jointNames.reserve(skeleton.bones.size());
    out.jointParents.reserve(skeleton.bones.size());
    out.globalRest.reserve(skeleton.bones.size());

    for (const auto& b : skeleton.bones) {
        out.jointNames.push_back(b.name);
        out.jointParents.push_back(b.parent);
        out.globalRest.push_back(b.matRestGlobal);
    }

    const size_t infl = out.influences;
    out.joints.resize(vmap.size() * infl);
    out.weights.resize(vmap.size() * infl);

    for (size_t rv = 0; rv < vmap.size(); ++rv) {
        const uint32_t mv = vmap[rv];
        if (mv >= weights.vertexCount()) {
            // vmap indexes a mesh the weights do not describe; returning a
            // half-filled skin would export silently wrong geometry.
            return SkinData{};
        }
        for (size_t i = 0; i < infl; ++i) {
            out.joints[rv * infl + i]  = weights.boneIndex[mv * infl + i];
            out.weights[rv * infl + i] = weights.weight[mv * infl + i];
        }
    }
    return out;
}

std::vector<Mat4> poseToBoneLocal(const Skeleton& skeleton, std::span<const Mat4> globalPose) {
    if (globalPose.size() != skeleton.boneCount()) return {};

    std::vector<Mat4> out(globalPose.size());
    for (size_t i = 0; i < globalPose.size(); ++i) {
        const Mat4& rest   = skeleton.bones[i].matRestGlobal;
        const Mat4 invRest = foundation::rigidInverse(rest);

        // Rotation only: the conjugation is defined on the 3x3 block, and
        // carrying the source translation through it as well would double-count
        // the offset the rest matrix already holds.
        Mat4 rot = Mat4::identity();
        for (size_t r = 0; r < 3; ++r) {
            for (size_t c = 0; c < 3; ++c)
                rot.m[r][c] = globalPose[i].m[r][c];
        }

        Mat4 local = invRest * rot * rest;

        // Translation is expressed in bone-local axis directions -- rotated by
        // the inverse rest basis, not transformed by it, so no offset is added.
        const float tx = globalPose[i].m[0][3];
        const float ty = globalPose[i].m[1][3];
        const float tz = globalPose[i].m[2][3];
        local.m[0][3]  = invRest.m[0][0] * tx + invRest.m[0][1] * ty + invRest.m[0][2] * tz;
        local.m[1][3]  = invRest.m[1][0] * tx + invRest.m[1][1] * ty + invRest.m[1][2] * tz;
        local.m[2][3]  = invRest.m[2][0] * tx + invRest.m[2][1] * ty + invRest.m[2][2] * tz;
        out[i]         = local;
    }
    return out;
}

}  // namespace mh::rig
