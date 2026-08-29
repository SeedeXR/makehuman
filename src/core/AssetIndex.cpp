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

        // The error_code overload only covers *construction*. The range-for
        // form then calls operator++, which throws filesystem_error -- so a
        // single unreadable subdirectory anywhere under a search path used to
        // terminate the process at startup:
        //   "terminating due to uncaught exception ... attempting recursion
        //    into ".../secret": Permission denied"
        // skip_permission_denied handles the common case; the explicit
        // increment(ec) makes every other error stop this root's scan instead
        // of unwinding out of the app.
        ec.clear();
        std::filesystem::recursive_directory_iterator it(
            root, std::filesystem::directory_options::skip_permission_denied, ec);
        if (ec) continue;

        for (const std::filesystem::recursive_directory_iterator last; it != last;
             it.increment(ec)) {
            if (ec) break;

            const auto& entry = *it;
            if (!entry.is_regular_file(ec) || ec) {
                ec.clear();
                continue;
            }
            const auto ext = entry.path().extension();
            if (ext != ".mhclo" && ext != ".proxy" && ext != ".mhmat") continue;

            AssetEntry a = peekAsset(entry.path());

            if (!a.uuid.empty()) {
                if (idx.byUuid_.contains(a.uuid)) {
                    // First wins for *resolution*. The reference lets the last
                    // writer win, which makes resolution depend on directory
                    // iteration order; recording the collision keeps it visible.
                    //
                    // The asset itself is still indexed. Skipping it entirely
                    // (as this did) also removed it from entries(), findByTag()
                    // and allTags(), so a copy-pasted UUID silently deleted an
                    // asset from the browser with nothing to explain it.
                    idx.duplicateUuids_.push_back(a.uuid);
                } else {
                    idx.byUuid_.emplace(a.uuid, idx.entries_.size());
                }
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
