// SPDX-License-Identifier: AGPL-3.0-or-later
#include "makehuman/rig/CentersOfRotation.h"

#include "makehuman/foundation/Transform.h"

#include <algorithm>
#include <cmath>
#include <unordered_map>

namespace mh::rig {

namespace {

/// @p bone's weight in a sparse influence list, or 0 when it is not there.
///
/// Linear, because the lists are four entries long: a map would cost more in
/// construction than it saves in lookup, and this runs inside the precompute's
/// innermost loop.
float weightOf(std::span<const uint32_t> bones, std::span<const float> weights, uint32_t bone) {
    for (size_t i = 0; i < bones.size() && i < weights.size(); ++i) {
        if (bones[i] == bone) return weights[i];
    }
    return 0.0F;
}

}  // namespace

float weightSimilarity(std::span<const uint32_t> bonesA, std::span<const float> weightsA,
                       std::span<const uint32_t> bonesB, std::span<const float> weightsB,
                       float sigma) {
    if (sigma <= 0.0F) return 0.0F;
    const float invSigmaSq = 1.0F / (sigma * sigma);

    // j and k range over A's bones ONLY. A term needs `w_pj` and `w_pk`
    // non-zero, and every bone A does not name has weight zero there, so a bone
    // outside this list can never contribute.
    float sum = 0.0F;
    for (size_t j = 0; j < bonesA.size() && j < weightsA.size(); ++j) {
        const float wpj = weightsA[j];
        if (wpj == 0.0F) continue;
        const float wvj = weightOf(bonesB, weightsB, bonesA[j]);
        if (wvj == 0.0F) continue;  // B is not attached to this bone
        for (size_t k = 0; k < bonesA.size() && k < weightsA.size(); ++k) {
            if (j == k) continue;  // the sum is over PAIRS of distinct bones
            const float wpk = weightsA[k];
            if (wpk == 0.0F) continue;
            const float wvk = weightOf(bonesB, weightsB, bonesA[k]);
            if (wvk == 0.0F) continue;

            // Zero exactly when the two weight RATIOS agree, which is when the
            // two vertices are carried through a bend identically.
            const float d = (wpj * wvk) - (wpk * wvj);
            sum += wpj * wpk * wvj * wvk * std::exp(-d * d * invSigmaSq);
        }
    }
    return sum;
}

std::vector<foundation::Vec3> computeCentersOfRotation(std::span<const foundation::Vec3> rest,
                                                       std::span<const uint32_t> triangles,
                                                       const CompiledWeights& weights,
                                                       float sigma) {
    if (triangles.size() % 3 != 0) return {};
    if (weights.influences == 0 || weights.vertexCount() != rest.size()) return {};
    const size_t influences    = weights.influences;
    const size_t triangleCount = triangles.size() / 3;

    // ---- per triangle: its mean weights, its centroid, its area ------------
    // The mean is over the three corners, so a triangle spanning a joint
    // carries both bones and is similar to the vertices on either side of it.
    std::vector<std::vector<uint32_t>> triBones(triangleCount);
    std::vector<std::vector<float>> triWeights(triangleCount);
    std::vector<foundation::Vec3> triCentroid(triangleCount);
    std::vector<float> triArea(triangleCount);

    for (size_t t = 0; t < triangleCount; ++t) {
        const uint32_t i0 = triangles[t * 3];
        const uint32_t i1 = triangles[t * 3 + 1];
        const uint32_t i2 = triangles[t * 3 + 2];
        if (i0 >= rest.size() || i1 >= rest.size() || i2 >= rest.size()) return {};

        const foundation::Vec3& a = rest[i0];
        const foundation::Vec3& b = rest[i1];
        const foundation::Vec3& c = rest[i2];
        triCentroid[t] = foundation::Vec3{(a.x + b.x + c.x) / 3.0F, (a.y + b.y + c.y) / 3.0F,
                                          (a.z + b.z + c.z) / 3.0F};
        // Half the cross product's length. A degenerate triangle contributes
        // nothing rather than a division by zero later.
        const foundation::Vec3 ab{b.x - a.x, b.y - a.y, b.z - a.z};
        const foundation::Vec3 ac{c.x - a.x, c.y - a.y, c.z - a.z};
        const foundation::Vec3 n{(ab.y * ac.z) - (ab.z * ac.y), (ab.z * ac.x) - (ab.x * ac.z),
                                 (ab.x * ac.y) - (ab.y * ac.x)};
        triArea[t] = 0.5F * std::sqrt((n.x * n.x) + (n.y * n.y) + (n.z * n.z));

        for (const uint32_t corner : {i0, i1, i2}) {
            for (size_t k = 0; k < influences; ++k) {
                const uint32_t bone = weights.boneIndex[(corner * influences) + k];
                const float w       = weights.weight[(corner * influences) + k] / 3.0F;
                if (w == 0.0F) continue;
                const auto at = std::find(triBones[t].begin(), triBones[t].end(), bone);
                if (at == triBones[t].end()) {
                    triBones[t].push_back(bone);
                    triWeights[t].push_back(w);
                } else {
                    triWeights[t][static_cast<size_t>(at - triBones[t].begin())] += w;
                }
            }
        }
    }

    // ---- bone -> triangles, so a vertex visits only what can matter --------
    // `weightSimilarity` is zero unless the two share at least two bones, so a
    // triangle sharing NO bone with the vertex cannot contribute. Skipping
    // those is what turns 19,158 x 36,972 into something affordable: a vertex
    // touches four bones, and a bone touches a small neighbourhood.
    std::unordered_map<uint32_t, std::vector<uint32_t>> bonesToTriangles;
    for (size_t t = 0; t < triangleCount; ++t) {
        for (const uint32_t bone : triBones[t]) {
            bonesToTriangles[bone].push_back(static_cast<uint32_t>(t));
        }
    }

    std::vector<foundation::Vec3> centers(rest.begin(), rest.end());
    std::vector<uint32_t> candidates;
    for (size_t v = 0; v < rest.size(); ++v) {
        const std::span<const uint32_t> vBones{&weights.boneIndex[v * influences], influences};
        const std::span<const float> vWeights{&weights.weight[v * influences], influences};

        candidates.clear();
        for (size_t k = 0; k < influences; ++k) {
            if (vWeights[k] == 0.0F) continue;
            const auto found = bonesToTriangles.find(vBones[k]);
            if (found == bonesToTriangles.end()) continue;
            candidates.insert(candidates.end(), found->second.begin(), found->second.end());
        }
        std::sort(candidates.begin(), candidates.end());
        candidates.erase(std::unique(candidates.begin(), candidates.end()), candidates.end());

        double sumW = 0.0;
        double sumX = 0.0;
        double sumY = 0.0;
        double sumZ = 0.0;
        for (const uint32_t t : candidates) {
            const float s = weightSimilarity(vBones, vWeights, triBones[t], triWeights[t], sigma);
            if (s == 0.0F) continue;
            const double contribution = static_cast<double>(s) * static_cast<double>(triArea[t]);
            sumW += contribution;
            sumX += contribution * static_cast<double>(triCentroid[t].x);
            sumY += contribution * static_cast<double>(triCentroid[t].y);
            sumZ += contribution * static_cast<double>(triCentroid[t].z);
        }
        // Zero means nothing on the surface bends like this vertex -- a rigid
        // binding, most often. It keeps the rest position it was seeded with.
        if (sumW > 0.0) {
            centers[v] =
                foundation::Vec3{static_cast<float>(sumX / sumW), static_cast<float>(sumY / sumW),
                                 static_cast<float>(sumZ / sumW)};
        }
    }
    return centers;
}

bool skinPositionsCor(std::span<const foundation::Vec3> rest, const CompiledWeights& weights,
                      std::span<const foundation::Mat4> skinning,
                      std::span<const foundation::Vec3> centers,
                      std::vector<foundation::Vec3>& out) {
    if (weights.influences == 0) return false;
    if (weights.vertexCount() != rest.size()) return false;
    // Checked rather than clamped: reusing the last centre for the tail of the
    // mesh would rotate those vertices about the wrong pivot and look like a
    // modelling error rather than a missing input.
    if (centers.size() != rest.size()) return false;
    const size_t influences = weights.influences;

    out.assign(rest.size(), foundation::Vec3{});
    for (size_t v = 0; v < rest.size(); ++v) {
        // The rotation, blended as a quaternion and renormalised. This is the
        // half CoR shares with DQS, and the reason a twist survives: averaging
        // the MATRICES would shrink the rotation toward zero.
        foundation::Quat blended{0.0, 0.0, 0.0, 0.0};
        foundation::Quat reference{};
        bool haveReference = false;

        // ...and the ordinary linear blend, which is what carries the centre.
        foundation::Mat4 linear{};
        for (auto& row : linear.m)
            row.fill(0.0F);

        for (size_t k = 0; k < influences; ++k) {
            const uint32_t bone = weights.boneIndex[(v * influences) + k];
            const float w       = weights.weight[(v * influences) + k];
            if (w == 0.0F) continue;
            if (bone >= skinning.size()) return false;
            const foundation::Mat4& m = skinning[bone];

            foundation::Quat q = foundation::quaternionFromMatrix(m);
            if (!haveReference) {
                reference     = q;
                haveReference = true;
            } else {
                // q and -q are the same rotation; blending across the sign flip
                // would cancel instead of interpolate. Line every influence up
                // with the first one.
                const double dot = (q.w * reference.w) + (q.x * reference.x) + (q.y * reference.y) +
                                   (q.z * reference.z);
                if (dot < 0.0) q = foundation::Quat{-q.w, -q.x, -q.y, -q.z};
            }
            const double dw = static_cast<double>(w);
            blended.w += dw * q.w;
            blended.x += dw * q.x;
            blended.y += dw * q.y;
            blended.z += dw * q.z;
            for (size_t r = 0; r < 4; ++r) {
                for (size_t c = 0; c < 4; ++c)
                    linear.m[r][c] += w * m.m[r][c];
            }
        }
        if (!haveReference) {
            out[v] = rest[v];  // no influence at all: leave it where it was
            continue;
        }

        // The blended matrix applied to a point. Named because it is used
        // twice -- for the centre, and for the degenerate fallback below.
        const auto applyLinear = [&linear](const foundation::Vec3& p) {
            return foundation::Vec3{(linear.m[0][0] * p.x) + (linear.m[0][1] * p.y) +
                                        (linear.m[0][2] * p.z) + linear.m[0][3],
                                    (linear.m[1][0] * p.x) + (linear.m[1][1] * p.y) +
                                        (linear.m[1][2] * p.z) + linear.m[1][3],
                                    (linear.m[2][0] * p.x) + (linear.m[2][1] * p.y) +
                                        (linear.m[2][2] * p.z) + linear.m[2][3]};
        };

        const double len = std::sqrt((blended.w * blended.w) + (blended.x * blended.x) +
                                     (blended.y * blended.y) + (blended.z * blended.z));
        // Every influence cancelled -- only reachable with opposing signed
        // weights, which `compile` does not produce. LBS is the honest answer
        // rather than a normalisation by zero.
        if (len < 1e-12) {
            out[v] = applyLinear(rest[v]);
            continue;
        }
        blended.w /= len;
        blended.x /= len;
        blended.y /= len;
        blended.z /= len;
        const foundation::Mat4 rotation = foundation::quaternionMatrix(blended);

        // v' = R (v - p*) + LBS(p*)
        const foundation::Vec3& centre = centers[v];
        const foundation::Vec3 offset{rest[v].x - centre.x, rest[v].y - centre.y,
                                      rest[v].z - centre.z};
        const foundation::Vec3 turned{
            (rotation.m[0][0] * offset.x) + (rotation.m[0][1] * offset.y) +
                (rotation.m[0][2] * offset.z),
            (rotation.m[1][0] * offset.x) + (rotation.m[1][1] * offset.y) +
                (rotation.m[1][2] * offset.z),
            (rotation.m[2][0] * offset.x) + (rotation.m[2][1] * offset.y) +
                (rotation.m[2][2] * offset.z)};
        const foundation::Vec3 movedCentre = applyLinear(centre);
        out[v] = foundation::Vec3{turned.x + movedCentre.x, turned.y + movedCentre.y,
                                  turned.z + movedCentre.z};
    }
    return true;
}

}  // namespace mh::rig
