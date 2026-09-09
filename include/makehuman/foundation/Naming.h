// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <cstddef>
#include <expected>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace mh::foundation {

/// Which set of user-facing names is in force (owner directive 12.1).
///
/// **Legacy is the default** so no existing setup breaks; modern is opt-in and
/// reversible — "anyone migrating flips one flag and can flip it back".
enum class NamingProfile { Legacy, Modern };

/// One domain's name table: workspace presets, material slots, asset kinds.
///
/// The DATA is the mapping, not code. A row is
/// `<canonical id> <legacy name> <modern name>`, whitespace-separated, with `#`
/// comments and blank lines ignored. Three columns rather than two files
/// because a canonical id present in one table and absent from the other is a
/// name that stops resolving, and one row per id makes that impossible to
/// write rather than something a test has to notice.
///
/// Names may not contain whitespace; the parser refuses a row that is not
/// exactly three fields rather than guessing where a name ends.
class NameTable {
public:
    struct Row {
        std::string canonical;
        std::string legacy;
        std::string modern;
    };

    void add(Row row);

    [[nodiscard]] size_t size() const noexcept { return rows_.size(); }

    [[nodiscard]] const std::vector<Row>& rows() const noexcept { return rows_; }

    /// canonical id, or nothing. Never falls back; used by the resolver.
    [[nodiscard]] const Row* byCanonical(std::string_view id) const;
    [[nodiscard]] const Row* byLegacy(std::string_view name) const;
    [[nodiscard]] const Row* byModern(std::string_view name) const;

private:
    std::vector<Row> rows_;
    std::unordered_map<std::string, size_t> canonical_;
    std::unordered_map<std::string, size_t> legacy_;
    std::unordered_map<std::string, size_t> modern_;
};

enum class NameErrorKind {
    Unreadable,
    /// Not exactly three whitespace-separated fields.
    Malformed,
    /// A canonical id or a name appears twice, so a lookup has two answers.
    Duplicate,
};

struct NameError {
    NameErrorKind kind{};
    /// 1-based, or 0 when the file could not be opened at all.
    size_t line{};
    std::string detail;

    [[nodiscard]] std::string message() const;
};

[[nodiscard]] std::expected<NameTable, NameError> loadNameTable(const std::filesystem::path& path);

struct Resolved {
    std::string_view canonical;

    /// The name came from the OTHER profile's column. The caller warns —
    /// **once** — so old- and new-named things coexist in one workspace while
    /// the user still learns the new name.
    bool viaFallback{};
};

/// A user-facing name, or a canonical id, to the canonical id.
///
/// Order is the directive's: the active profile's column, then the other,
/// warning on the fallback. A canonical id resolves to itself in every profile
/// and is never a fallback — canonical ids are what save files persist, so
/// reading one back is the normal case, not a migration.
[[nodiscard]] std::optional<Resolved> resolve(const NameTable& table, NamingProfile profile,
                                              std::string_view name);

/// What to SHOW for a canonical id in this profile.
[[nodiscard]] std::optional<std::string_view> displayName(const NameTable& table,
                                                          NamingProfile profile,
                                                          std::string_view canonical);

}  // namespace mh::foundation
