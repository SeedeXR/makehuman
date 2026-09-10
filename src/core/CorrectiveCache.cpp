// SPDX-License-Identifier: AGPL-3.0-or-later
//
// See the header for what "disposable" and "invalidated" are taken to mean.
#include "makehuman/core/CorrectiveCache.h"

#include "makehuman/core/CorrectiveManifest.h"
#include "makehuman/foundation/FileRead.h"

#include <fstream>
#include <iterator>

namespace mh::core {

namespace {

/// The blob beside @p manifestPath, if it is one and it belongs to @p manifest.
///
/// Every way of not matching gives the same answer -- nothing -- because to a
/// caller they are the same event: there is no usable cache, so compile. The
/// distinctions matter to `readCorrectiveBlob`'s own callers, not here.
std::vector<std::byte> usableBlob(const std::filesystem::path& blobPath,
                                  const CorrectiveManifest& manifest) {
    // openForRead rather than exists()+ifstream: a directory satisfies both and
    // then reads as an empty file. See foundation/FileRead.h.
    auto opened = foundation::openForRead(blobPath);
    if (!opened) return {};

    std::vector<std::byte> bytes;
    for (std::istreambuf_iterator<char> it(*opened), end; it != end; ++it) {
        bytes.push_back(static_cast<std::byte>(*it));
    }

    // One condition, because the two halves are not independently testable.
    // Dropping `!blob` and reading `manifestHash` off a failed `expected` is
    // undefined behaviour that libc++ does not trap -- the value comes out of
    // the error union's storage, compares unequal to a real hash, and the cache
    // misses exactly as it should. Measured: that mutation passes the file, and
    // passes under ASan+UBSan too. So the guard is kept for correctness and the
    // test cannot see it; saying so beats leaving a line that looks tested.
    const auto blob = readCorrectiveBlob(bytes);
    if (!blob || blob->manifestHash != manifest.hash) return {};
    return bytes;
}

/// Writes @p bytes to @p path, and says nothing if it cannot.
///
/// The cache is disposable: a read-only asset directory, a full disk or a
/// sandbox costs the rebuild time on every load, not the character. Written to
/// a sibling temporary first and renamed, so a crash or a full disk leaves
/// either the old blob or the new one -- never half of one, which would be read
/// back as corrupt and rebuilt, but only after the truncated file had been
/// mistaken for a cache by every other process looking at it.
void writeIfPossible(const std::filesystem::path& path, std::span<const std::byte> bytes) {
    std::filesystem::path temp = path;
    temp += ".tmp";
    {
        std::ofstream out(temp, std::ios::binary | std::ios::trunc);
        if (!out) return;
        out.write(reinterpret_cast<const char*>(bytes.data()),
                  static_cast<std::streamsize>(bytes.size()));
        if (!out) {
            out.close();
            std::error_code ignored;
            std::filesystem::remove(temp, ignored);
            return;
        }
    }
    std::error_code ec;
    std::filesystem::rename(temp, path, ec);
    if (ec) {
        std::error_code ignored;
        std::filesystem::remove(temp, ignored);
    }
}

}  // namespace

std::string CorrectiveCacheError::message() const {
    std::string s = kind == CorrectiveCacheErrorKind::Manifest ? "cannot read the manifest"
                                                               : "cannot compile the correctives";
    if (!detail.empty()) s += ": " + detail;
    return s;
}

std::expected<CorrectiveCache, CorrectiveCacheError> loadOrCompileCorrectives(
    const std::filesystem::path& manifestPath) {
    // The manifest first, ALWAYS, even when a good blob is sitting beside it.
    // See the header: a manifest that no longer loads has to be an error, or a
    // character keeps working that nobody can rebuild.
    const auto manifest = loadCorrectiveManifest(manifestPath);
    if (!manifest) {
        return std::unexpected(
            CorrectiveCacheError{CorrectiveCacheErrorKind::Manifest, manifest.error().message()});
    }

    std::filesystem::path blobPath = manifestPath;
    blobPath.replace_extension(kCorrectiveBlobExtension);

    if (auto cached = usableBlob(blobPath, *manifest); !cached.empty()) {
        return CorrectiveCache{.bytes    = std::move(cached),
                               .status   = CorrectiveCacheStatus::Reused,
                               .blobPath = blobPath};
    }

    auto bytes = compileCorrectives(*manifest);
    if (!bytes) {
        return std::unexpected(
            CorrectiveCacheError{CorrectiveCacheErrorKind::Compile, bytes.error().message()});
    }
    writeIfPossible(blobPath, *bytes);
    return CorrectiveCache{
        .bytes = std::move(*bytes), .status = CorrectiveCacheStatus::Rebuilt, .blobPath = blobPath};
}

}  // namespace mh::core
