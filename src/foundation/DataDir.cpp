// SPDX-License-Identifier: Apache-2.0
#include "makehuman/foundation/DataDir.h"

#include <cstdlib>
#include <vector>

namespace mh::foundation {
namespace {

/// Not just "the directory exists": a directory that exists but holds no assets
/// is worse than no candidate at all, because it stops the search and the
/// failure surfaces later, somewhere less obvious.
bool looksLikeAssetTree(const std::filesystem::path& dir) {
    std::error_code ec;
    return std::filesystem::exists(dir / "3dobjs" / "base.obj", ec);
}

std::filesystem::path homeDirectory() {
    if (const char* home = std::getenv("HOME"); home != nullptr && *home != '\0') {
        return std::filesystem::path(home);
    }
    return {};
}

}  // namespace

std::filesystem::path resolveDataDir(const std::filesystem::path& executable,
                                     const std::filesystem::path& compiledDefault) {
    std::vector<std::filesystem::path> candidates;

    if (const char* override = std::getenv("MH_DATA_DIR");
        override != nullptr && *override != '\0') {
        candidates.emplace_back(override);
    }

    const std::filesystem::path binDir = executable.parent_path();
    // MakeHuman.app/Contents/MacOS/makehuman -> MakeHuman.app/Contents/Resources/data
    candidates.push_back(binDir.parent_path() / "Resources" / "data");
    // <prefix>/bin/makehuman -> <prefix>/share/makehuman/data
    candidates.push_back(binDir.parent_path() / "share" / "makehuman" / "data");

    if (const std::filesystem::path home = homeDirectory(); !home.empty()) {
        candidates.push_back(home / "Library" / "Application Support" / "MakeHuman" / "data");
    }

    candidates.push_back(compiledDefault);

    for (const std::filesystem::path& candidate : candidates) {
        if (looksLikeAssetTree(candidate)) return candidate;
    }
    // Nothing found. Returning the compiled default rather than an empty path
    // gives the caller a concrete thing to name when it reports the failure.
    return compiledDefault;
}

}  // namespace mh::foundation
