// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include "makehuman/foundation/Types.h"
#include "makehuman/rig/Skeleton.h"

#include <array>
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

    /// How many vertices had more influences than `influences` and so lost
    /// some, and the largest influence count seen before truncation.
    ///
    /// This is not a rare edge case worth ignoring: compiling the shipped rig
    /// to 4 -- which is what glTF's JOINTS_0/WEIGHTS_0 require -- clamps
    /// **3,665 of 19,158 vertices (19.1%)**, the worst of them carrying 12.
    /// compile() always computed this and used to discard it, so a fifth of the
    /// mesh silently lost influence. A caller that exports should say so.
    size_t clampedVertices{};
    uint8_t maxInfluences{};

    [[nodiscard]] size_t vertexCount() const noexcept {
        return influences != 0 ? boneIndex.size() / influences : 0;
    }
};

/// Skin weights for a fitted proxy, derived from the body's.
///
/// A `.mhclo` proxy vertex is a weighted blend of three base-mesh vertices --
/// that is what fitting IS -- so its skin weights are the same blend of those
/// vertices' weights. Nothing here is a heuristic: the vertex is already
/// defined as that combination, and skinning it any other way would move it off
/// the surface it was fitted to.
///
/// Why it matters: a live-rig export ships REST geometry and lets the consumer
/// pose it, so anything shipped UNSKINNED stays where it was while the body
/// moves. Measured 2026-09-07, before this existed: the eyes protruded from
/// their sockets in both our glTF and our FBX, and the same export with no pose
/// was clean.
///
/// Takes spans rather than a `core::Proxy` so `mh_rig` needs no dependency on
/// `mh_core` for it -- and so a test can state a fitting basis in three lines.
///
/// @param body   per-BASE-MESH-vertex weights, i.e. before the render unweld.
/// @param refs   three base vertices per proxy vertex (`Proxy::refVerts`).
/// @param fit    their fitting weights (`Proxy::weights`), parallel to @p refs.
/// @return weights with the same `influences` as @p body, strongest first and
///         normalised; EMPTY if the inputs do not describe each other or a ref
///         index is out of range. A silently truncated skin binds the rest to
///         bone 0, which looks like a proxy glued to the hips.
[[nodiscard]] CompiledWeights proxyWeights(const CompiledWeights& body,
                                           std::span<const std::array<uint32_t, 3>> refs,
                                           std::span<const std::array<float, 3>> fit);

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
