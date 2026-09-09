// SPDX-License-Identifier: AGPL-3.0-or-later
//
// The corrective compiler and the blob it bakes: stages two and three of owner
// directive 12.4.
//
//   AUTHORING  the manifest (core/CorrectiveManifest.h).
//   COMPILE    `compileCorrectives` -- solve the RBF ONCE, offline, and bake.
//   RUNTIME    `readCorrectiveBlob` -- the blob only, and per frame just
//              kernel evaluation and a matvec.
//
// **What the RBF is solved FOR is the whole idea.** The values are the IDENTITY
// matrix, so at example pose i the weight vector is the i-th basis vector --
// that pose fully on, every other off -- and between poses they blend. That is
// what makes a sculpted pose reproduce exactly what the artist sculpted, which
// is the one thing a pose-space deformer must do. The weights are NOT a
// partition of unity: a Gaussian RBF sums to a little over one between poses
// (1.03 to 1.05 on a unit-spaced set, measured), and nothing here pretends
// otherwise.
//
// **The blob is host-endian and host-layout on purpose.** It is a cache,
// rebuilt whenever the manifest hash moves, not an interchange format. Paying
// for portability would buy nothing; the magic and version are there to refuse
// a stale or foreign one, not to make it portable. Hence directive 12.4's
// "version the blob format cheaply and aggressively" -- bumping it costs a
// recompile, where bumping the manifest version costs an author an afternoon.
#pragma once

#include "makehuman/core/CorrectiveManifest.h"
#include "makehuman/core/Types.h"
#include "makehuman/foundation/Rbf.h"

#include <cstddef>
#include <cstdint>
#include <expected>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace mh::core {

/// Bumped freely. A blob that does not match is rebuilt, not migrated.
inline constexpr uint32_t kCorrectiveBlobVersion = 1;

enum class CorrectiveCompileErrorKind : uint8_t {
    /// A `.target` payload could not be read.
    PayloadUnreadable,
    /// A payload read cleanly and moves no vertices. Refused rather than baked:
    /// a pose that sculpts nothing is an authoring mistake, and a corrective
    /// that silently does nothing gives nobody anything to notice.
    EmptyPayload,
    /// `foundation::rbfSolve` refused the system -- in practice a radius far
    /// larger than the spacing of the example poses.
    NotSolvable,
};

struct CorrectiveCompileError {
    CorrectiveCompileErrorKind kind{};
    /// Which pose, and why.
    std::string detail;

    [[nodiscard]] std::string message() const;
};

enum class CorrectiveBlobErrorKind : uint8_t {
    /// Shorter than the header.
    TooSmall,
    BadMagic,
    UnsupportedVersion,
    /// The length the header claims is not the length of the buffer.
    SizeMismatch,
    /// The counts in the header do not describe a buffer of that length, or a
    /// section would start unaligned.
    Inconsistent,
};

struct CorrectiveBlobError {
    CorrectiveBlobErrorKind kind{};
    std::string detail;

    [[nodiscard]] std::string message() const;
};

/// A blob, read in place.
///
/// The delta arrays are SPANS INTO the bytes -- they are the bulk, thousands of
/// vertices across every pose, and copying them would defeat the point of a
/// mappable layout. **Keep the buffer alive for as long as this is used.**
///
/// The RBF coefficients are COPIED, which is the one deliberate exception.
/// `foundation::RbfCoefficients` owns its vectors, and for a realistic set --
/// 64 poses in four dimensions is 33 KB -- copying once at load costs nothing,
/// where a span-taking overload of `rbfEvaluate` would widen foundation's API
/// for a benefit nobody has measured.
struct CompiledCorrectives {
    uint32_t formatVersion{};
    /// `CorrectiveManifest::hash` of the manifest this was compiled from.
    /// A mismatch against the manifest on disk means the blob is stale.
    uint64_t manifestHash{};
    /// The base topology it was authored against (directive 12.7's guard).
    uint64_t topologyHash{};
    double radius{};
    size_t dimension{};
    size_t poseCount{};

    /// `Driver` is the manifest's own type, so its joint name is a `std::string`
    /// and IS copied -- a handful of short names, against reusing one type for
    /// one concept rather than adding a near-identical view struct.
    std::vector<Driver> drivers;
    /// These are views into the buffer, and the buffer must outlive them.
    std::vector<std::string_view> poseNames;

    /// Ready for `foundation::rbfEvaluate`. Copied, as above.
    foundation::RbfCoefficients coefficients;

    /// One sparse delta per pose, parallel to `poseNames`, in the order a
    /// weight vector comes out of `rbfEvaluate` -- so they hand straight to
    /// `CorrectiveBuffer::apply`.
    struct Delta {
        std::span<const uint32_t> verts;
        std::span<const Vec3> offsets;
    };

    std::vector<Delta> deltas;
};

/// Loads every payload the manifest names, solves the RBF, and bakes the blob.
///
/// Offline: reads files and factorises an n-by-n matrix, where n is the number
/// of example poses. Deterministic -- compiling the same manifest twice gives
/// the same bytes, or a cache would rebuild for ever.
[[nodiscard]] std::expected<std::vector<std::byte>, CorrectiveCompileError> compileCorrectives(
    const CorrectiveManifest& manifest);

/// Reads a blob in place, validating it against its own header.
///
/// Every section's extent is checked against the buffer, so a truncated or
/// tampered blob is refused rather than read out of bounds.
[[nodiscard]] std::expected<CompiledCorrectives, CorrectiveBlobError> readCorrectiveBlob(
    std::span<const std::byte> bytes);

}  // namespace mh::core
