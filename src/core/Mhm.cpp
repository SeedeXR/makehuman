// SPDX-License-Identifier: AGPL-3.0-or-later
#include "makehuman/core/Mhm.h"
#include "makehuman/foundation/Chars.h"

#include "makehuman/core/Modifier.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <fstream>
#include <sstream>

namespace mh::core {
namespace {

/// The format version this writer emits. Major and minor only -- the reference
/// compares with `v(\d)\.(\d)` and ignores the patch.
constexpr std::string_view kFormatVersion = "v1.3.0";

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

bool parseFloat(const std::string& s, double& out) {
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
        if (line.empty() || line[0] == '#') {
            // The one comment worth keeping: without it a round trip would
            // rewrite the header and no longer match byte for byte.
            constexpr std::string_view kHeader = "# Written by MakeHuman ";
            if (line.starts_with(kHeader) && out.writtenBy.empty()) {
                out.writtenBy = line.substr(kHeader.size());
            }
            continue;
        }

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
            // addTag lower-cases, truncates to 25 and stores in a set
            // (human.py:131, reached from the loader at :1524-1526). Skipping
            // that made "Zulu;alpha;MIKE" round-trip as "MIKE;Zulu;alpha"
            // where the reference writes "alpha;mike;zulu".
            for (std::string tag : splitSemicolons(restOfLine(line, "tags"))) {
                std::transform(tag.begin(), tag.end(), tag.begin(),
                               [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
                if (tag.size() > 25) tag.resize(25);
                if (std::find(out.tags.begin(), out.tags.end(), tag) == out.tags.end()) {
                    out.tags.push_back(std::move(tag));
                }
            }
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

std::expected<void, MhmError> saveMhm(const std::filesystem::path& path, const MhmFile& mhm) {
    std::ofstream out(path, std::ios::binary);
    if (!out) {
        return std::unexpected(
            MhmError{MhmErrorKind::Unreadable, path.string(), 0, "cannot open for writing"});
    }
    // Binary mode plus explicit "\n": text mode on some platforms would turn
    // these into CRLF, and the reference writes LF.
    out << "# Written by MakeHuman "
        << (mhm.writtenBy.empty() ? std::string(MH_VERSION_STRING) : mhm.writtenBy) << "\n";
    out << "version " << (mhm.version.empty() ? std::string(kFormatVersion) : mhm.version) << "\n";

    // uuid, name and tags are omitted entirely when empty, not written blank.
    if (!mhm.uuid.empty()) out << "uuid " << mhm.uuid << "\n";
    if (!mhm.name.empty()) out << "name " << mhm.name << "\n";
    if (!mhm.tags.empty()) {
        std::vector<std::string> tags = mhm.tags;
        std::sort(tags.begin(), tags.end());  // the reference sorts before joining
        out << "tags";
        for (size_t i = 0; i < tags.size(); ++i)
            out << (i == 0 ? " " : ";") << tags[i];
        out << "\n";
    }

    if (mhm.hasCamera) {
        out << "camera";
        for (const double v : mhm.camera) {
            std::string text = foundation::formatShortest(v);
            // Python's `'%s' % 12.0` is "12.0"; formatShortest gives "12".
            if (text.find_first_of(".eE") == std::string::npos) text += ".0";
            out << ' ' << text;
        }
        out << "\n";
    }

    for (const auto& [name, value] : mhm.modifiers) {
        out << "modifier " << name << ' ' << foundation::formatFixed(value, 6) << "\n";
    }

    // Plugin lines go after the modifiers, which is where the reference's save
    // handlers run.
    for (const std::string& line : mhm.unhandled)
        out << line << "\n";

    out << "subdivide " << (mhm.subdivide ? "True" : "False") << "\n";

    out.flush();
    if (!out) {
        return std::unexpected(
            MhmError{MhmErrorKind::Unreadable, path.string(), 0, "write failed"});
    }
    return {};
}

MhmFile mhmFromHuman(const Human& human, std::string name) {
    MhmFile out;
    out.version = kFormatVersion;
    out.name    = std::move(name);

    for (const Modifier& m : human.modifiers()) {
        const float value = human.modifierValue(m.fullName);
        // `if modifier.getValue() or modifier.isMacro()` (human.py:1612). A
        // macro at its default still has to be written: the default is 0.5, and
        // omitting it would load back as 0.
        const bool macro = m.kind == ModifierKind::Macro || m.kind == ModifierKind::Ethnic;
        if (value != 0.0F || macro) out.modifiers.emplace_back(m.fullName, value);
    }
    return out;
}

namespace {

/// @return @p v as a float, or @p fallback when it does not survive the
/// narrowing. `static_cast<float>(1e300)` is `inf`, and an `inf` written into a
/// `.mhm` comes out as the unparseable `inf.0`.
float narrowOr(double v, float fallback) {
    if (!std::isfinite(v)) return fallback;
    const float narrowed = static_cast<float>(v);
    return std::isfinite(narrowed) ? narrowed : fallback;
}

}  // namespace

namespace {

/// The reference clamps every pan to [-1, 1] (`lib/camera.py:608-610`).
/// A non-finite value becomes 0: it is not a direction, so there is no edge to
/// clamp it to.
float clampTranslation(float v) noexcept {
    if (!std::isfinite(v)) return 0.0F;
    return std::clamp(v, -1.0F, 1.0F);
}

}  // namespace

std::array<double, 6> mhmCameraFrom(const OrbitView& view) {
    const bool usable     = std::isfinite(view.distance) && view.distance > 0.0F;
    const double distance = static_cast<double>(usable ? view.distance : kDefaultOrbitDistance);
    const double pitch =
        static_cast<double>(narrowOr(static_cast<double>(view.pitchDegrees), 0.0F));
    const double yaw = static_cast<double>(narrowOr(static_cast<double>(view.yawDegrees), 0.0F));
    const auto pan   = [&view](size_t i) {
        return static_cast<double>(clampTranslation(view.translation[i]));
    };
    return {pitch,  yaw,    pan(0),
            pan(1), pan(2), static_cast<double>(kDefaultOrbitDistance) / distance};
}

OrbitView orbitFromMhmCamera(const std::array<double, 6>& camera) {
    OrbitView view;
    view.pitchDegrees = narrowOr(camera[0], 0.0F);
    view.yawDegrees   = narrowOr(camera[1], 0.0F);
    // camera[2..4] is the pan, in fractions of the model's half-extents.
    // narrowOr first, so a value that does not survive the double -> float
    // narrowing becomes 0 rather than clamping to a hard +-1 it never meant.
    for (size_t i = 0; i < 3; ++i) {
        view.translation[i] = clampTranslation(narrowOr(camera[i + 2], 0.0F));
    }

    // A tiny zoom makes the quotient overflow float long before it divides by
    // zero: 1e-38 already yields `inf`, and an infinite orbit distance renders
    // an empty viewport with nothing reported.
    const double zoom = camera[5];
    view.distance =
        zoom > 0.0 && std::isfinite(zoom)
            ? narrowOr(static_cast<double>(kDefaultOrbitDistance) / zoom, kDefaultOrbitDistance)
            : kDefaultOrbitDistance;
    return view;
}

}  // namespace mh::core
