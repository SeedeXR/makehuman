// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Which wrinkle map the renderer shows, out of everything the driver fired.
//
// Owner directive 12.3's second consumer, at the point where it meets the shape
// of what consumes it. The pose-signal evaluator and the RBF produce ONE weight
// per example pose; each pose may name its own wrinkle sheet
// (`ExamplePose::wrinkle`, manifest version 2); and `render::MeshInstance`
// carries exactly ONE map and one weight per mesh.
//
// So a choice is unavoidable, and the useful part of making it is being honest
// about what the choice throws away.
#pragma once

#include "makehuman/core/CorrectiveManifest.h"

#include <cstddef>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>

namespace mh::rig {

/// Below this a weight is not a live map.
///
/// A Gaussian RBF never returns exactly zero -- measured 2.7e-16 at a pose far
/// from every centre -- so "nothing fired" cannot mean "weight == 0". Without a
/// threshold every sheet in a set is always live, which makes the dropped-map
/// report fire on every frame of every multi-sheet character, and a message
/// that always appears is a message nobody reads.
inline constexpr double kWrinkleNegligible = 1e-6;

/// The one map the renderer will show, and what showing it cost.
struct WrinkleChoice {
    /// The chosen sheet, or empty when nothing fired. Resolved, as the manifest
    /// resolves it.
    std::filesystem::path map;
    /// Its strength, summed over every pose naming it and clamped to [0, 1].
    float weight{0.0F};
    /// How many OTHER distinct sheets were live and are not being shown.
    size_t dropped{0};
    /// Their filenames, comma-separated, for a message an author can act on.
    /// "2 maps dropped" is not something anyone can do anything about.
    std::string droppedNames;
};

/// Picks the wrinkle map for one frame.
///
/// @param manifest  the authoring manifest; only pose names and wrinkle paths
///                  are read.
/// @param poseNames the compiled blob's pose names, parallel to @p weights.
/// @param weights   `CorrectiveRuntime::weights()` -- one per pose.
///
/// **Summed per map, not maxed.** One crease sheet keyed at several example
/// poses is how a shoulder is authored, and two neighbouring poses each half
/// active should show that sheet fully on. Taking the maximum would show it
/// half on, which is wrong in the direction nobody notices.
///
/// **Matched by NAME, not by index.** The blob is compiled from the manifest
/// and the cache's hash check is what guarantees the two agree, so an index
/// pairing would be correct today. It would also hand pose 0's weight to pose
/// 0's map whatever the two actually were, so the day that guarantee breaks it
/// shows the wrong sheet at the wrong strength and looks entirely plausible.
/// Mismatched input chooses nothing instead.
[[nodiscard]] WrinkleChoice chooseWrinkle(const core::CorrectiveManifest& manifest,
                                          std::span<const std::string_view> poseNames,
                                          std::span<const double> weights);

}  // namespace mh::rig
