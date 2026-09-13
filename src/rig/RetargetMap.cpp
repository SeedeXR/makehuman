// SPDX-License-Identifier: AGPL-3.0-or-later
#include "makehuman/rig/RetargetMap.h"

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

}  // namespace mh::rig
