// SPDX-License-Identifier: Apache-2.0
#include "makehuman/foundation/DataDir.h"

#include <cstdlib>
#include <vector>

namespace mh::foundation {
namespace {

/// Not just "the directory exists": a directory that exists but holds nothing
/// we need is worse than no candidate at all, because it stops the search and
/// the failure surfaces later, somewhere less obvious.
bool holds(const std::filesystem::path& dir, const std::filesystem::path& sentinel) {
    std::error_code ec;
    return std::filesystem::exists(dir / sentinel, ec);
}

std::filesystem::path homeDirectory() {
    if (const char* home = std::getenv("HOME"); home != nullptr && *home != '\0') {
        return std::filesystem::path(home);
    }
    return {};
}

/// The search, shared by every runtime directory the application needs.
///
/// Written once rather than three times: the order of candidates is the whole
/// policy, and three copies of it would drift the moment one of them gained a
/// location the others did not.
std::filesystem::path resolveDir(const std::filesystem::path& executable,
                                 const std::filesystem::path& compiledDefault, const char* envVar,
                                 const std::filesystem::path& bundleName,
                                 const std::filesystem::path& sentinel) {
    std::vector<std::filesystem::path> candidates;

    if (const char* override = std::getenv(envVar); override != nullptr && *override != '\0') {
        candidates.emplace_back(override);
    }

    const std::filesystem::path binDir = executable.parent_path();
    // MakeHuman.app/Contents/MacOS/makehuman -> MakeHuman.app/Contents/Resources/<name>
    candidates.push_back(binDir.parent_path() / "Resources" / bundleName);
    // <prefix>/bin/makehuman -> <prefix>/share/makehuman/<name>
    candidates.push_back(binDir.parent_path() / "share" / "makehuman" / bundleName);

    if (const std::filesystem::path home = homeDirectory(); !home.empty()) {
        candidates.push_back(home / "Library" / "Application Support" / "MakeHuman" / bundleName);
    }

    candidates.push_back(compiledDefault);

    for (const std::filesystem::path& candidate : candidates) {
        if (holds(candidate, sentinel)) return candidate;
    }
    // Nothing found. Returning the compiled default rather than an empty path
    // gives the caller a concrete thing to name when it reports the failure.
    return compiledDefault;
}

}  // namespace

std::filesystem::path resolveDataDir(const std::filesystem::path& executable,
                                     const std::filesystem::path& compiledDefault) {
    return resolveDir(executable, compiledDefault, "MH_DATA_DIR", "data",
                      std::filesystem::path("3dobjs") / "base.obj");
}

std::filesystem::path resolveShaderDir(const std::filesystem::path& executable,
                                       const std::filesystem::path& compiledDefault) {
    return resolveDir(executable, compiledDefault, "MH_SHADER_DIR", "shaders",
                      "litsphere.vert.qsb");
}

std::filesystem::path resolveResourceDir(const std::filesystem::path& executable,
                                         const std::filesystem::path& compiledDefault) {
    return resolveDir(executable, compiledDefault, "MH_RESOURCE_DIR", "resources",
                      std::filesystem::path("icons") / "lucide" / "save.svg");
}

}  // namespace mh::foundation
