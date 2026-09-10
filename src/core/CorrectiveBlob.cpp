// SPDX-License-Identifier: AGPL-3.0-or-later
//
// See the header for what the RBF is solved for and why the blob is a cache
// rather than an interchange format.
//
// LAYOUT. A fixed 64-byte header, then sections whose offsets are COMPUTED from
// the header's counts rather than stored. Computing them means there are no
// offset fields to validate -- a stored offset pointing outside the buffer is
// the classic way a blob reader is exploited -- and the reader recomputes what
// the writer computed, which a round trip over a real compile checks directly.
//
//   0   header, 64 bytes (see kHeader* below)
//   64  centres   poseCount * dimension doubles
//       weights   poseCount * poseCount  doubles
//       deltaSpan poseCount * 2 uint32   (first vertex, count)
//       verts     totalVerts uint32
//       offsets   totalVerts Vec3        (12 bytes each)
//       names     driverCount + poseCount NUL-terminated strings
//       components driverCount bytes
//
// Every section that is read as a wider type starts on a multiple of its own
// alignment, so it can be read in place. The name and component tables go last
// because they are the only variable-width, byte-aligned parts.
#include "makehuman/core/CorrectiveBlob.h"

#include "makehuman/core/Target.h"

#include <algorithm>
#include <cstring>

namespace mh::core {

namespace {

constexpr size_t kHeaderBytes = 64;
constexpr char kMagic[8]      = {'M', 'H', 'C', 'O', 'R', 'R', 'V', '\0'};

/// Where each section starts, and how long the whole thing is.
///
/// One function so the writer and the reader cannot disagree: they call this
/// with the same counts and get the same answer, and the round-trip test is
/// what proves it.
///
/// No padding is inserted and none is needed: the header is 64 bytes, the two
/// double sections are whole multiples of 8 from there, and everything after
/// them is a whole multiple of 4. So every section lands on its own alignment
/// for ANY counts. The reader checks that anyway -- it costs nothing and a blob
/// is an input.
struct Layout {
    size_t centres{};
    size_t weights{};
    size_t deltaSpans{};
    size_t verts{};
    size_t offsets{};
    size_t names{};
    size_t components{};
    size_t total{};
};

Layout layoutFor(size_t poseCount, size_t dimension, size_t totalVerts, size_t nameBytes,
                 size_t driverCount) {
    Layout l;
    l.centres    = kHeaderBytes;
    l.weights    = l.centres + poseCount * dimension * sizeof(double);
    l.deltaSpans = l.weights + poseCount * poseCount * sizeof(double);
    l.verts      = l.deltaSpans + poseCount * 2 * sizeof(uint32_t);
    l.offsets    = l.verts + totalVerts * sizeof(uint32_t);
    l.names      = l.offsets + totalVerts * sizeof(Vec3);
    l.components = l.names + nameBytes;
    l.total      = l.components + driverCount;
    return l;
}

template <typename T>
void put(std::vector<std::byte>& out, size_t offset, const T& value) {
    std::memcpy(out.data() + offset, &value, sizeof(T));
}

template <typename T>
T get(std::span<const std::byte> bytes, size_t offset) {
    T value{};
    std::memcpy(&value, bytes.data() + offset, sizeof(T));
    return value;
}

std::unexpected<CorrectiveBlobError> blobFail(CorrectiveBlobErrorKind kind,
                                              std::string detail = {}) {
    return std::unexpected(CorrectiveBlobError{kind, std::move(detail)});
}

}  // namespace

std::string CorrectiveCompileError::message() const {
    std::string s;
    switch (kind) {
        case CorrectiveCompileErrorKind::PayloadUnreadable:
            s = "cannot read a delta payload";
            break;
        case CorrectiveCompileErrorKind::EmptyPayload: s = "a delta payload moves nothing"; break;
        case CorrectiveCompileErrorKind::NotSolvable:
            s = "the interpolation matrix cannot be solved";
            break;
    }
    if (!detail.empty()) s += ": " + detail;
    return s;
}

std::string CorrectiveBlobError::message() const {
    std::string s;
    switch (kind) {
        case CorrectiveBlobErrorKind::TooSmall: s = "shorter than a blob header"; break;
        case CorrectiveBlobErrorKind::BadMagic: s = "not a corrective blob"; break;
        case CorrectiveBlobErrorKind::UnsupportedVersion:
            s = "blob version is not the one this build reads";
            break;
        case CorrectiveBlobErrorKind::SizeMismatch:
            s = "the blob is not the length its header claims";
            break;
        case CorrectiveBlobErrorKind::Inconsistent:
            s = "the header's counts do not describe this buffer";
            break;
    }
    if (!detail.empty()) s += ": " + detail;
    return s;
}

std::expected<std::vector<std::byte>, CorrectiveCompileError> compileCorrectives(
    const CorrectiveManifest& manifest) {
    const size_t poseCount = manifest.poses.size();
    const size_t dimension = manifest.dimension();

    std::vector<Target> deltas;
    deltas.reserve(poseCount);
    for (const ExamplePose& pose : manifest.poses) {
        auto t = loadTarget(pose.delta);
        if (!t) {
            return std::unexpected(
                CorrectiveCompileError{CorrectiveCompileErrorKind::PayloadUnreadable,
                                       pose.name + ": " + t.error().message()});
        }
        if (t->empty()) {
            return std::unexpected(CorrectiveCompileError{CorrectiveCompileErrorKind::EmptyPayload,
                                                          pose.name + ": " + pose.delta.string()});
        }
        deltas.push_back(std::move(*t));
    }

    // THE compile-time choice: solve for the identity, so that at example pose i
    // the weight vector is the i-th basis vector. See the header.
    std::vector<double> centres;
    centres.reserve(poseCount * dimension);
    std::vector<double> values(poseCount * poseCount, 0.0);
    for (size_t i = 0; i < poseCount; ++i) {
        centres.insert(centres.end(), manifest.poses[i].signal.begin(),
                       manifest.poses[i].signal.end());
        values[i * poseCount + i] = 1.0;
    }

    const auto solved =
        foundation::rbfSolve(centres, dimension, values, poseCount, manifest.radius);
    if (!solved) {
        return std::unexpected(CorrectiveCompileError{CorrectiveCompileErrorKind::NotSolvable,
                                                      "radius " + std::to_string(manifest.radius) +
                                                          " against " + std::to_string(poseCount) +
                                                          " example poses"});
    }

    size_t totalVerts = 0;
    for (const Target& t : deltas)
        totalVerts += t.verts.size();

    std::string names;
    for (const Driver& d : manifest.drivers) {
        names += d.joint;
        names += '\0';
    }
    for (const ExamplePose& p : manifest.poses) {
        names += p.name;
        names += '\0';
    }

    const Layout l =
        layoutFor(poseCount, dimension, totalVerts, names.size(), manifest.drivers.size());
    std::vector<std::byte> out(l.total, std::byte{0});

    std::memcpy(out.data(), kMagic, sizeof(kMagic));
    put(out, 8, kCorrectiveBlobVersion);
    put(out, 12, static_cast<uint32_t>(dimension));
    put(out, 16, manifest.hash);
    put(out, 24, manifest.topologyHash);
    put(out, 32, manifest.radius);
    put(out, 40, static_cast<uint32_t>(poseCount));
    put(out, 44, static_cast<uint32_t>(manifest.drivers.size()));
    put(out, 48, static_cast<uint32_t>(names.size()));
    put(out, 52, static_cast<uint32_t>(totalVerts));
    put(out, 56, static_cast<uint64_t>(l.total));

    std::memcpy(out.data() + l.centres, solved->centres.data(),
                solved->centres.size() * sizeof(double));
    std::memcpy(out.data() + l.weights, solved->weights.data(),
                solved->weights.size() * sizeof(double));

    uint32_t first = 0;
    for (size_t i = 0; i < poseCount; ++i) {
        const auto count = static_cast<uint32_t>(deltas[i].verts.size());
        put(out, l.deltaSpans + i * 2 * sizeof(uint32_t), first);
        put(out, l.deltaSpans + (i * 2 + 1) * sizeof(uint32_t), count);
        std::memcpy(out.data() + l.verts + first * sizeof(uint32_t), deltas[i].verts.data(),
                    count * sizeof(uint32_t));
        std::memcpy(out.data() + l.offsets + first * sizeof(Vec3), deltas[i].offsets.data(),
                    count * sizeof(Vec3));
        first += count;
    }

    std::memcpy(out.data() + l.names, names.data(), names.size());
    for (size_t i = 0; i < manifest.drivers.size(); ++i) {
        out[l.components + i] =
            static_cast<std::byte>(manifest.drivers[i].component == DriverComponent::Swing ? 0 : 1);
    }
    return out;
}

std::expected<CompiledCorrectives, CorrectiveBlobError> readCorrectiveBlob(
    std::span<const std::byte> bytes) {
    if (bytes.size() < kHeaderBytes) return blobFail(CorrectiveBlobErrorKind::TooSmall);
    if (std::memcmp(bytes.data(), kMagic, sizeof(kMagic)) != 0) {
        return blobFail(CorrectiveBlobErrorKind::BadMagic);
    }

    CompiledCorrectives c;
    c.formatVersion = get<uint32_t>(bytes, 8);
    if (c.formatVersion != kCorrectiveBlobVersion) {
        return blobFail(CorrectiveBlobErrorKind::UnsupportedVersion,
                        "this build reads " + std::to_string(kCorrectiveBlobVersion));
    }
    c.dimension    = get<uint32_t>(bytes, 12);
    c.manifestHash = get<uint64_t>(bytes, 16);
    c.topologyHash = get<uint64_t>(bytes, 24);
    c.radius       = get<double>(bytes, 32);
    c.poseCount    = get<uint32_t>(bytes, 40);

    const size_t driverCount = get<uint32_t>(bytes, 44);
    const size_t nameBytes   = get<uint32_t>(bytes, 48);
    const size_t totalVerts  = get<uint32_t>(bytes, 52);
    const auto claimed       = get<uint64_t>(bytes, 56);

    if (claimed != bytes.size()) {
        return blobFail(CorrectiveBlobErrorKind::SizeMismatch,
                        "header says " + std::to_string(claimed) + ", buffer is " +
                            std::to_string(bytes.size()));
    }
    if (c.poseCount == 0 || c.dimension == 0 || driverCount == 0) {
        return blobFail(CorrectiveBlobErrorKind::Inconsistent, "a count is zero");
    }

    const Layout l = layoutFor(c.poseCount, c.dimension, totalVerts, nameBytes, driverCount);
    if (l.total != bytes.size()) {
        return blobFail(CorrectiveBlobErrorKind::Inconsistent,
                        "counts describe " + std::to_string(l.total) + " bytes");
    }
    // The layout is only mappable if every numeric section starts aligned. It
    // does by construction here, but a blob is an input: checking costs nothing
    // and reading an unaligned span of doubles is undefined behaviour.
    if (l.centres % alignof(double) != 0 || l.weights % alignof(double) != 0 ||
        l.deltaSpans % alignof(uint32_t) != 0 || l.verts % alignof(uint32_t) != 0 ||
        l.offsets % alignof(float) != 0) {
        return blobFail(CorrectiveBlobErrorKind::Inconsistent, "a section is not aligned");
    }

    c.coefficients.dimension = c.dimension;
    c.coefficients.outputs   = c.poseCount;
    c.coefficients.count     = c.poseCount;
    c.coefficients.radius    = c.radius;
    c.coefficients.centres.resize(c.poseCount * c.dimension);
    c.coefficients.weights.resize(c.poseCount * c.poseCount);
    std::memcpy(c.coefficients.centres.data(), bytes.data() + l.centres,
                c.coefficients.centres.size() * sizeof(double));
    std::memcpy(c.coefficients.weights.data(), bytes.data() + l.weights,
                c.coefficients.weights.size() * sizeof(double));

    // Names: driverCount joints then poseCount pose names, NUL-terminated. Read
    // by scanning rather than trusting a count, so a table that runs short is
    // caught rather than read past.
    std::vector<std::string_view> names;
    const auto* nameBase = reinterpret_cast<const char*>(bytes.data() + l.names);
    size_t at            = 0;
    while (at < nameBytes && names.size() < driverCount + c.poseCount) {
        const size_t end = std::string_view(nameBase + at, nameBytes - at).find('\0');
        if (end == std::string_view::npos) break;
        names.emplace_back(nameBase + at, end);
        at += end + 1;
    }
    if (names.size() != driverCount + c.poseCount || at != nameBytes) {
        return blobFail(CorrectiveBlobErrorKind::Inconsistent, "the name table does not match");
    }

    c.drivers.reserve(driverCount);
    for (size_t i = 0; i < driverCount; ++i) {
        const auto raw = static_cast<uint8_t>(bytes[l.components + i]);
        if (raw > 1) {
            return blobFail(CorrectiveBlobErrorKind::Inconsistent, "unknown driver component");
        }
        c.drivers.push_back(
            Driver{.joint     = std::string(names[i]),
                   .component = raw == 0 ? DriverComponent::Swing : DriverComponent::Twist});
    }
    // Derived from the drivers, exactly as the manifest derives it, so a header
    // claiming a dimension its own drivers do not add up to is refused.
    size_t fromDrivers = 0;
    for (const Driver& d : c.drivers)
        fromDrivers += componentDimension(d.component);
    if (fromDrivers != c.dimension) {
        return blobFail(CorrectiveBlobErrorKind::Inconsistent,
                        "drivers add up to " + std::to_string(fromDrivers) + ", header says " +
                            std::to_string(c.dimension));
    }

    c.poseNames.assign(names.begin() + static_cast<ptrdiff_t>(driverCount), names.end());

    // Read in place. The offsets were checked for alignment above, which is what
    // makes this defined in practice on the one compiler and architecture this
    // project targets; a strictly conforming reader would need
    // `std::start_lifetime_as`, which libc++ does not ship yet.
    const auto* spans  = reinterpret_cast<const uint32_t*>(bytes.data() + l.deltaSpans);
    const auto* verts  = reinterpret_cast<const uint32_t*>(bytes.data() + l.verts);
    const auto* offset = reinterpret_cast<const Vec3*>(bytes.data() + l.offsets);
    c.deltas.reserve(c.poseCount);
    for (size_t i = 0; i < c.poseCount; ++i) {
        const size_t first = spans[i * 2];
        const size_t count = spans[i * 2 + 1];
        if (count == 0 || first + count > totalVerts) {
            return blobFail(CorrectiveBlobErrorKind::Inconsistent,
                            "delta " + std::to_string(i) + " runs outside the vertex table");
        }
        // The largest index, found once here rather than per frame: the view
        // carries it so `CorrectiveBuffer::apply` can range-check a corrective
        // in O(1). Scanning once is the cost of not spending four bytes per
        // pose in the format on something derivable from what is already there.
        uint32_t largest = 0;
        for (size_t v = 0; v < count; ++v)
            largest = std::max(largest, verts[first + v]);
        c.deltas.push_back(TargetView{.verts          = std::span(verts + first, count),
                                      .offsets        = std::span(offset + first, count),
                                      .maxVertexIndex = largest});
    }
    return c;
}

}  // namespace mh::core
