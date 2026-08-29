// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include "makehuman/core/Macro.h"

#include <array>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace mh::core {

/// One `.target` file, indexed by what its filename says about it.
///
/// A target's path IS its parameterisation. Every path and filename component
/// is split on `-`, `_` and `.`; a token naming a macro value becomes a
/// dependency, everything else joins the group key
/// (legacy/python/lib/targets.py:188-227).
///
///     data/targets/macrodetails/african-female-baby.target
///       -> key   = ["macrodetails"]
///          deps  = {race: African, gender: Female, age: Baby}
struct TargetComponent {
    std::vector<std::string> key;
    std::filesystem::path path;
    std::string relativePath;

    /// One slot per macro category; empty where the filename says nothing.
    std::array<std::optional<MacroValue>, kMacroCategoryCount> data{};

    /// The macro values this target depends on, in category order.
    [[nodiscard]] std::vector<MacroValue> variables() const;

    /// `key` joined with '-', which is also the group name used as a factor.
    [[nodiscard]] std::string groupName() const;
};

/// Groups every target by its key, exactly as the reference does.
///
/// Measured on the shipped data: **653 groups over 1,280 targets**, the largest
/// being `breast` (216), `macrodetails-height` (144),
/// `macrodetails-proportions` (108), `macrodetails-universal` (72) and
/// `macrodetails` (24).
class TargetIndex {
public:
    /// Walks @p root recursively, indexing every `.target` file beneath it.
    ///
    /// @param root the data directory (the one containing `targets/`)
    [[nodiscard]] static TargetIndex build(const std::filesystem::path& root);

    /// Components sharing a group key, e.g. {"macrodetails","universal"}.
    /// Empty span when the group does not exist.
    [[nodiscard]] std::span<const TargetComponent> group(const std::vector<std::string>& key) const;

    [[nodiscard]] std::span<const TargetComponent> group(std::string_view dashedKey) const;

    [[nodiscard]] size_t groupCount() const noexcept { return groups_.size(); }

    [[nodiscard]] size_t componentCount() const noexcept { return componentCount_; }

    /// Every group key present, for enumeration and diagnostics.
    [[nodiscard]] std::vector<std::string> groupNames() const;

private:
    // Keyed by the dashed group name so lookup needs no vector hashing.
    std::unordered_map<std::string, std::vector<TargetComponent>> groups_;
    size_t componentCount_{};
};

/// A target together with the factors whose product is its weight.
struct WeightedTarget {
    const TargetComponent* component{};
    std::vector<MacroValue> factors;  ///< the target's macro dependencies
    bool usesGroupFactor{true};
};

/// The weighting rule, and the reason a slider becomes geometry:
///
///     weight(target) = value * PRODUCT(factors[f]) for each dependency f
///
/// legacy/python/apps/humanmodifier.py:644-652. The group name itself is also
/// a factor there; a caller supplies its contribution through @p groupFactor,
/// which is 1.0 for a macro group (`humanmodifier.py:607-610`) and the signed
/// share of the slider for a left/right/centre modifier (`:536-545`).
[[nodiscard]] float targetWeight(const TargetComponent& component, const MacroFactors& factors,
                                 float value = 1.0F, float groupFactor = 1.0F);

}  // namespace mh::core
