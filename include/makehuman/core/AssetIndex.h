// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <cstdint>
#include <filesystem>
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace mh::core {

enum class AssetKind : uint8_t {
    Proxy,     ///< .mhclo / .proxy
    Material,  ///< .mhmat
};

struct AssetEntry {
    std::filesystem::path path;
    std::string uuid;
    std::string name;
    std::set<std::string> tags;  ///< lowercased, as the reference stores them
    AssetKind kind{AssetKind::Proxy};
};

/// Finds assets on disk and resolves them by UUID.
///
/// This exists because **a `.mhm` references proxies by UUID only**. Loading a
/// proxy by filename was deliberately removed upstream
/// (legacy/python/apps/gui/proxychooser.py:550-552, which logs an error and
/// refuses), so without a UUID index a saved character's clothes, hair and eyes
/// cannot resolve at all.
///
/// Metadata is *peeked*, not fully parsed: for a proxy the scan stops at the
/// `verts` line (proxy.py:1023-1036), which is where the megabytes begin.
class AssetIndex {
public:
    /// Scans @p searchPaths in order. **Earlier paths win** a UUID collision,
    /// matching the reference's user-data-over-system-data precedence
    /// (getpath.py:289-308).
    [[nodiscard]] static AssetIndex build(std::span<const std::filesystem::path> searchPaths);

    [[nodiscard]] const AssetEntry* findByUuid(std::string_view uuid) const;

    /// Every asset carrying @p tag (compared lowercased).
    [[nodiscard]] std::vector<const AssetEntry*> findByTag(std::string_view tag) const;

    [[nodiscard]] std::span<const AssetEntry> entries() const noexcept { return entries_; }

    [[nodiscard]] size_t size() const noexcept { return entries_.size(); }

    /// UUIDs seen more than once. The reference warns and lets the last writer
    /// win (proxychooser.py:617-623); we keep the first and report the rest, so
    /// a packaging mistake is visible rather than silently changing behaviour
    /// depending on directory iteration order.
    [[nodiscard]] std::span<const std::string> duplicateUuids() const noexcept {
        return duplicateUuids_;
    }

    /// All distinct tags across the index.
    [[nodiscard]] std::set<std::string> allTags() const;

private:
    std::vector<AssetEntry> entries_;
    std::unordered_map<std::string, size_t> byUuid_;
    std::vector<std::string> duplicateUuids_;
};

/// Reads only the identifying fields of an asset, without parsing its bulk.
[[nodiscard]] AssetEntry peekAsset(const std::filesystem::path& path);

}  // namespace mh::core
