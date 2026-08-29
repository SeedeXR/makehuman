// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include "makehuman/foundation/Transform.h"
#include "makehuman/io/BvhReader.h"
#include "makehuman/rig/Skeleton.h"

#include <expected>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace mh::rig {

using foundation::Mat4;

/// A library of named unit poses, each a full set of per-bone transforms.
///
/// The face library ships as a BVH of 60 frames plus a JSON `framemapping`
/// naming them; frame N is the pose named N. A unit is a single expression
/// component ("LeftBrowDown"), and an expression is a weighted blend of them.
struct PoseUnits {
    std::vector<std::string> names;
    size_t boneCount{};

    /// names.size() * boneCount transforms, unit-major.
    std::vector<Mat4> data;

    [[nodiscard]] size_t unitCount() const noexcept { return names.size(); }

    [[nodiscard]] std::span<const Mat4> unit(size_t i) const {
        return {data.data() + i * boneCount, boneCount};
    }

    [[nodiscard]] std::optional<size_t> indexOf(std::string_view name) const;

    /// Blends units together, weighted.
    ///
    /// **This is order-dependent, deliberately.** Each unit is turned into a
    /// rotation scaled by its weight -- `slerp(identity, q_i, w_i)` -- and the
    /// results are composed by multiplication:
    ///
    ///     quat = q1;  quat = q2 * quat;  quat = q3 * quat;  ...
    ///
    /// Quaternion multiplication does not commute, so feeding the same units in
    /// a different order gives a different face. Measured on the shipped units:
    /// reversing a five-unit blend moves the result by **0.034**. The reference
    /// behaves this way (`animation.py:402-415`) and expression files are
    /// authored against it, so it is replicated rather than "fixed" by
    /// symmetrising.
    ///
    /// Weights are **not** normalised: the blend is additive, so two units at
    /// 1.0 apply both fully rather than averaging to a half of each.
    ///
    /// @return boneCount transforms, or empty if the inputs disagree in length
    ///         or an index is out of range.
    [[nodiscard]] std::vector<Mat4> blend(std::span<const size_t> unitIndices,
                                          std::span<const float> weights) const;
};

enum class PoseUnitsErrorKind { NotFound, Unreadable, Malformed, FrameCountMismatch };

struct PoseUnitsError {
    PoseUnitsErrorKind kind{};
    std::string file;
    std::string detail;

    [[nodiscard]] std::string message() const;
};

/// Reads the `framemapping` name list from a pose-unit JSON.
[[nodiscard]] std::expected<std::vector<std::string>, PoseUnitsError> loadPoseUnitNames(
    const std::filesystem::path& path);

/// Maps a BVH's frames onto @p skeleton's bone order to build the library.
///
/// The BVH and the rig are **not the same joint set** -- the face BVH has 212
/// joints against the rig's 163 bones -- so this walks the rig's bones and
/// takes each one's identically-named BVH joint, leaving identity where the BVH
/// has none. Walking the BVH instead would silently reorder every bone.
[[nodiscard]] std::expected<PoseUnits, PoseUnitsError> makePoseUnits(
    const io::BvhFile& bvh, const Skeleton& skeleton, std::vector<std::string> names);

}  // namespace mh::rig
