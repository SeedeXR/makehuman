// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Bone-name tables: what another skeleton calls the bone this rig calls X.
//
// `data/rigs/mixamo_retarget.json` shipped in an earlier milestone and NOTHING
// read it -- 65 `Mixamo bone -> superset bone` pairs sitting as data with no
// loader. This is the loader.
//
// It exists because a BVH drives a bone only when it holds a joint of
// IDENTICALLY the same name (`mh::rig::bonesDrivenBy`), so a file written for
// another skeleton poses nothing at all. MEASURED: the shipped
// `data/animations/` walks name 75 joints of the old MakeHuman skeleton and
// match 0 of this rig's 179. Renaming through a table is what makes such a
// file usable, and the owner's decision of 2026-09-13 is that the naming
// becomes a CHOICE -- Mixamo, MakeHuman 1.x, or this port's own names --
// applied on import as well as export.
#pragma once

#include "makehuman/rig/Skeleton.h"

#include <expected>
#include <filesystem>
#include <string>
#include <unordered_map>

namespace mh::rig {

/// Source-skeleton joint name -> this rig's bone name.
struct RetargetMap {
    /// Kept as a plain map rather than wrapped: every caller so far wants an
    /// ordinary lookup, and a wrapper with one method is a layer with one
    /// caller. `_provenance` in the file is deliberately NOT read -- it
    /// documents how the table was generated and is not input to anything.
    std::unordered_map<std::string, std::string> toBone;

    [[nodiscard]] size_t size() const noexcept { return toBone.size(); }
};

/// Reads a retarget table, or reports why not.
///
/// Reuses `SkeletonError` rather than inventing a parallel enum: the three
/// things that can go wrong -- absent, unreadable, malformed -- are exactly
/// its first three kinds, and this IS a table about a skeleton's bones.
[[nodiscard]] std::expected<RetargetMap, SkeletonError> loadRetargetMap(
    const std::filesystem::path& path);

}  // namespace mh::rig
