// SPDX-License-Identifier: AGPL-3.0-or-later
#include "makehuman/core/AssetMeta.h"

#include "makehuman/foundation/FileRead.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <sstream>
#include <string>

namespace mh::core {

namespace {

std::string lowered(std::string_view text) {
    std::string out(text);
    std::transform(out.begin(), out.end(), out.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return out;
}

}  // namespace

bool AssetMeta::hasTag(std::string_view tag) const {
    const std::string wanted = lowered(tag);
    return std::find(tags.begin(), tags.end(), wanted) != tags.end();
}

AssetMeta loadAssetMeta(const std::filesystem::path& asset) {
    AssetMeta meta;
    // The stem is the default name, as it is in the reference
    // (`3_libraries_pose.py:114`), so a caller always has something to label a
    // chooser entry with.
    meta.name = asset.stem().string();

    std::filesystem::path sidecar = asset;
    sidecar.replace_extension(".meta");

    auto in = foundation::openForRead(sidecar);
    if (!in) {
        // Absence is the normal case -- most assets ship without a sidecar --
        // so it is not reported. A file that EXISTS and still would not open is
        // a real problem the user can act on (permissions, a directory in its
        // place), and staying silent about it is how a mislabelled chooser
        // entry becomes a mystery.
        if (in.error() != foundation::FileReadErrorKind::NotFound) {
            std::fprintf(stderr, "warning: cannot read %s; using defaults from the filename\n",
                         sidecar.string().c_str());
        }
        return meta;
    }

    std::string line;
    while (std::getline(*in, line)) {
        std::istringstream fields(line);
        std::string key;
        if (!(fields >> key)) continue;  // blank or whitespace-only

        // The value is the REST of the line, whitespace runs collapsed to one
        // space -- the reference splits and rejoins (`' '.join(l[1:])`), so
        // matching it keeps every chooser label identical to the reference's.
        std::string word;
        std::string value;
        while (fields >> word) {
            if (!value.empty()) value += ' ';
            value += word;
        }

        const std::string name = lowered(key);
        if (name == "tag") {
            // A bare `tag` line would otherwise add an empty tag: it matches
            // nothing a caller would ask for, while still inflating the count
            // that tells "this asset is classified" from "it is not".
            if (value.empty()) continue;
            std::string tag = lowered(value);
            if (std::find(meta.tags.begin(), meta.tags.end(), tag) == meta.tags.end()) {
                meta.tags.push_back(std::move(tag));
            }
        } else if (name == "name") {
            // Empty is NOT accepted, where the reference would assign it and
            // blank the display name. Keeping the stem is the behaviour a
            // chooser needs, and CLAUDE.md 3 says to exclude a broken
            // behaviour explicitly rather than port it.
            if (!value.empty()) meta.name = value;
        }
        // Anything else -- including the reference's `description`, `license`,
        // `copyright` and `author` -- is ignored: `.meta` has no schema and
        // authors add fields, so refusing would reject files the reference
        // itself writes.
    }
    return meta;
}

}  // namespace mh::core
