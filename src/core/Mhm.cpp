// SPDX-License-Identifier: AGPL-3.0-or-later
#include "makehuman/core/Mhm.h"
#include "makehuman/foundation/Chars.h"

#include "makehuman/core/Modifier.h"

#include <fstream>
#include <sstream>

namespace mh::core {
namespace {

std::vector<std::string> splitWs(const std::string& line) {
    std::vector<std::string> out;
    std::istringstream ss(line);
    std::string tok;
    while (ss >> tok)
        out.push_back(tok);
    return out;
}

/// The rest of the line after the key, preserved with internal spacing --
/// `name` and `tags` take free text (human.py:1602-1605).
std::string restOfLine(const std::string& line, std::string_view key) {
    const size_t pos = line.find(key);
    if (pos == std::string::npos) return {};
    size_t start = pos + key.size();
    while (start < line.size() && (line[start] == ' ' || line[start] == '\t'))
        ++start;
    std::string out = line.substr(start);
    while (!out.empty() && (out.back() == '\r' || out.back() == '\n' || out.back() == ' ')) {
        out.pop_back();
    }
    return out;
}

std::vector<std::string> splitSemicolons(const std::string& s) {
    std::vector<std::string> out;
    std::string cur;
    for (const char c : s) {
        if (c == ';') {
            if (!cur.empty()) out.push_back(cur);
            cur.clear();
        } else {
            cur.push_back(c);
        }
    }
    if (!cur.empty()) out.push_back(cur);
    return out;
}

bool parseFloat(const std::string& s, float& out) {
    return foundation::parseFloat(s, out);
}

/// Extracts the leading "vN.N" of a version string.
std::pair<int, int> majorMinor(std::string_view v) {
    if (v.size() < 4 || v[0] != 'v') return {-1, -1};
    const size_t dot = v.find('.', 1);
    if (dot == std::string_view::npos) return {-1, -1};
    int major = 0;
    int minor = 0;
    if (!foundation::parseInteger(v.substr(1, dot - 1), major)) return {-1, -1};

    // "v1.3.0" carries a patch component that this comparison ignores, so take
    // only the digit run after the first dot. The previous code got this from
    // std::from_chars stopping at the second '.', which made the intent an
    // accident of the API rather than something stated -- and it broke the
    // moment parsing moved behind a helper that demands full consumption.
    const size_t mStart = dot + 1;
    size_t mEnd         = mStart;
    while (mEnd < v.size() && v[mEnd] >= '0' && v[mEnd] <= '9')
        ++mEnd;
    if (!foundation::parseInteger(v.substr(mStart, mEnd - mStart), minor)) return {-1, -1};
    return {major, minor};
}

}  // namespace

bool MhmFile::versionMatches(std::string_view other) const {
    const auto a = majorMinor(version);
    const auto b = majorMinor(other);
    return a.first >= 0 && a == b;
}

std::string MhmError::message() const {
    const char* k = "unknown error";
    switch (kind) {
        case MhmErrorKind::NotFound: k = "file not found"; break;
        case MhmErrorKind::Unreadable: k = "file unreadable"; break;
        case MhmErrorKind::Malformed: k = "malformed line"; break;
    }
    std::string m = file;
    if (line > 0) m += ':' + std::to_string(line);
    m += ": ";
    m += k;
    if (!detail.empty()) m += " (" + detail + ")";
    return m;
}

std::expected<MhmFile, MhmError> loadMhm(const std::filesystem::path& path) {
    std::error_code ec;
    if (!std::filesystem::exists(path, ec)) {
        return std::unexpected(MhmError{MhmErrorKind::NotFound, path.string(), 0, {}});
    }
    std::ifstream in(path);
    if (!in) {
        return std::unexpected(MhmError{MhmErrorKind::Unreadable, path.string(), 0, {}});
    }

    MhmFile out;
    std::string line;
    uint32_t lineNo = 0;

    while (std::getline(in, line)) {
        ++lineNo;
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty() || line[0] == '#') continue;

        const auto tok = splitWs(line);
        if (tok.empty()) continue;
        const std::string& key = tok[0];

        if (key == "version" && tok.size() > 1) {
            out.version = tok[1];
        } else if (key == "uuid" && tok.size() > 1) {
            out.uuid = tok[1];
        } else if (key == "name") {
            out.name = restOfLine(line, "name");
        } else if (key == "tags") {
            out.tags = splitSemicolons(restOfLine(line, "tags"));
        } else if (key == "camera" && tok.size() >= 7) {
            bool ok = true;
            for (size_t i = 0; i < 6; ++i)
                ok = ok && parseFloat(tok[i + 1], out.camera[i]);
            out.hasCamera = ok;
        } else if (key == "modifier" && tok.size() >= 3) {
            float v{};
            if (!parseFloat(tok[2], v)) {
                return std::unexpected(MhmError{MhmErrorKind::Malformed, path.string(), lineNo,
                                                "modifier value '" + tok[2] + "'"});
            }
            out.modifiers.emplace_back(tok[1], v);
        } else if (key == "subdivide" && tok.size() > 1) {
            out.subdivide = (tok[1] == "True" || tok[1] == "true");
        } else {
            // skeleton, pose, expression, proxies, materials, plugin lines --
            // kept verbatim so a round trip loses nothing.
            out.unhandled.push_back(line);
        }
    }
    return out;
}

uint32_t applyMhm(const MhmFile& mhm, Human& human, uint32_t* unknown) {
    uint32_t applied  = 0;
    uint32_t notKnown = 0;
    for (const auto& [name, value] : mhm.modifiers) {
        if (human.setModifierValue(name, value)) {
            ++applied;
        } else {
            ++notKnown;
        }
    }
    if (unknown != nullptr) *unknown = notKnown;
    return applied;
}

}  // namespace mh::core
