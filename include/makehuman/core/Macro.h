// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string_view>

namespace mh::core {

/// The nine macro categories and their 27 discrete values.
///
/// This table is the load-bearing convention of the whole modelling system: a
/// target's filename is split on `-`, `_` and `.`, and any token appearing here
/// becomes a macro dependency rather than part of the target's group name
/// (legacy-python/lib/targets.py:50-67, :203-215).
enum class MacroCategory : uint8_t {
    Gender,
    Age,
    Race,
    Muscle,
    Weight,
    Height,
    BreastSize,
    BreastFirmness,
    BodyProportions,
    Count
};

inline constexpr size_t kMacroCategoryCount = static_cast<size_t>(MacroCategory::Count);

/// Every macro value, in the reference's declaration order. The index into this
/// array is the factor id used by the weighting rule.
enum class MacroValue : uint8_t {
    Male,
    Female,
    Baby,
    Child,
    Young,
    Old,
    Caucasian,
    Asian,
    African,
    MaxMuscle,
    AverageMuscle,
    MinMuscle,
    MinWeight,
    AverageWeight,
    MaxWeight,
    MinHeight,
    AverageHeight,
    MaxHeight,
    MinCup,
    AverageCup,
    MaxCup,
    MinFirmness,
    AverageFirmness,
    MaxFirmness,
    UncommonProportions,
    RegularProportions,
    IdealProportions,
    Count
};

inline constexpr size_t kMacroValueCount = static_cast<size_t>(MacroValue::Count);

/// The spelling of each value as it appears in a target filename.
[[nodiscard]] std::string_view macroValueName(MacroValue v) noexcept;

/// The category a value belongs to.
[[nodiscard]] MacroCategory categoryOf(MacroValue v) noexcept;

/// Looks up a filename token. Returns nullopt when the token is not a macro
/// value, in which case it belongs to the target's group key.
[[nodiscard]] std::optional<MacroValue> macroValueFromToken(std::string_view token) noexcept;

/// The 27 derived per-value weights, computed from the 11 user-facing scalars.
///
/// Every formula below is transcribed verbatim from the reference; see the
/// citation on each setter. They are not interchangeable: weight and muscle use
/// `1 - (max + min)` for the average while height, cup, firmness and proportions
/// use an if/else. The results are identical (at most one of max/min is
/// non-zero) but the reference is reproduced as written.
class MacroFactors {
public:
    MacroFactors() { recomputeAll(); }

    // -- user-facing scalars, all in [0,1] --------------------------------
    void setGender(float v);
    void setAge(float v);
    void setMuscle(float v);
    void setWeight(float v);
    void setHeight(float v);
    void setBreastSize(float v);
    void setBreastFirmness(float v);
    void setBodyProportions(float v);

    /// Sets one ethnic component and renormalises the other two so all three
    /// sum to 1 (legacy-python/apps/human.py:847-888), including the three
    /// degenerate branches.
    void setCaucasian(float v);
    void setAsian(float v);
    void setAfrican(float v);

    [[nodiscard]] float gender() const noexcept { return gender_; }

    [[nodiscard]] float age() const noexcept { return age_; }

    [[nodiscard]] float muscle() const noexcept { return muscle_; }

    [[nodiscard]] float weight() const noexcept { return weight_; }

    [[nodiscard]] float height() const noexcept { return height_; }

    [[nodiscard]] float breastSize() const noexcept { return breastSize_; }

    [[nodiscard]] float breastFirmness() const noexcept { return breastFirmness_; }

    [[nodiscard]] float bodyProportions() const noexcept { return bodyProportions_; }

    [[nodiscard]] float caucasian() const noexcept { return caucasian_; }

    [[nodiscard]] float asian() const noexcept { return asian_; }

    [[nodiscard]] float african() const noexcept { return african_; }

    /// The derived weight of a macro value.
    [[nodiscard]] float value(MacroValue v) const noexcept {
        return values_[static_cast<size_t>(v)];
    }

    /// Age in years. `<0.5` maps 1..25, `>=0.5` maps 25..90
    /// (legacy-python/apps/human.py:552-559).
    [[nodiscard]] float ageYears() const noexcept;

private:
    void recomputeAll();
    void setAgeVals();
    void setGenderVals();
    void setMuscleVals();
    void setWeightVals();
    void setHeightVals();
    void setBreastSizeVals();
    void setBreastFirmnessVals();
    void setBodyProportionVals();
    void setEthnicVals(std::optional<MacroValue> exclude);
    void writeEthnicValues();

    float gender_{0.5F};
    float age_{0.5F};
    float muscle_{0.5F};
    float weight_{0.5F};
    float height_{0.5F};
    float breastSize_{0.5F};
    float breastFirmness_{0.5F};
    float bodyProportions_{0.5F};

    float caucasian_{1.0F / 3.0F};
    float asian_{1.0F / 3.0F};
    float african_{1.0F / 3.0F};

    std::array<float, kMacroValueCount> values_{};
};

}  // namespace mh::core
