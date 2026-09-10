// SPDX-License-Identifier: AGPL-3.0-or-later
//
// See the header for why the weights are summed rather than maxed, and why the
// pairing is by name.
#include "makehuman/rig/Wrinkles.h"

#include <algorithm>
#include <utility>
#include <vector>

namespace mh::rig {

namespace {

/// The wrinkle path @p name is authored with, or nothing.
const std::filesystem::path* wrinkleOf(const core::CorrectiveManifest& manifest,
                                       std::string_view name) {
    const auto it = std::ranges::find_if(
        manifest.poses, [name](const core::ExamplePose& p) { return p.name == name; });
    if (it == manifest.poses.end() || it->wrinkle.empty()) return nullptr;
    return &it->wrinkle;
}

}  // namespace

WrinkleChoice chooseWrinkle(const core::CorrectiveManifest& manifest,
                            std::span<const std::string_view> poseNames,
                            std::span<const double> weights) {
    // One condition, because to a caller the two are the same event: the inputs
    // do not describe one another, so there is no honest choice to make. See
    // the header on why this is not an assertion -- the cache is what makes it
    // unreachable, and this is what happens if that ever stops being true.
    if (poseNames.size() != weights.size()) return {};

    // Summed per distinct sheet. A vector rather than a map: a corrective set
    // has a handful of sheets, and the linear scan keeps the FIRST-AUTHORED
    // order, which is what makes the dropped list read the way the manifest
    // does instead of in hash order.
    std::vector<std::pair<const std::filesystem::path*, double>> live;
    for (size_t i = 0; i < poseNames.size(); ++i) {
        // Not `abs`: RBF weights overshoot between example poses, so a negative
        // one is ordinary. A negative crease depth has no meaning, and taking
        // its magnitude would invert the sheet.
        if (weights[i] <= kWrinkleNegligible) continue;
        const std::filesystem::path* map = wrinkleOf(manifest, poseNames[i]);
        if (map == nullptr) continue;

        const auto at =
            std::ranges::find_if(live, [map](const auto& e) { return *e.first == *map; });
        if (at == live.end()) {
            live.emplace_back(map, weights[i]);
        } else {
            at->second += weights[i];
        }
    }
    if (live.empty()) return {};

    // The strongest, and on a tie the first authored -- `max_element` returns
    // the first of equals, which is the only stable answer available.
    const auto best = std::ranges::max_element(
        live, [](const auto& a, const auto& b) { return a.second < b.second; });

    WrinkleChoice choice;
    choice.map = *best->first;
    // Clamped: the shader adds `weight * slope` with no upper bound of its own,
    // so an unclamped sum is a sheet creased deeper than anything the author
    // previewed.
    choice.weight = static_cast<float>(std::clamp(best->second, 0.0, 1.0));

    for (const auto& e : live) {
        if (e.first == best->first) continue;
        ++choice.dropped;
        if (!choice.droppedNames.empty()) choice.droppedNames += ", ";
        choice.droppedNames += e.first->filename().string();
    }
    return choice;
}

}  // namespace mh::rig
