// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

// Symmetry.h for symmetrySide/symmetricOpposite: the randomiser mirrors pairs,
// and the day a symmetry COMMAND needed them -- foretold in the note that used
// to sit here -- they moved to their own header rather than being duplicated.
#include "makehuman/core/Symmetry.h"

#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace mh::core {

/// Which families of modifier a randomisation touches.
///
/// The four flags and the groups behind each are the reference's
/// (`legacy/python/plugins/0_modeling_8_random.py:109-121`). `height` is
/// separate from `macro` there and stays separate here: a randomiser that
/// changes stature is useful for crowds and unwanted when you are iterating on
/// a face.
struct RandomOptions {
    /// 1.0 keeps every symmetric pair identical; 0.0 lets the two sides vary
    /// independently across their full range.
    float symmetry{1.0F};
    bool macro{true};
    bool height{false};
    bool face{true};
    bool body{true};
};

/// One draw from the reference's distribution
/// (`0_modeling_8_random.py:184-192`).
///
/// Gaussian around @p middle with sigma = @p sigmaFactor × the range, then
/// **reflected** at each bound rather than clamped. The difference is visible:
/// clamping piles probability onto the endpoints, so a run of characters would
/// share identical extreme features, while reflecting folds it back inside.
[[nodiscard]] float randomValue(float minValue, float maxValue, float middle, float sigmaFactor,
                                uint64_t& state);

/// Randomises @p human in place.
///
/// Deterministic in @p seed: the same seed and options give the same character,
/// which is what makes this testable at all and what lets a user keep a result
/// they liked.
///
/// **One deliberate divergence from the reference**, per CLAUDE.md hard rule 3.
/// Its pregnancy guard reads
///
///     if Gender > 0.5 or Age < 0.2 or Age < 0.75:   # 0_modeling_8_random.py:172-174
///
/// and the comment above it says "No pregnancy for male, too young or **too
/// old** subjects". The third clause is `<` where the comment means `>`, so it
/// is true for every age below 0.75 and the guard fires on almost every
/// character — pregnancy is zeroed nearly always, and the second clause is
/// entirely redundant. We implement the stated intent: `Age > 0.75`.
///
/// @return every modifier this set, and to what, in application order. The
///         caller needs it: the sliders have to be moved to match, and a
///         randomisation that cannot be undone in one step is not usable.
[[nodiscard]] std::vector<std::pair<std::string, float>> randomize(Human& human,
                                                                   const RandomOptions& options,
                                                                   uint64_t seed);

}  // namespace mh::core
