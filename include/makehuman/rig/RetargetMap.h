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

#include "makehuman/io/BvhReader.h"
#include "makehuman/rig/Skeleton.h"

#include <expected>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

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

/// Renames @p bvh's joints through @p map, in place, and reports how many were
/// renamed.
///
/// This is the whole of retargeting on import. Everything downstream --
/// `makePoseUnits`, `loadBodyPose`, `bonesDrivenBy` -- matches a BVH joint to a
/// bone by IDENTICALLY the same name, so making the file say `shoulder01.L`
/// where it said `UpArm_L` is all it takes; no second matching path is added
/// that could drift from the first.
///
/// A joint the table does not name is LEFT ALONE rather than dropped. The
/// MakeHuman 1.x tables deliberately leave 16 connector joints unmapped, and a
/// joint keeping a name no bone shares simply drives nothing -- which is what
/// it did before. Dropping it would renumber every `parent` index in the file.
///
/// End sites are skipped. `BvhReader` names them `<parent>_end` expressly so
/// they cannot collide with a real joint, and a table never names one.
size_t retargetJoints(io::BvhFile& bvh, const RetargetMap& map);

/// The table read backwards: this rig's bone name -> what the other skeleton
/// calls it.
///
/// `RetargetMap` is authored in the IMPORT direction, source name -> our bone,
/// because that is the direction a file arrives in. Export is the same question
/// asked the other way, and inverting is exact rather than approximate: both
/// shipped tables are gated as INJECTIVE (`test_makehuman1_retarget.cpp`,
/// `test_mixamo_retarget.cpp`), so no two sources claim one bone and nothing is
/// lost. A non-injective table would silently drop pairs, so this refuses to
/// pretend: the loser is reported, not swallowed.
///
/// @param collisions if non-null, receives the number of pairs dropped because
///        two sources named the same bone. Zero for every shipped table.
[[nodiscard]] RetargetMap invertRetargetMap(const RetargetMap& map, size_t* collisions = nullptr);

/// Renames @p names in place through @p map, returning how many actually
/// CHANGED -- not how many the table covers.
///
/// The two differ: a table may map a bone to the name it already has. The
/// Mixamo table does exactly once, for `HeadTop_End`, because this rig borrowed
/// Mixamo's word for that bone. Counting hits rather than changes would report
/// work that did not happen.
///
/// This is the export counterpart of `retargetJoints`, and it exists because
/// four of the five skeleton-carrying writers -- glTF, FBX, USD and DAE -- read
/// their bone names from exactly one place, `foundation::SkinView::jointNames`.
/// Renaming that vector once covers all four. BVH is the exception: it does not
/// go through `SkinView` at all, so it takes `retargetJoints` on its own
/// `BvhFile` with the same inverted table.
///
/// A name the table does not cover is LEFT ALONE, exactly as an unmapped joint
/// is on import. The MakeHuman 1.x table names 59 of 179 superset bones and the
/// Mixamo table 65; the rest keep this rig's own names, which is the only
/// honest thing to call a bone the other skeleton has no word for.
size_t renameBones(std::vector<std::string>& names, const RetargetMap& map);

/// How well one naming fits a motion file: the table's name, and how many of a
/// skeleton's bones the file drives once read through it.
struct NamingFit {
    std::string naming;
    size_t driven{};
};

/// @p candidates ranked by how many of @p skeleton's bones the BVH at @p bvh
/// drives under each, best first. Ties keep the caller's order.
///
/// This exists because the shipped motion files are unusable by default and a
/// GUI user cannot say otherwise. MEASURED: every `.bvh` under
/// `data/animations/` drives **0** of the 179 superset bones under this rig's
/// own names, 59 through the MakeHuman 1.x table and 5 through Mixamo's. On the
/// command line that is survivable -- `--rig-names` exists and the loader says
/// so -- but a chooser has no flag to offer, so picking a walk would silently
/// produce a character standing still.
///
/// The file's OWN names are always ranked too, under the name @p nativeNaming,
/// so "none of these tables helps" is a result rather than a special case.
///
/// Reads @p bvh once per candidate, so call it when a file is chosen, not per
/// frame.
[[nodiscard]] std::vector<NamingFit> rankNamings(
    const std::filesystem::path& bvh, const Skeleton& skeleton,
    std::span<const std::pair<std::string, RetargetMap>> candidates,
    std::string_view nativeNaming = "native");

}  // namespace mh::rig
