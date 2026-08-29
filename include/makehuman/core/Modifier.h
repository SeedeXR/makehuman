// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include "makehuman/core/Macro.h"
#include "makehuman/core/Target.h"
#include "makehuman/core/TargetIndex.h"

#include <expected>
#include <filesystem>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace mh::core {

/// What a modifier drives.
enum class ModifierKind : uint8_t {
    /// A bipolar or unipolar shape slider built from target-name extensions,
    /// e.g. `head-age-decr` / `head-age-incr` (humanmodifier.py:488).
    Universal,
    /// Drives one of the macro scalars; contributes no factor of its own beyond
    /// setting the scalar, whose derived values then weight the targets
    /// (humanmodifier.py:547, :607-610).
    Macro,
    /// A macro that participates in ethnic renormalisation (humanmodifier.py:615).
    Ethnic,
};

/// One slider.
///
/// Name assembly follows humanmodifier.py:493-513 exactly:
///   - with both extensions: left = `<group>-<target>-<min>`,
///     right = `<group>-<target>-<max>`, and the modifier is named
///     `<target>-<min>|<max>` (or `<min>|<mid>|<max>` with a centre)
///   - with neither: right = `<group>-<target>`, name = `<target>`
struct Modifier {
    std::string group;
    std::string name;      ///< as assembled above
    std::string fullName;  ///< "<group>/<name>"
    ModifierKind kind{ModifierKind::Universal};

    /// Target group names for each side of a Universal modifier. All three are
    /// empty for a Macro/Ethnic modifier, which the reference also leaves unset
    /// -- it resolves targets from the group name instead (humanmodifier.py:566).
    std::string left;
    std::string right;
    std::string center;

    /// The target group a Macro/Ethnic modifier draws from: its group name.
    std::string targetGroup;

    /// For a macro modifier, the scalar it drives.
    std::optional<MacroValue> macroValue;  ///< only for Ethnic
    std::string macroVariable;             ///< e.g. "Gender", "Age", "African"

    float defaultValue{0.0F};

    /// Range: [-1,1] when a left side exists, else [0,1]
    /// (humanmodifier.py:453-459, :530-534).
    [[nodiscard]] float minValue() const noexcept { return left.empty() ? 0.0F : -1.0F; }

    [[nodiscard]] float maxValue() const noexcept { return 1.0F; }

    [[nodiscard]] float clamp(float v) const noexcept;
};

enum class ModifierErrorKind { NotFound, Unreadable, Malformed };

struct ModifierError {
    ModifierErrorKind kind{};
    std::string file;
    std::string detail;

    [[nodiscard]] std::string message() const;
};

/// Loads a `*_modifiers.json` file.
///
/// Schema (humanmodifier.py:677-696): a list of `{group, modifiers:[...]}`,
/// each entry either `{target, min?, max?, mid?}` for a Universal modifier or
/// `{macrovar, modifierType?}` for a Macro/Ethnic one.
[[nodiscard]] std::expected<std::vector<Modifier>, ModifierError> loadModifiers(
    const std::filesystem::path& jsonPath);

/// A character: the macro scalars, the modifier values, and the resulting
/// weighted target stack.
///
/// The stack maps a target's relative path to its weight, and **zero weights
/// are removed rather than stored** (legacy-python/apps/human.py:918-921) —
/// callers rely on that, since otherwise a full rebuild iterates thousands of
/// no-ops.
class Human {
public:
    Human(const TargetIndex* index, std::vector<Modifier> modifiers);

    [[nodiscard]] const MacroFactors& factors() const noexcept { return factors_; }

    [[nodiscard]] MacroFactors& factors() noexcept { return factors_; }

    [[nodiscard]] const std::vector<Modifier>& modifiers() const noexcept { return modifiers_; }

    [[nodiscard]] const Modifier* findModifier(std::string_view fullName) const;

    /// Current slider value, or its default if never set.
    [[nodiscard]] float modifierValue(std::string_view fullName) const;

    /// Sets a slider and recomputes the affected part of the stack.
    bool setModifierValue(std::string_view fullName, float value);

    /// Recomputes the entire stack from the current scalars and slider values.
    void rebuildStack();

    /// Target relative path -> weight. Never contains a zero.
    [[nodiscard]] const std::unordered_map<std::string, float>& stack() const noexcept {
        return stack_;
    }

    [[nodiscard]] size_t stackSize() const noexcept { return stack_.size(); }

    /// Resets @p mesh to its morph base and applies the whole stack.
    ///
    /// This mirrors `applyAllTargets` (legacy-python/apps/human.py:1147-1209):
    /// restore `orig_coord`, then apply every entry at its weight. Application
    /// is additive and commutative, so stack order does not affect the result.
    ///
    /// @return the number of targets applied. Missing targets are skipped and
    ///         counted in @p missing rather than aborting the rebuild.
    uint32_t applyStack(Mesh& mesh, TargetLibrary& targets, uint32_t* missing = nullptr) const;

private:
    void accumulate(const Modifier& m, float value);

    const TargetIndex* index_{};
    std::vector<Modifier> modifiers_;
    std::unordered_map<std::string, float> values_;
    std::unordered_map<std::string, float> stack_;
    MacroFactors factors_;
};

}  // namespace mh::core
