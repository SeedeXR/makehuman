// SPDX-License-Identifier: AGPL-3.0-or-later
//
// The compiled blob as a CACHE: owner directive 12.4's third layer, which says
// "the blob only. It is a DISPOSABLE CACHE, invalidated on manifest hash
// mismatch."
//
// The format and the compiler existed before this; nothing wrote a blob to
// disk, checked whether it was stale, or rebuilt it. Two words in that sentence
// carry the whole design:
//
//   DISPOSABLE  deleting it costs nothing but time, and FAILING TO WRITE IT
//               does not fail the load. A cache that can break a character by
//               being unwritable is not a cache.
//   INVALIDATED the manifest's content hash is the key. Anything that does not
//               match -- stale, corrupt, truncated, belonging to another
//               manifest, or in a blob format this build no longer reads -- is
//               rebuilt rather than reported.
//
// **There is no separate compiler tool, and that is deliberate.** This is the
// entry point: the application calls it, a batch script calls it, and both get
// the same rebuild-when-stale behaviour instead of two implementations that can
// disagree about when a blob is current.
#pragma once

#include "makehuman/core/CorrectiveBlob.h"
#include "makehuman/core/CorrectiveManifest.h"

#include <cstddef>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <string>
#include <vector>

namespace mh::core {

/// The extension a compiled blob is written with, beside its manifest and
/// sharing its stem: `correctives.json` -> `correctives.mhcorr`.
///
/// A DERIVED file. It is in `.gitignore` for the same reason a build directory
/// is: committing it would put a machine-specific, host-endian artefact under
/// review, and it can always be rebuilt from what is committed.
inline constexpr const char* kCorrectiveBlobExtension = ".mhcorr";

enum class CorrectiveCacheStatus : uint8_t {
    /// The blob on disk matched the manifest and was used as it stood.
    Reused,
    /// There was no usable blob, so one was compiled. Whether it could then be
    /// WRITTEN is deliberately not part of this: the result is the same either
    /// way, and a caller that cared could look for the file.
    Rebuilt,
};

enum class CorrectiveCacheErrorKind : uint8_t {
    /// The manifest is missing or does not validate. NOT rescued by a blob that
    /// happens to be sitting beside it -- see `loadOrCompileCorrectives`.
    Manifest,
    /// The manifest is fine and compiling it is not.
    Compile,
};

struct CorrectiveCacheError {
    CorrectiveCacheErrorKind kind{};
    std::string detail;

    [[nodiscard]] std::string message() const;
};

struct CorrectiveCache {
    /// The blob's bytes. `readCorrectiveBlob` reads them in place, and they
    /// must outlive anything that does.
    std::vector<std::byte> bytes;
    CorrectiveCacheStatus status{};
    /// Where the blob is, or would have been.
    std::filesystem::path blobPath;
    /// The manifest that was read, whether or not the blob had to be rebuilt.
    ///
    /// It is parsed on every call regardless -- see the note on
    /// `loadOrCompileCorrectives` -- so handing it back costs nothing, and it
    /// is what lets a caller reach the fields the blob deliberately does not
    /// bake. The per-pose wrinkle paths are the reason it exists:
    /// `docs/formats/corrective-manifest.md`, "Not baked into the blob".
    CorrectiveManifest manifest;
};

/// Returns the compiled blob for @p manifestPath, rebuilding it if the one on
/// disk is missing, stale or unusable.
///
/// The manifest is ALWAYS read first, even when a perfectly good blob is
/// sitting beside it. That costs a JSON parse per call and buys the property
/// that matters: a manifest that no longer loads is an error rather than a
/// silently-still-working character. Returning the cached blob instead would
/// keep something running that nobody can rebuild, and hide the breakage until
/// the cache was cleared -- on someone else's machine.
[[nodiscard]] std::expected<CorrectiveCache, CorrectiveCacheError> loadOrCompileCorrectives(
    const std::filesystem::path& manifestPath);

}  // namespace mh::core
