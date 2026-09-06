// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <expected>
#include <filesystem>
#include <fstream>

namespace mh::foundation {

/// Why a path could not be opened for reading.
enum class FileReadErrorKind {
    NotFound,   ///< nothing exists at the path
    NotAFile,   ///< it exists but is a directory, FIFO, socket, device...
    Unreadable  ///< it is a regular file and the open still failed (permissions)
};

/// Opens @p path for reading, refusing anything that is not a REGULAR FILE.
///
/// Every reader in the project used to write this instead:
///
///     if (!std::filesystem::exists(path)) return NotFound;
///     std::ifstream in(path);
///     if (!in) return Unreadable;
///
/// and every one of them accepted a directory. `exists()` is true for a
/// directory and `std::ifstream` **opens** one, so `if (!in)` never fires. What
/// happened next split by how the parser read:
///
///   * a parser calling `sbumpc()` (nlohmann's) throws on the first read;
///   * a parser calling `in.get()` or `std::getline` merely sets badbit and
///     sees end-of-input — so it parses a directory into a valid, EMPTY asset
///     and returns SUCCESS.
///
/// Measured on this tree (libc++, 2026-09-06): `loadMaterial`, `loadMhm` and
/// `loadProxy` all returned success for `data/targets`. Four more —
/// `loadModifiers`, `loadSliderLayout`, `loadSkeleton`, `loadPoseUnitNames` —
/// failed with `Malformed`, which says the file's contents are wrong when the
/// truth is that it is not a file at all.
///
/// `data/` is full of directories sitting next to the files these readers want,
/// so this is one mistyped path away from a character that loads to nothing.
///
/// **Not just directories.** A FIFO also exists, also opens, and then blocks
/// forever on the first read — and a hang is the one failure a test cannot
/// assert on. `is_regular_file` refuses the whole class rather than the one
/// case that happened to be noticed.
[[nodiscard]] std::expected<std::ifstream, FileReadErrorKind> openForRead(
    const std::filesystem::path& path);

}  // namespace mh::foundation
