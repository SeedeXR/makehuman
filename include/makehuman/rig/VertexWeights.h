// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include "makehuman/foundation/Types.h"
#include "makehuman/rig/Skeleton.h"

#include <cstdint>
#include <expected>
#include <filesystem>
#include <span>
#include <string>
#include <unordered_map>
#include <vector>

namespace mh::rig {

/// Sub-threshold weights are dropped (`animation.py` WEIGHT_THRESHOLD). An
/// influence this small moves a vertex by less than float precision but still
/// costs a matrix multiply per frame.
inline constexpr float kWeightThreshold = 1e-4F;

/// One bone's influence list: parallel vertex and weight arrays, sorted by
/// vertex index.
struct BoneWeights {
    std::vector<uint32_t> verts;
    std::vector<float> weights;
};

/// Per-vertex influences, truncated to a fixed count and re-normalised.
///
/// This is what a GPU skinning path and glTF both want: glTF's JOINTS_0 /
/// WEIGHTS_0 are exactly 4-wide, which is why 4 is the interesting case.
struct CompiledWeights {
    uint8_t influences{4};
    /// influences entries per vertex, strongest first.
    std::vector<uint32_t> boneIndex;
    std::vector<float> weight;

    [[nodiscard]] size_t vertexCount() const noexcept {
        return influences != 0 ? boneIndex.size() / influences : 0;
    }
};

enum class WeightsErrorKind { NotFound, Unreadable, Malformed, VertexOutOfRange };

struct WeightsError {
    WeightsErrorKind kind{};
    std::string file;
    std::string detail;

    [[nodiscard]] std::string message() const;
};

/// Vertex-to-bone weights loaded from a `.mhw` (which is JSON).
///
/// Ported from `legacy/python/shared/animation.py:494-620`. The normalisation
/// in `load()` is not cosmetic -- it is what makes the weights usable at all:
///
///  1. `wtot[v]` is summed over EVERY bone first, then each weight is stored as
///     `w / wtot[v]`. So the file's numbers are relative, not absolute, and a
///     vertex's influences always end up summing to 1.
///  2. A vertex listed twice under the same bone has its weights merged.
///  3. Weights at or below the threshold are dropped, AFTER normalising.
///  4. **A vertex with no weight at all is bound to the root bone with weight
///     1.** Without this an unweighted vertex collapses to the origin the
///     moment the rig is posed.
struct VertexWeights {
    std::string name;
    std::string description;
    int32_t version{};

    /// Bone name -> its influence list.
    std::unordered_map<std::string, BoneWeights> perBone;
    size_t vertexCount{};

    /// The largest number of bones influencing any single vertex. The base rig
    /// reaches 12, which is why truncation exists.
    [[nodiscard]] uint8_t maxInfluences() const;

    /// Builds the per-vertex form against @p skeleton's bone order.
    ///
    /// A vertex with more than @p influences bones keeps the strongest ones and
    /// is re-normalised so the kept weights still sum to 1; dropping without
    /// re-normalising would darken every heavily-weighted vertex. A vertex with
    /// fewer keeps bone index 0 and weight 0 in the unused slots.
    ///
    /// Ties are broken by **descending bone index**, matching Python's
    /// `sorted(reverse=True)` over `(weight, bone_index)` tuples. That looks
    /// arbitrary because it is, but it decides which influence survives
    /// truncation on symmetric vertices, so it must be replicated.
    [[nodiscard]] CompiledWeights compile(const Skeleton& skeleton, uint8_t influences) const;
};

[[nodiscard]] std::expected<VertexWeights, WeightsError> loadWeights(
    const std::filesystem::path& path, size_t vertexCount, const std::string& rootBone = "root");

}  // namespace mh::rig
