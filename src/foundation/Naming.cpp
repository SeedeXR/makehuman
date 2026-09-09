// SPDX-License-Identifier: Apache-2.0
#include "makehuman/foundation/Naming.h"

#include "makehuman/foundation/FileRead.h"

#include <sstream>
#include <string>
#include <vector>

namespace mh::foundation {
namespace {

const NameTable::Row* lookup(const std::vector<NameTable::Row>& rows,
                             const std::unordered_map<std::string, size_t>& index,
                             std::string_view key) {
    const auto it = index.find(std::string(key));
    return it == index.end() ? nullptr : &rows[it->second];
}

}  // namespace

void NameTable::add(Row row) {
    const size_t at = rows_.size();
    canonical_.emplace(row.canonical, at);
    legacy_.emplace(row.legacy, at);
    modern_.emplace(row.modern, at);
    rows_.push_back(std::move(row));
}

const NameTable::Row* NameTable::byCanonical(std::string_view id) const {
    return lookup(rows_, canonical_, id);
}

const NameTable::Row* NameTable::byLegacy(std::string_view name) const {
    return lookup(rows_, legacy_, name);
}

const NameTable::Row* NameTable::byModern(std::string_view name) const {
    return lookup(rows_, modern_, name);
}

std::string NameError::message() const {
    std::string where = line == 0 ? std::string{} : (" at line " + std::to_string(line));
    switch (kind) {
        case NameErrorKind::Unreadable: return "cannot read the name table: " + detail;
        case NameErrorKind::Malformed:
            return "name table" + where +
                   ": expected '<canonical> <legacy> <modern>', got: " + detail;
        case NameErrorKind::Duplicate:
            return "name table" + where + ": '" + detail +
                   "' already names something else, so a lookup has two answers";
    }
    return "unknown name table error";
}

std::expected<NameTable, NameError> loadNameTable(const std::filesystem::path& path) {
    // `openForRead`, not a bare ifstream: a directory opens successfully and
    // then parses into a valid, EMPTY table -- the failure mode that helper
    // exists to refuse.
    auto in = openForRead(path);
    if (!in) {
        return std::unexpected(
            NameError{.kind = NameErrorKind::Unreadable, .line = 0, .detail = path.string()});
    }

    NameTable table;
    // Every name and id already seen, so a collision between ANY two of them is
    // caught -- including a display name equal to another row's canonical id,
    // which would otherwise resolve two ways.
    std::unordered_map<std::string, bool> seen;

    std::string line;
    size_t number = 0;
    while (std::getline(*in, line)) {
        ++number;
        if (const size_t hash = line.find('#'); hash != std::string::npos) line.resize(hash);

        std::istringstream fields(line);
        std::string canonical;
        std::string legacy;
        std::string modern;
        std::string extra;
        if (!(fields >> canonical)) continue;  // blank or comment-only
        if (!(fields >> legacy) || !(fields >> modern) || (fields >> extra)) {
            return std::unexpected(
                NameError{.kind = NameErrorKind::Malformed, .line = number, .detail = line});
        }

        // Within ONE row the three keys may coincide -- most names do not
        // change between profiles, so `Modelling Modelling` is the common case
        // and only `Materials`/`Assets` differs. What must not happen is the
        // same key naming two DIFFERENT rows, which is a lookup with two
        // answers. So dedupe the row's own keys, then check across rows.
        std::vector<std::string> keys{canonical};
        if (legacy != canonical) keys.push_back(legacy);
        if (modern != canonical && modern != legacy) keys.push_back(modern);
        for (const std::string& key : keys) {
            if (!seen.emplace(key, true).second) {
                return std::unexpected(
                    NameError{.kind = NameErrorKind::Duplicate, .line = number, .detail = key});
            }
        }
        table.add({.canonical = canonical, .legacy = legacy, .modern = modern});
    }
    return table;
}

std::optional<Resolved> resolve(const NameTable& table, NamingProfile profile,
                                std::string_view name) {
    // Canonical FIRST, and never a fallback: save files persist canonical ids,
    // so reading one back is the normal case rather than a migration.
    if (const NameTable::Row* row = table.byCanonical(name); row != nullptr) {
        return Resolved{.canonical = row->canonical, .viaFallback = false};
    }

    const bool legacyFirst     = profile == NamingProfile::Legacy;
    const NameTable::Row* mine = legacyFirst ? table.byLegacy(name) : table.byModern(name);
    if (mine != nullptr) return Resolved{.canonical = mine->canonical, .viaFallback = false};

    const NameTable::Row* other = legacyFirst ? table.byModern(name) : table.byLegacy(name);
    if (other == nullptr) return std::nullopt;
    return Resolved{.canonical = other->canonical, .viaFallback = true};
}

std::optional<std::string_view> displayName(const NameTable& table, NamingProfile profile,
                                            std::string_view canonical) {
    const NameTable::Row* row = table.byCanonical(canonical);
    if (row == nullptr) return std::nullopt;
    return profile == NamingProfile::Legacy ? std::string_view{row->legacy}
                                            : std::string_view{row->modern};
}

}  // namespace mh::foundation
