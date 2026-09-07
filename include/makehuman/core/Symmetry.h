// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include "makehuman/core/Modifier.h"

#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace mh::core {

/// The side of a symmetric pair this modifier name takes: `'l'`, `'r'`, or
/// `'\0'` when symmetry does not apply.
///
/// A component of the `-`-separated name being exactly "l" or "r", not a
/// prefix — `humanmodifier.py:304-311`. So `l-eye-height` is left and
/// `lowerface-...` is not, which a `starts_with("l")` test would get wrong.
[[nodiscard]] char symmetrySide(std::string_view modifierName) noexcept;

/// The full name of the modifier mirroring @p m, or empty when it has none.
[[nodiscard]] std::string symmetricOpposite(const Modifier& m);

/// Copies each modifier onto its opposite so that @p targetSide matches the
/// other one. `'r'` makes the right side match the left, `'l'` the reverse.
///
/// The direction letter names the TARGET, which is also the reference's
/// convention and its own crossover: `applySymmetryRight` calls
/// `symmetrize('r')`, whose source is the LEFT side
/// (`apps/human.py:1228-1264`). Anything other than `'l'` or `'r'` is a
/// caller's mistake and does nothing rather than guessing a direction.
///
/// Two deliberate differences from the reference, both about what a caller can
/// survive:
///  - A modifier whose opposite is not installed is **skipped**. The reference
///    dereferences `getModifier`'s result unconditionally (`human.py:1262`) and
///    would raise; half a pair is a data problem, not a reason to lose the
///    character.
///  - The value is read back **after** the write. `setModifierValue` clamps to
///    the target's own range, so reporting what we asked for rather than what
///    was stored would make a pair with unequal ranges report a change forever.
///
/// @return every modifier this moved and its new value. Only genuine changes
///         appear, so mirroring an already symmetric character returns empty
///         and puts nothing on the undo stack.
[[nodiscard]] std::vector<std::pair<std::string, float>> symmetrise(Human& human, char targetSide);

}  // namespace mh::core
