// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include "makehuman/rig/PoseUnits.h"

#include <array>
#include <expected>
#include <span>
#include <string>
#include <string_view>

namespace mh::rig {

/// FACS Action Units expressed with the 60 shipped face pose units.
///
/// The Facial Action Coding System numbers and names a set of visible facial
/// actions, each attributed to the muscle that produces it -- AU12 is the "Lip
/// Corner Puller", zygomaticus major. MakeHuman's face library names its units
/// after the same anatomy (`levator`, `risorius`, `platysma`, `oris`), so the
/// table below is **derived** from those two published vocabularies rather than
/// authored: the AU gives the muscle, and the muscle picks the unit.
///
/// Nothing new is generated. This is a naming layer: `facsExpression` returns
/// the same `Expression` a `.mhpose` file would, so `PoseUnits::blend` and
/// `mixPoses` do the work they already did and a FACS request and a hand-written
/// expression cannot drift apart.
///
/// **Sides.** A bilateral AU carries both halves; asking for `AU12L` or `AU12R`
/// gives one. FACS's own notation for a unilateral action is `L12` / `R12`;
/// this takes the suffix instead, so that the base code is always the prefix of
/// its sided spellings and a parser needs no special case. An AU with one
/// midline muscle refuses a side rather than quietly applying the whole thing.
///
/// **What is deliberately absent.** AUs coding an action this unit set does not
/// hold get no row: a fictional AU number in a user's file is worse than a gap
/// they can see. That covers AU13 (Sharp Lip Puller), AU21 (Neck Tightener),
/// AU24 (Lip Presser), AU25 (Lips Part -- a consequence of others, not an
/// action), AU28 (Lip Suck), AU31 (Jaw Clencher), AU32 (Bite), AU34 (Puff),
/// AU36 (Tongue Bulge), AU37 (Lip Wipe), AU38/39 (nostril dilator and
/// compressor), AU45 (Blink -- a timed event, not an intensity) and AU51-58
/// (head and neck movement, which is the body rig's business, not the face's).
struct ActionUnitInfo {
    /// The FACS code, `AU` followed by the number.
    std::string_view code;

    /// The FACS name of the action.
    std::string_view name;

    /// The muscle FACS attributes it to. Recorded because it is what makes the
    /// row below derivable and checkable rather than a preference.
    std::string_view muscle;

    /// Pose units, left side or unsided first. An AU is SIDED when `right[0]`
    /// is non-empty; then `units` is the two halves and each side takes its
    /// own entry. An unsided AU puts up to two units in `left` and leaves
    /// `right` empty -- AU22 acts on both lips, which is two units and one
    /// action, not two sides.
    std::array<std::string_view, 2> left{};
    std::array<std::string_view, 2> right{};

    [[nodiscard]] bool sided() const noexcept { return !right[0].empty(); }

    /// Every unit this AU names, in order, for walking the whole table.
    [[nodiscard]] std::array<std::string_view, 4> units() const noexcept {
        return {left[0], left[1], right[0], right[1]};
    }
};

/// One Action Unit request: a code and how strongly to apply it.
struct ActionUnit {
    std::string code;
    float weight{};
};

enum class FacsErrorKind {
    /// No such AU code, or an AU this unit set cannot express.
    UnknownUnit,
    /// `L`/`R` asked of an AU that has no sides.
    NotSided,
    /// No Action Units at all.
    Empty,
};

struct FacsError {
    FacsErrorKind kind{};
    std::string detail;

    [[nodiscard]] std::string message() const;
};

/// The Action Units this rig can express, in ascending AU order.
[[nodiscard]] std::span<const ActionUnitInfo> actionUnits();

/// Resolves Action Unit requests to the pose units that make them.
///
/// The result's `units` keeps the REQUEST order, and an AU's own units keep
/// table order within it: `PoseUnits::blend` composes by multiplication and
/// does not commute, so reordering here would silently change the face.
///
/// The weight is applied to every unit of the AU unchanged. FACS grades
/// intensity A-E; a caller wanting those maps them to a number, because a
/// letter scale baked in here would be one more thing to disagree about.
[[nodiscard]] std::expected<Expression, FacsError> facsExpression(
    std::span<const ActionUnit> requested);

}  // namespace mh::rig
