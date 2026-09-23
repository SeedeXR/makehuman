// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include "makehuman/core/Modifier.h"
#include "makehuman/foundation/SliderSpec.h"

#include <expected>
#include <filesystem>
#include <span>
#include <string>
#include <vector>

namespace mh::core {

enum class SliderLayoutErrorKind { NotFound, Unreadable, Malformed };

struct SliderLayoutError {
    SliderLayoutErrorKind kind{};
    std::string file;
    std::string detail;

    [[nodiscard]] std::string message() const;
};

/// Derives a slider's display name from the modifier's own name.
///
/// Ported from `modifierslider.py:46-56`. The rule: split on `-`, drop a
/// trailing `min|max` component, drop a leading component that merely repeats
/// the group name, then capitalise each word. So `head-age-decr|incr` in group
/// `head` becomes "Age".
///
/// Exposed because it is the part with a real rule behind it; the rest of the
/// loader is transcription.
[[nodiscard]] std::string guessSliderLabel(std::string_view modifierName,
                                           std::string_view groupName);

/// Reads a `*_sliders.json` task-view definition.
///
/// This is the reference's tab registry: which sliders appear on which tab, in
/// which section, in which order, and under what name. **Order is significant
/// everywhere** -- the reference parses with `OrderedDict` and the UI presents
/// them in file order, so this preserves it rather than using a map.
///
/// Each entry names a modifier by full name. An entry whose modifier is not in
/// @p modifiers is **skipped**, not defaulted: a slider that cannot change
/// anything is worse than a missing one. All 291 shipped entries resolve.
[[nodiscard]] std::expected<std::vector<foundation::TaskViewSpec>, SliderLayoutError>
loadSliderLayout(const std::filesystem::path& path, std::span<const Modifier> modifiers);

/// The modifiers and the task views together, which is how every caller wants
/// them: the views are meaningless without the modifiers they resolve against.
struct StandardLayout {
    std::vector<Modifier> modifiers;
    std::vector<foundation::TaskViewSpec> views;
};

/// Loads all three shipped modifier sets and their layouts from @p dataDir,
/// with the task views in **tab order**.
///
/// Tab order is not file order. The reference sorts by `sortOrder`, and a view
/// that gives none is assigned the lowest non-negative integer not already
/// taken (`gui3d.py:300-317`); ties keep load order, because Python's sort is
/// stable. For the shipped files that yields:
///
///     Macro modelling, Body shapes, Gender, Face, Torso, Arms and Legs, Measure
///
/// -- not the Face-first order the files are written in.
[[nodiscard]] std::expected<StandardLayout, SliderLayoutError> loadStandardLayout(
    const std::filesystem::path& dataDir);

/// The combination presets from `combination_presets.json` in @p dataDir.
///
/// A preset names two or three sliders and the values that make a look --
/// "Six-pack" is Muscle high AND Stomach tone high AND Weight low, and no one
/// of those alone produces it. The recipes are DATA rather than code because
/// they are taste, and taste is the part most likely to be wrong and want
/// editing without a rebuild.
///
/// @param known every modifier that exists, so a preset naming one that does
///        not can be refused. A preset that silently sets nothing is the
///        painted no-op this project keeps finding: the click would work, the
///        body would not move, and nothing would say why.
/// @return the presets in file order, or an error. A MISSING file is not an
///         error -- it yields an empty list, so a data directory without the
///         file simply offers no presets.
[[nodiscard]] std::expected<std::vector<foundation::SliderPreset>, SliderLayoutError>
loadCombinationPresets(const std::filesystem::path& dataDir, std::span<const Modifier> known);

}  // namespace mh::core
