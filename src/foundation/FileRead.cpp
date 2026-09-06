// SPDX-License-Identifier: Apache-2.0
#include "makehuman/foundation/FileRead.h"

namespace mh::foundation {

std::expected<std::ifstream, FileReadErrorKind> openForRead(const std::filesystem::path& path) {
    // One `status()` call rather than exists() then is_regular_file(): two
    // queries race, and the second would also throw on a path the first
    // accepted. The error_code overload never throws.
    std::error_code ec;
    const std::filesystem::file_status st = std::filesystem::status(path, ec);
    if (ec || !std::filesystem::exists(st)) return std::unexpected(FileReadErrorKind::NotFound);
    if (!std::filesystem::is_regular_file(st)) {
        return std::unexpected(FileReadErrorKind::NotAFile);
    }

    std::ifstream in(path);
    if (!in) return std::unexpected(FileReadErrorKind::Unreadable);
    return in;
}

}  // namespace mh::foundation
