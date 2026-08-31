// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "makehuman/io/BvhReader.h"

#include <expected>
#include <filesystem>

namespace mh::io {

/// Writes a `BvhFile` back out as BVH text.
///
/// **The file is written in the reader's in-memory convention, which is Y-up.**
/// BVH does not record its up axis, and `readBvh` may have converted a Z-up
/// file (`BvhFile::convertedFromZUp`); the transforms it hands back are always
/// Y-up. Writing the *original* channel names against Y-up data would produce a
/// file that re-reads with a different Euler order, so channel names are derived
/// from each joint's `rotationOrder` instead of copied from the source.
///
/// That makes read -> write -> read stable, which is the property the round-trip
/// test pins. It also means a Z-up file does not survive as Z-up: it comes back
/// as the equivalent Y-up file, since that is what was actually parsed.
///
/// Joints with no rotation channels (End Site blocks) are written as `End Site`
/// with only an OFFSET, as the format requires.
[[nodiscard]] std::expected<void, BvhError> writeBvh(const std::filesystem::path& path,
                                                     const BvhFile& bvh);

}  // namespace mh::io
