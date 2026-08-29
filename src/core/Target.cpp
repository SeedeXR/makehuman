// SPDX-License-Identifier: AGPL-3.0-or-later
#include "makehuman/core/Target.h"

#include <algorithm>
#include <charconv>
#include <fstream>
#include <string_view>

namespace mh::core {
namespace {

constexpr std::string_view kWhitespace = " \t\r\n";

size_t tokenize(std::string_view line, std::array<std::string_view, 5>& out) {
    size_t n   = 0;
    size_t pos = 0;
    while (pos < line.size() && n < out.size()) {
        const size_t start = line.find_first_not_of(kWhitespace, pos);
        if (start == std::string_view::npos) break;
        const size_t end = line.find_first_of(kWhitespace, start);
        out[n++]         = line.substr(
            start, (end == std::string_view::npos) ? std::string_view::npos : end - start);
        if (end == std::string_view::npos) break;
        pos = end;
    }
    return n;
}

/// Target files use forms like "-.001" and ".004"; from_chars handles both.
bool parseFloat(std::string_view s, float& out) {
    const auto r = std::from_chars(s.data(), s.data() + s.size(), out);
    return r.ec == std::errc{} && r.ptr == s.data() + s.size();
}

bool parseIndex(std::string_view s, uint32_t& out) {
    const auto r = std::from_chars(s.data(), s.data() + s.size(), out);
    return r.ec == std::errc{} && r.ptr == s.data() + s.size();
}

}  // namespace

std::string TargetError::message() const {
    const char* k = "unknown error";
    switch (kind) {
        case TargetErrorKind::NotFound: k = "file not found"; break;
        case TargetErrorKind::Unreadable: k = "file unreadable"; break;
        case TargetErrorKind::MalformedLine: k = "malformed data line"; break;
    }
    std::string m = file;
    if (line > 0) {
        m += ':';
        m += std::to_string(line);
    }
    m += ": ";
    m += k;
    if (!detail.empty()) {
        m += " (";
        m += detail;
        m += ')';
    }
    return m;
}

std::expected<Target, TargetError> loadTarget(const std::filesystem::path& path,
                                              uint32_t* skippedLines) {
    std::error_code ec;
    if (!std::filesystem::exists(path, ec)) {
        return std::unexpected(TargetError{TargetErrorKind::NotFound, path.string(), 0, {}});
    }
    std::ifstream in(path);
    if (!in) {
        return std::unexpected(TargetError{TargetErrorKind::Unreadable, path.string(), 0, {}});
    }

    Target t;
    t.name           = path.stem().string();
    uint32_t skipped = 0;

    std::string line;
    std::array<std::string_view, 5> tok{};
    uint32_t lineNo = 0;

    while (std::getline(in, line)) {
        ++lineNo;
        if (line.empty() || line[0] == '#') continue;  // comment / licence header

        const size_t n = tokenize(line, tok);
        if (n == 0) continue;
        if (n != 4) {
            // The reference skips these outright (algos3d.py:136-138). Safe,
            // because every entry carries its own index, so nothing shifts.
            ++skipped;
            continue;
        }

        uint32_t idx{};
        Vec3 d{};
        if (!parseIndex(tok[0], idx) || !parseFloat(tok[1], d.x) || !parseFloat(tok[2], d.y) ||
            !parseFloat(tok[3], d.z)) {
            return std::unexpected(TargetError{TargetErrorKind::MalformedLine, path.string(),
                                               lineNo, "expected 'index dx dy dz'"});
        }

        t.verts.push_back(idx);
        t.offsets.push_back(d);
        t.maxVertexIndex = std::max(t.maxVertexIndex, idx);
    }

    if (skippedLines != nullptr) *skippedLines = skipped;
    return t;
}

uint32_t applyTarget(const Target& target, Mesh& mesh, float factor, Vec3 scale) {
    if (factor == 0.0F || target.empty()) return 0;

    const Vec3 s{scale.x * factor, scale.y * factor, scale.z * factor};
    auto coord        = mesh.mutableCoord();
    const auto nVerts = static_cast<uint32_t>(coord.size());
    uint32_t applied  = 0;

    for (size_t i = 0; i < target.verts.size(); ++i) {
        const uint32_t v = target.verts[i];
        if (v >= nVerts) continue;  // the reference would read out of bounds here
        const Vec3& d = target.offsets[i];
        coord[v].x += d.x * s.x;
        coord[v].y += d.y * s.y;
        coord[v].z += d.z * s.z;
        ++applied;
    }
    return applied;
}

std::expected<const Target*, TargetError> TargetLibrary::get(const std::string& relativePath) {
    if (const auto it = cache_.find(relativePath); it != cache_.end()) {
        return &it->second;
    }
    auto loaded = loadTarget(root_ / relativePath);
    if (!loaded) return std::unexpected(loaded.error());

    const auto [it, _] = cache_.emplace(relativePath, std::move(*loaded));
    return &it->second;
}

}  // namespace mh::core
