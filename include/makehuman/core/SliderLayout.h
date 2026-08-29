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

}  // namespace mh::core
