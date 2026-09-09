// SPDX-License-Identifier: AGPL-3.0-or-later
//
// See the header for the three layers of directive 12.4 and why this is JSON.
#include "makehuman/core/CorrectiveManifest.h"

#include "makehuman/foundation/FileRead.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <bit>
#include <optional>
#include <string_view>

namespace mh::core {

namespace {

using json = nlohmann::json;

/// FNV-1a, the hash this project already uses for topology.
void hashBytes(uint64_t& h, std::string_view s) {
    for (const char c : s) {
        h ^= static_cast<uint64_t>(static_cast<unsigned char>(c));
        h *= 1099511628211ULL;
    }
}

void hashDouble(uint64_t& h, double v) {
    // The bytes of the VALUE, not of its printed form, so `0.9` and `0.90` hash
    // the same while 0.9 and 0.8 do not -- reformatting a number in a manifest
    // must not throw away a compiled cache.
    auto bits = std::bit_cast<uint64_t>(v);
    for (int i = 0; i < 8; ++i) {
        h ^= bits & 0xFFULL;
        h *= 1099511628211ULL;
        bits >>= 8;
    }
}

/// 0-15, or -1 if @p c is not a hex digit. Local because it has exactly one
/// caller; `foundation::Chars` has no hex parser and adding one to a shared
/// header for a single use is a wider change than the problem.
int hexDigit(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

std::unexpected<CorrectiveManifestError> fail(CorrectiveManifestErrorKind kind,
                                              const std::filesystem::path& path,
                                              std::string detail = {}) {
    return std::unexpected(CorrectiveManifestError{kind, path.string(), std::move(detail)});
}

/// Sixteen hex digits, either case, and nothing else.
///
/// Strict because the alternative is worse than useless: a hash that parses
/// loosely can silently equal a different topology's, and the guard exists
/// precisely to be trusted.
std::optional<uint64_t> parseTopologyHash(std::string_view s) {
    if (s.size() != 16) return std::nullopt;
    uint64_t v = 0;
    for (const char c : s) {
        const int d = hexDigit(c);
        if (d < 0) return std::nullopt;
        v = (v << 4) | static_cast<uint64_t>(d);
    }
    return v;
}

std::optional<DriverComponent> parseComponent(std::string_view s) {
    if (s == "swing") return DriverComponent::Swing;
    if (s == "twist") return DriverComponent::Twist;
    return std::nullopt;
}

/// True if @p rel is a relative path that stays inside the manifest's own
/// directory.
///
/// A manifest is data, which is a trust boundary this project guards elsewhere
/// too. Reading through `..` or an absolute path would let a shipped asset name
/// any file on the machine.
bool payloadPathIsContained(const std::filesystem::path& rel) {
    if (rel.empty() || rel.is_absolute()) return false;
    return std::ranges::none_of(rel,
                                [](const std::filesystem::path& part) { return part == ".."; });
}

}  // namespace

std::string CorrectiveManifestError::message() const {
    std::string s;
    switch (kind) {
        case CorrectiveManifestErrorKind::NotFound: s = "no such manifest"; break;
        case CorrectiveManifestErrorKind::Unreadable: s = "manifest cannot be read"; break;
        case CorrectiveManifestErrorKind::Malformed: s = "manifest is malformed"; break;
        case CorrectiveManifestErrorKind::UnsupportedVersion:
            s = "manifest formatVersion is missing or not supported";
            break;
        case CorrectiveManifestErrorKind::UnknownKernel: s = "unknown kernel"; break;
        case CorrectiveManifestErrorKind::BadRadius: s = "radius must be greater than zero"; break;
        case CorrectiveManifestErrorKind::NoDrivers:
            s = "no drivers, so no signal to key on";
            break;
        case CorrectiveManifestErrorKind::UnknownComponent:
            s = "component must be \"swing\" or \"twist\"";
            break;
        case CorrectiveManifestErrorKind::DuplicateDriver: s = "the same driver twice"; break;
        case CorrectiveManifestErrorKind::NoPoses: s = "no example poses"; break;
        case CorrectiveManifestErrorKind::SignalDimension:
            s = "a pose's signal is not the drivers' dimension";
            break;
        case CorrectiveManifestErrorKind::DuplicatePose: s = "two poses are the same"; break;
        case CorrectiveManifestErrorKind::BadPayloadPath:
            s = "delta path must be relative and stay inside the manifest's directory";
            break;
    }
    if (!file.empty()) s += " (" + file + ")";
    if (!detail.empty()) s += ": " + detail;
    return s;
}

size_t CorrectiveManifest::dimension() const noexcept {
    size_t n = 0;
    for (const Driver& d : drivers)
        n += componentDimension(d.component);
    return n;
}

std::expected<CorrectiveManifest, CorrectiveManifestError> loadCorrectiveManifest(
    const std::filesystem::path& path) {
    // openForRead, not exists()+ifstream: a DIRECTORY satisfies both and then
    // parses as an empty file. See foundation/FileRead.h.
    auto opened = foundation::openForRead(path);
    if (!opened) {
        return fail(opened.error() == foundation::FileReadErrorKind::NotFound
                        ? CorrectiveManifestErrorKind::NotFound
                        : CorrectiveManifestErrorKind::Unreadable,
                    path);
    }

    json doc;
    try {
        doc = json::parse(*opened);
    } catch (const json::exception& e) {
        return fail(CorrectiveManifestErrorKind::Malformed, path, e.what());
    }
    if (!doc.is_object())
        return fail(CorrectiveManifestErrorKind::Malformed, path, "top level is not an object");

    CorrectiveManifest m;

    const auto version = doc.find("formatVersion");
    if (version == doc.end() || !version->is_number_unsigned() ||
        version->get<uint32_t>() != kCorrectiveManifestVersion) {
        return fail(CorrectiveManifestErrorKind::UnsupportedVersion, path,
                    "this build reads version " + std::to_string(kCorrectiveManifestVersion));
    }
    m.formatVersion = version->get<uint32_t>();

    const auto hashField = doc.find("topologyHash");
    if (hashField == doc.end() || !hashField->is_string()) {
        return fail(CorrectiveManifestErrorKind::Malformed, path,
                    "topologyHash is required, as sixteen hex digits");
    }
    const auto topology = parseTopologyHash(hashField->get<std::string>());
    if (!topology) {
        return fail(CorrectiveManifestErrorKind::Malformed, path,
                    "topologyHash is not sixteen hex digits");
    }
    m.topologyHash = *topology;

    const auto kernel = doc.find("kernel");
    if (kernel == doc.end() || !kernel->is_string()) {
        return fail(CorrectiveManifestErrorKind::UnknownKernel, path, "kernel is required");
    }
    if (kernel->get<std::string>() != "gaussian") {
        // Refused rather than defaulted to gaussian: a thin-plate kernel is a
        // different shape, and producing gaussian output for a manifest that
        // asked for thin-plate is the kind of wrong that looks plausible.
        return fail(CorrectiveManifestErrorKind::UnknownKernel, path,
                    "only \"gaussian\" is implemented, got \"" + kernel->get<std::string>() + "\"");
    }

    const auto radius = doc.find("radius");
    if (radius == doc.end() || !radius->is_number()) {
        return fail(CorrectiveManifestErrorKind::BadRadius, path, "radius is required");
    }
    m.radius = radius->get<double>();
    if (!(m.radius > 0.0)) {
        return fail(CorrectiveManifestErrorKind::BadRadius, path,
                    "got " + std::to_string(m.radius));
    }

    const auto drivers = doc.find("drivers");
    if (drivers == doc.end() || !drivers->is_array() || drivers->empty()) {
        return fail(CorrectiveManifestErrorKind::NoDrivers, path);
    }
    for (const auto& d : *drivers) {
        if (!d.is_object())
            return fail(CorrectiveManifestErrorKind::Malformed, path, "a driver is not an object");
        const auto joint     = d.find("joint");
        const auto component = d.find("component");
        if (joint == d.end() || !joint->is_string() || joint->get<std::string>().empty()) {
            return fail(CorrectiveManifestErrorKind::Malformed, path, "a driver has no joint");
        }
        if (component == d.end() || !component->is_string()) {
            return fail(CorrectiveManifestErrorKind::UnknownComponent, path,
                        joint->get<std::string>());
        }
        const auto parsed = parseComponent(component->get<std::string>());
        if (!parsed) {
            return fail(CorrectiveManifestErrorKind::UnknownComponent, path,
                        joint->get<std::string>() + ": \"" + component->get<std::string>() + "\"");
        }
        const Driver next{.joint = joint->get<std::string>(), .component = *parsed};
        if (std::ranges::find(m.drivers, next) != m.drivers.end()) {
            return fail(CorrectiveManifestErrorKind::DuplicateDriver, path, next.joint);
        }
        m.drivers.push_back(next);
    }

    const size_t dimension = m.dimension();

    const auto poses = doc.find("poses");
    if (poses == doc.end() || !poses->is_array() || poses->empty()) {
        return fail(CorrectiveManifestErrorKind::NoPoses, path);
    }
    for (const auto& p : *poses) {
        if (!p.is_object())
            return fail(CorrectiveManifestErrorKind::Malformed, path, "a pose is not an object");
        const auto name = p.find("name");
        if (name == p.end() || !name->is_string() || name->get<std::string>().empty()) {
            return fail(CorrectiveManifestErrorKind::Malformed, path, "a pose has no name");
        }
        ExamplePose pose;
        pose.name = name->get<std::string>();

        const auto signal = p.find("signal");
        if (signal == p.end() || !signal->is_array()) {
            return fail(CorrectiveManifestErrorKind::SignalDimension, path,
                        pose.name + " has no signal");
        }
        if (signal->size() != dimension) {
            return fail(CorrectiveManifestErrorKind::SignalDimension, path,
                        pose.name + " has " + std::to_string(signal->size()) +
                            " values, drivers "
                            "need " +
                            std::to_string(dimension));
        }
        for (const auto& v : *signal) {
            if (!v.is_number()) {
                return fail(CorrectiveManifestErrorKind::SignalDimension, path,
                            pose.name + " has a non-numeric signal value");
            }
            pose.signal.push_back(v.get<double>());
        }

        const auto delta = p.find("delta");
        if (delta == p.end() || !delta->is_string()) {
            return fail(CorrectiveManifestErrorKind::BadPayloadPath, path,
                        pose.name + " has no delta");
        }
        const std::filesystem::path rel = delta->get<std::string>();
        if (!payloadPathIsContained(rel)) {
            return fail(CorrectiveManifestErrorKind::BadPayloadPath, path,
                        pose.name + ": \"" + delta->get<std::string>() + "\"");
        }
        pose.delta = path.parent_path() / rel;

        for (const ExamplePose& seen : m.poses) {
            if (seen.name == pose.name) {
                return fail(CorrectiveManifestErrorKind::DuplicatePose, path,
                            "two poses named " + pose.name);
            }
            // Two poses at the same point make the interpolation matrix
            // singular. rbfSolve would refuse it as NotSolvable, which is true
            // and tells the author nothing; here both names can be named.
            if (seen.signal == pose.signal) {
                return fail(CorrectiveManifestErrorKind::DuplicatePose, path,
                            seen.name + " and " + pose.name + " are at the same signal");
            }
        }
        m.poses.push_back(std::move(pose));
    }

    // The cache key. Over the CONTENT in a canonical order, so whitespace, key
    // order and the file's location are not part of it -- reformatting a
    // manifest or moving a checkout must not throw away a compiled blob. The
    // delta path is hashed as written rather than as resolved, for the same
    // reason.
    uint64_t h = 14695981039346656037ULL;
    hashBytes(h, std::to_string(m.formatVersion));
    hashBytes(h, hashField->get<std::string>());
    hashDouble(h, m.radius);
    for (const Driver& d : m.drivers) {
        hashBytes(h, d.joint);
        hashBytes(h, d.component == DriverComponent::Swing ? "swing" : "twist");
    }
    for (size_t i = 0; i < m.poses.size(); ++i) {
        hashBytes(h, m.poses[i].name);
        for (const double v : m.poses[i].signal)
            hashDouble(h, v);
        hashBytes(h, (*poses)[i].at("delta").get<std::string>());
    }
    m.hash = h;

    return m;
}

}  // namespace mh::core
