// SPDX-License-Identifier: AGPL-3.0-or-later
#include "makehuman/rig/RetargetMap.h"

#include "makehuman/rig/PoseUnits.h"

#include <algorithm>

#include "makehuman/foundation/FileRead.h"

#include <nlohmann/json.hpp>

#include <fstream>
#include <string>
#include <utility>

namespace mh::rig {

namespace {
using json = nlohmann::json;
}  // namespace

std::expected<RetargetMap, SkeletonError> loadRetargetMap(const std::filesystem::path& path) {
    // openForRead rather than exists()+ifstream, for the reason `loadSkeleton`
    // records: a DIRECTORY satisfies both and then parses as an empty file.
    auto opened = foundation::openForRead(path);
    if (!opened) {
        // NotAFile is Unreadable, not NotFound: something IS there, and saying
        // "not found" about a path that exists sends the reader looking in the
        // wrong place.
        const auto kind = opened.error() == foundation::FileReadErrorKind::NotFound
                              ? SkeletonErrorKind::NotFound
                              : SkeletonErrorKind::Unreadable;
        return std::unexpected(SkeletonError{kind, path.string(), {}});
    }

    json root;
    try {
        // Exceptions on: this is a trust boundary, and a truncated table must
        // fail loudly rather than retarget half a skeleton.
        root = json::parse(*opened);
    } catch (const json::parse_error& e) {
        return std::unexpected(
            SkeletonError{SkeletonErrorKind::Malformed, path.string(), e.what()});
    }
    if (!root.is_object() || !root.contains("mapping") || !root["mapping"].is_object()) {
        return std::unexpected(
            SkeletonError{SkeletonErrorKind::Malformed, path.string(), "no \"mapping\" object"});
    }

    RetargetMap map;
    for (const auto& [source, target] : root["mapping"].items()) {
        if (!target.is_string()) {
            return std::unexpected(SkeletonError{SkeletonErrorKind::Malformed, path.string(),
                                                 "\"" + source + "\" does not name a bone"});
        }
        // An EMPTY target would be accepted by `is_string` and then rename a
        // joint to nothing, which reads downstream as "this rig has no such
        // bone" -- a silent hole rather than a bad table.
        auto name = target.get<std::string>();
        if (name.empty()) {
            return std::unexpected(SkeletonError{SkeletonErrorKind::Malformed, path.string(),
                                                 "\"" + source + "\" maps to an empty name"});
        }
        map.toBone.emplace(source, std::move(name));
    }
    return map;
}

size_t retargetJoints(io::BvhFile& bvh, const RetargetMap& map) {
    size_t renamed = 0;
    for (auto& joint : bvh.joints) {
        if (joint.endSite) continue;
        const auto it = map.toBone.find(joint.name);
        if (it == map.toBone.end()) continue;  // unmapped: left as it was
        joint.name = it->second;
        ++renamed;
    }
    return renamed;
}

RetargetMap invertRetargetMap(const RetargetMap& map, size_t* collisions) {
    RetargetMap out;
    out.toBone.reserve(map.toBone.size());
    size_t dropped = 0;

    for (const auto& [source, bone] : map.toBone) {
        const auto [it, inserted] = out.toBone.emplace(bone, source);
        if (inserted) continue;
        ++dropped;
        // Two sources claim one bone, so one of them cannot survive. WHICH one
        // is chosen deterministically -- the lexicographically smaller name --
        // because `unordered_map` iteration order is unspecified, and picking
        // by arrival would make the same table export different files on
        // different runs.
        if (source < it->second) it->second = source;
    }

    // Reported through `collisions`, not printed here: the caller knows WHICH
    // --rig-names choice produced this and can say so, and a library that
    // printed as well would say it twice -- including into unit-test output.
    if (collisions) *collisions = dropped;
    return out;
}

size_t renameBones(std::vector<std::string>& names, const RetargetMap& map) {
    size_t renamed = 0;
    for (std::string& name : names) {
        const auto it = map.toBone.find(name);
        if (it == map.toBone.end()) continue;  // no word for it: keep ours
        if (it->second == name) continue;      // the same word in both
        name = it->second;
        ++renamed;
    }
    return renamed;
}

std::vector<NamingFit> rankNamings(const std::filesystem::path& bvh, const Skeleton& skeleton,
                                   std::span<const std::pair<std::string, RetargetMap>> candidates,
                                   std::string_view nativeNaming) {
    // The file's own names go in FIRST, so a stable sort leaves them ahead of
    // any table that merely ties them. A pose authored against this rig should
    // never be renamed just because some table scores equally.
    const auto native = bonesDrivenBy(bvh, skeleton, nullptr);
    if (!native) {
        // Unreadable or absent. Ranking nothing is the honest answer: every
        // candidate would score zero, and a caller could not tell "no naming
        // helps" from "there is no file".
        return {};
    }

    std::vector<NamingFit> ranked;
    ranked.reserve(candidates.size() + 1);
    ranked.push_back({std::string(nativeNaming), *native});

    for (const auto& [naming, table] : candidates) {
        const auto driven = bonesDrivenBy(bvh, skeleton, &table);
        if (!driven) return {};  // it read a moment ago; something is wrong
        ranked.push_back({naming, *driven});
    }

    std::stable_sort(ranked.begin(), ranked.end(),
                     [](const NamingFit& a, const NamingFit& b) { return a.driven > b.driven; });
    return ranked;
}

}  // namespace mh::rig
