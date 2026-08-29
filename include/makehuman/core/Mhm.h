// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <array>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <string>
#include <utility>
#include <vector>

namespace mh::core {

class Human;

/// A parsed `.mhm` saved model.
///
/// Line-oriented UTF-8, `#` comments, whitespace-split
/// (legacy-python/apps/human.py:1459-1643). The keys below are the ones the
/// core writes; plugin-contributed lines (skeleton, pose, proxies, materials)
/// are preserved verbatim in @ref unhandled so a round trip does not lose them,
/// and so a later milestone can consume them without a format change.
struct MhmFile {
    std::string version;  ///< e.g. "v1.3.0" -- major.minor only, no patch
    std::string uuid;
    std::string name;
    std::vector<std::string> tags;

    /// rotX rotY transX transY transZ zoom
    std::array<float, 6> camera{};
    bool hasCamera{false};

    /// Modifier full name -> value, in file order.
    std::vector<std::pair<std::string, float>> modifiers;

    bool subdivide{false};

    /// Lines this parser does not interpret, kept verbatim.
    std::vector<std::string> unhandled;

    /// Compares only the major and minor version, as the reference does with
    /// the regex `v(\d)\.(\d)` (human.py:1461-1466).
    [[nodiscard]] bool versionMatches(std::string_view other) const;
};

enum class MhmErrorKind { NotFound, Unreadable, Malformed };

struct MhmError {
    MhmErrorKind kind{};
    std::string file;
    uint32_t line{};
    std::string detail;

    [[nodiscard]] std::string message() const;
};

[[nodiscard]] std::expected<MhmFile, MhmError> loadMhm(const std::filesystem::path& path);

/// Applies every modifier line to @p human, in file order.
///
/// @param unknown optionally receives the count of modifier names the human
///        does not have. The reference warns and continues unless `strict`
///        (human.py:1543-1547); the same tolerance is kept, since a model saved
///        with community plugins names modifiers this build has never heard of.
/// @return the number of modifiers actually applied.
uint32_t applyMhm(const MhmFile& mhm, Human& human, uint32_t* unknown = nullptr);

}  // namespace mh::core
