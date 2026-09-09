// SPDX-License-Identifier: AGPL-3.0-or-later
//
// The corrective authoring manifest: stage one of owner directive 12.4.
//
//   AUTHORING  this manifest plus the existing sparse `.target` payloads.
//              Diffable, hand-editable, git-reviewable.
//   COMPILE    solve the RBF offline and bake an mmap-able blob.
//   RUNTIME    the blob only -- a disposable cache, invalidated on hash
//              mismatch.
//
// This header is only the first of those. It defines what an author writes and
// what the compiler will read: which joints drive, which component of each,
// where every example pose sits in signal space and what it sculpts.
//
// **JSON rather than TOML, for a licensing reason rather than a taste one.**
// nlohmann/json is already a recorded dependency (LICENSING.md 5.1) and a TOML
// library would be a new one; hard rule 6 forbids adding one without recording
// it, and TOML buys nothing here that would justify the entry. The directive
// offers either.
//
// The format version is checked STRICTLY, which is what directive 12.4 means by
// versioning the manifest conservatively: a version this build does not know
// is refused rather than read for the parts it recognises. Silently ignoring a
// newer field means geometry that quietly does not appear.
#pragma once

#include <cstdint>
#include <expected>
#include <filesystem>
#include <string>
#include <vector>

namespace mh::core {

/// The version of THIS format that this build writes and reads.
///
/// Bumped only for a change that an older reader could not handle correctly --
/// conservatively, per directive 12.4. The compiled blob's own version moves
/// far more freely, because a blob is a cache that can always be rebuilt.
inline constexpr uint32_t kCorrectiveManifestVersion = 1;

/// Which half of a joint's rotation drives the signal.
///
/// The split `foundation::swingTwist` produces, and the reason directive 12.3
/// rules out Euler angles: three sequential angles gimbal-lock, and near the
/// lock a twist reading jumps while the rotation barely moves.
enum class DriverComponent : uint8_t {
    /// How far the bone is bent away from rest. Contributes THREE numbers --
    /// the rotation vector of the swing quaternion.
    Swing,
    /// How far the bone is rotated about its own length. Contributes ONE
    /// signed angle.
    Twist,
};

/// How many numbers a component contributes to the signal vector.
[[nodiscard]] constexpr size_t componentDimension(DriverComponent c) noexcept {
    return c == DriverComponent::Swing ? 3 : 1;
}

/// One joint, one component of its rotation.
struct Driver {
    std::string joint;
    DriverComponent component{};

    [[nodiscard]] bool operator==(const Driver&) const = default;
};

/// One sculpted example: a point in signal space and the deltas authored there.
struct ExamplePose {
    std::string name;
    /// Exactly `dimension()` long, in driver order.
    std::vector<double> signal;
    /// Resolved against the manifest's own directory, so a caller never needs
    /// to know where the manifest was.
    std::filesystem::path delta;
};

enum class CorrectiveManifestErrorKind : uint8_t {
    NotFound,
    /// Something is there but cannot be read as a file -- a directory, a FIFO.
    Unreadable,
    /// Not JSON, not an object, or a required field missing or of the wrong
    /// type.
    Malformed,
    /// Absent, or a version this build does not implement.
    UnsupportedVersion,
    UnknownKernel,
    BadRadius,
    NoDrivers,
    UnknownComponent,
    /// The same joint AND component twice, which would count that joint's
    /// contribution twice and make every distance in signal space wrong.
    DuplicateDriver,
    NoPoses,
    /// A pose's signal is not the length its drivers add up to.
    SignalDimension,
    /// Two poses with the same name, or at the same point in signal space --
    /// the latter makes the interpolation matrix singular.
    DuplicatePose,
    /// Empty, absolute, or reaching outside the manifest's directory.
    BadPayloadPath,
};

struct CorrectiveManifestError {
    CorrectiveManifestErrorKind kind{};
    std::string file;
    /// Which driver, pose or field -- an error a forty-pose manifest's author
    /// can act on.
    std::string detail;

    [[nodiscard]] std::string message() const;
};

/// A parsed manifest.
struct CorrectiveManifest {
    uint32_t formatVersion{};
    /// `core::topologyHash` of the base mesh this was authored against.
    ///
    /// Directive 12.7's guard. A `.target` is a list of VERTEX INDICES, so
    /// renumbering the base mesh puts every delta somewhere else with nothing
    /// to notice it. Comparing this against the loaded mesh is the caller's
    /// job; stating it is the manifest's.
    uint64_t topologyHash{};
    /// The Gaussian width, in the units of the signal space. Only the Gaussian
    /// kernel exists (see `foundation::Rbf`), so the kernel is not stored --
    /// an unknown one is refused at load.
    double radius{};
    std::vector<Driver> drivers;
    std::vector<ExamplePose> poses;
    /// FNV-1a over the manifest's CONTENT, in a canonical order.
    ///
    /// What makes the compiled blob a disposable cache: the blob records this,
    /// and a mismatch means rebuild. Whitespace, key order and the manifest's
    /// location are deliberately NOT content -- reformatting a file or moving a
    /// checkout must not throw away a cache.
    uint64_t hash{};

    /// The signal-space dimension, summed from the drivers.
    ///
    /// Derived rather than declared, so a manifest cannot claim a dimension its
    /// own drivers do not add up to.
    [[nodiscard]] size_t dimension() const noexcept;
};

/// Reads and validates the manifest at @p path.
///
/// Every check refuses rather than defaults. A corrective that silently does
/// not appear, or appears in the wrong place because a delta was applied to a
/// renumbered mesh, is worse than a file that will not load.
[[nodiscard]] std::expected<CorrectiveManifest, CorrectiveManifestError> loadCorrectiveManifest(
    const std::filesystem::path& path);

}  // namespace mh::core
