// SPDX-License-Identifier: AGPL-3.0-or-later
#include "makehuman/rig/ReduceSkin.h"

#include <algorithm>
#include <unordered_map>
#include <vector>

namespace mh::rig {

namespace {

/// The target bone a source bone's influence belongs to.
///
/// Walks UP the source hierarchy until it reaches a bone the map knows. The
/// visited set is not paranoia: a malformed skeleton with a parent cycle would
/// otherwise hang here, and this runs on data read from a file.
int32_t ownerOf(int32_t bone, const std::vector<int32_t>& parents,
                const std::unordered_map<int32_t, int32_t>& direct) {
    std::vector<int32_t> seen;
    while (bone >= 0) {
        if (const auto it = direct.find(bone); it != direct.end()) return it->second;
        if (std::ranges::find(seen, bone) != seen.end()) break;
        seen.push_back(bone);
        bone = parents[static_cast<size_t>(bone)];
    }
    return -1;
}

}  // namespace

std::optional<SkinData> reduceSkin(const SkinData& skin, const RetargetMap& toSource) {
    if (skin.jointNames.empty() || skin.influences == 0) return std::nullopt;

    std::unordered_map<std::string, int32_t> indexOf;
    for (size_t i = 0; i < skin.jointNames.size(); ++i) {
        indexOf.emplace(skin.jointNames[i], static_cast<int32_t>(i));
    }

    // The target skeleton, in the SOURCE's order. Ordering by the source keeps
    // a parent before its children, which `jointParents` has to satisfy and
    // which an unordered_map's iteration order would not.
    std::vector<std::string> targetNames;
    std::vector<int32_t> sourceOf;
    std::unordered_map<int32_t, int32_t> direct;  // source index -> target index
    std::vector<std::pair<int32_t, std::string>> ordered;
    for (const auto& [target, source] : toSource.toBone) {
        if (const auto it = indexOf.find(source); it != indexOf.end()) {
            ordered.emplace_back(it->second, target);
        }
    }
    if (ordered.empty()) return std::nullopt;  // the map describes a different rig
    std::ranges::sort(ordered);
    for (const auto& [src, name] : ordered) {
        direct.emplace(src, static_cast<int32_t>(targetNames.size()));
        sourceOf.push_back(src);
        targetNames.push_back(name);
    }

    SkinData out;
    out.jointNames = targetNames;
    out.influences = skin.influences;
    out.jointParents.reserve(targetNames.size());
    out.globalRest.reserve(targetNames.size());
    for (size_t t = 0; t < targetNames.size(); ++t) {
        const int32_t src = sourceOf[t];
        // The parent is the nearest MAPPED ancestor, so the target hierarchy is
        // the source's with the unmapped bones squeezed out -- not a flat list
        // under one root, which is what taking the source's parent directly
        // would give whenever that parent has no counterpart.
        const int32_t up = skin.jointParents[static_cast<size_t>(src)];
        out.jointParents.push_back(ownerOf(up, skin.jointParents, direct));
        // Taken from the SOURCE rather than recomputed: these are the matrices
        // this character is actually bound and posed with, so the reduced
        // skeleton lands exactly where the full one does.
        out.globalRest.push_back(skin.globalRest[static_cast<size_t>(src)]);
    }
    if (!skin.globalPose.empty()) {
        out.globalPose.reserve(targetNames.size());
        for (size_t t = 0; t < targetNames.size(); ++t) {
            out.globalPose.push_back(skin.globalPose[static_cast<size_t>(sourceOf[t])]);
        }
    }

    const size_t perVertex = skin.influences;
    const size_t vertices  = skin.joints.size() / perVertex;
    out.joints.assign(vertices * perVertex, 0);
    out.weights.assign(vertices * perVertex, 0.0F);

    std::vector<std::pair<int32_t, float>> merged;
    for (size_t v = 0; v < vertices; ++v) {
        merged.clear();
        for (size_t k = 0; k < perVertex; ++k) {
            const float w = skin.weights[v * perVertex + k];
            if (w <= 0.0F) continue;
            const auto src       = static_cast<int32_t>(skin.joints[v * perVertex + k]);
            const int32_t target = ownerOf(src, skin.jointParents, direct);
            if (target < 0) continue;
            // SUMMED, not overwritten. Several source bones collapse onto one
            // target -- that is the whole point -- and keeping only the last
            // would throw away most of a vertex's weight.
            const auto hit =
                std::ranges::find_if(merged, [target](const auto& p) { return p.first == target; });
            if (hit != merged.end()) {
                hit->second += w;
            } else {
                merged.emplace_back(target, w);
            }
        }
        // Strongest first, then truncated to the influence budget: dropping an
        // arbitrary subset would move the vertex, dropping the weakest moves it
        // least.
        std::ranges::sort(merged, [](const auto& a, const auto& b) { return a.second > b.second; });
        if (merged.size() > perVertex) merged.resize(perVertex);

        float total = 0.0F;
        for (const auto& [j, w] : merged)
            total += w;
        for (size_t k = 0; k < merged.size(); ++k) {
            out.joints[v * perVertex + k] = static_cast<uint32_t>(merged[k].first);
            // Renormalised, because the truncation above removes weight and a
            // vertex whose weights sum to less than 1 shrinks toward the origin
            // under linear blend skinning.
            out.weights[v * perVertex + k] = total > 0.0F ? merged[k].second / total : 0.0F;
        }
    }
    return out;
}

}  // namespace mh::rig
