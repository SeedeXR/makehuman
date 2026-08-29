// SPDX-License-Identifier: AGPL-3.0-or-later
#include "makehuman/core/AssetIndex.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <sstream>

namespace mh::core {
namespace {

std::string toLower(std::string s) {
    std::ranges::transform(s, s.begin(),
                           [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

std::vector<std::string> splitWs(const std::string& line) {
    std::vector<std::string> out;
    std::istringstream ss(line);
    std::string tok;
    while (ss >> tok)
        out.push_back(tok);
    return out;
}

std::string joinFrom(const std::vector<std::string>& tok, size_t first) {
    std::string out;
    for (size_t i = first; i < tok.size(); ++i) {
        if (i != first) out.push_back(' ');
        out += tok[i];
    }
    return out;
}

bool isProxyExtension(const std::filesystem::path& p) {
    const auto e = p.extension();
    return e == ".mhclo" || e == ".proxy";
}

}  // namespace

AssetEntry peekAsset(const std::filesystem::path& path) {
    AssetEntry e;
    e.path = path;
    e.kind = isProxyExtension(path) ? AssetKind::Proxy : AssetKind::Material;
    e.name = path.stem().string();

    std::ifstream in(path);
    if (!in) return e;

    std::string line;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty() || line[0] == '#') continue;

        const auto tok = splitWs(line);
        if (tok.empty()) continue;

        if (tok[0] == "uuid" && tok.size() > 1) {
            e.uuid = tok[1];
        } else if (tok[0] == "tag" && tok.size() > 1) {
            e.tags.insert(toLower(joinFrom(tok, 1)));
        } else if (tok[0] == "name" && tok.size() > 1) {
            e.name = joinFrom(tok, 1);
        } else if (tok[0] == "verts") {
            // Everything past here is vertex data -- megabytes of it. The
            // reference stops at exactly this line too (proxy.py:1035-1036).
            break;
        }
    }
    return e;
}

AssetIndex AssetIndex::build(std::span<const std::filesystem::path> searchPaths) {
    AssetIndex idx;

    for (const auto& root : searchPaths) {
        std::error_code ec;
        if (!std::filesystem::exists(root, ec)) continue;

        for (const auto& entry : std::filesystem::recursive_directory_iterator(root, ec)) {
            if (ec) break;
            if (!entry.is_regular_file()) continue;
            const auto ext = entry.path().extension();
            if (ext != ".mhclo" && ext != ".proxy" && ext != ".mhmat") continue;

            AssetEntry a = peekAsset(entry.path());

            if (!a.uuid.empty()) {
                if (const auto it = idx.byUuid_.find(a.uuid); it != idx.byUuid_.end()) {
                    // First wins. The reference lets the last writer win, which
                    // makes resolution depend on directory iteration order;
                    // recording the collision instead keeps it visible.
                    idx.duplicateUuids_.push_back(a.uuid);
                    continue;
                }
                idx.byUuid_.emplace(a.uuid, idx.entries_.size());
            }
            idx.entries_.push_back(std::move(a));
        }
    }

    std::ranges::sort(idx.duplicateUuids_);
    idx.duplicateUuids_.erase(std::ranges::unique(idx.duplicateUuids_).begin(),
                              idx.duplicateUuids_.end());
    return idx;
}

const AssetEntry* AssetIndex::findByUuid(std::string_view uuid) const {
    const auto it = byUuid_.find(std::string{uuid});
    if (it == byUuid_.end()) return nullptr;
    return &entries_[it->second];
}

std::vector<const AssetEntry*> AssetIndex::findByTag(std::string_view tag) const {
    const std::string needle = toLower(std::string{tag});
    std::vector<const AssetEntry*> out;
    for (const AssetEntry& e : entries_) {
        if (e.tags.contains(needle)) out.push_back(&e);
    }
    return out;
}

std::set<std::string> AssetIndex::allTags() const {
    std::set<std::string> out;
    for (const AssetEntry& e : entries_)
        out.insert(e.tags.begin(), e.tags.end());
    return out;
}

}  // namespace mh::core
