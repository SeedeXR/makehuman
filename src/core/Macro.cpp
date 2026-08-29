// SPDX-License-Identifier: AGPL-3.0-or-later
#include "makehuman/core/Macro.h"

#include <algorithm>
#include <cmath>
#include <unordered_map>

namespace mh::core {
namespace {

struct ValueInfo {
    std::string_view name;
    MacroCategory category;
};

// Order must match the MacroValue enum, which follows the reference's
// _cat_data declaration order (legacy-python/lib/targets.py:50-61).
constexpr std::array<ValueInfo, kMacroValueCount> kValues{{
    {"male", MacroCategory::Gender},
    {"female", MacroCategory::Gender},
    {"baby", MacroCategory::Age},
    {"child", MacroCategory::Age},
    {"young", MacroCategory::Age},
    {"old", MacroCategory::Age},
    {"caucasian", MacroCategory::Race},
    {"asian", MacroCategory::Race},
    {"african", MacroCategory::Race},
    {"maxmuscle", MacroCategory::Muscle},
    {"averagemuscle", MacroCategory::Muscle},
    {"minmuscle", MacroCategory::Muscle},
    {"minweight", MacroCategory::Weight},
    {"averageweight", MacroCategory::Weight},
    {"maxweight", MacroCategory::Weight},
    {"minheight", MacroCategory::Height},
    {"averageheight", MacroCategory::Height},
    {"maxheight", MacroCategory::Height},
    {"mincup", MacroCategory::BreastSize},
    {"averagecup", MacroCategory::BreastSize},
    {"maxcup", MacroCategory::BreastSize},
    {"minfirmness", MacroCategory::BreastFirmness},
    {"averagefirmness", MacroCategory::BreastFirmness},
    {"maxfirmness", MacroCategory::BreastFirmness},
    {"uncommonproportions", MacroCategory::BodyProportions},
    {"regularproportions", MacroCategory::BodyProportions},
    {"idealproportions", MacroCategory::BodyProportions},
}};

float clamp01(float v) noexcept {
    return std::clamp(v, 0.0F, 1.0F);
}

void set(std::array<float, kMacroValueCount>& a, MacroValue v, float f) noexcept {
    a[static_cast<size_t>(v)] = f;
}

}  // namespace

std::string_view macroValueName(MacroValue v) noexcept {
    return kValues[static_cast<size_t>(v)].name;
}

MacroCategory categoryOf(MacroValue v) noexcept {
    return kValues[static_cast<size_t>(v)].category;
}

std::optional<MacroValue> macroValueFromToken(std::string_view token) noexcept {
    static const auto lookup = [] {
        std::unordered_map<std::string_view, MacroValue> m;
        m.reserve(kMacroValueCount);
        for (size_t i = 0; i < kMacroValueCount; ++i) {
            m.emplace(kValues[i].name, static_cast<MacroValue>(i));
        }
        return m;
    }();

    const auto it = lookup.find(token);
    if (it == lookup.end()) return std::nullopt;
    return it->second;
}

// --------------------------------------------------------------- setters ---
void MacroFactors::setGender(float v) {
    gender_ = clamp01(v);
    setGenderVals();
}

void MacroFactors::setAge(float v) {
    age_ = clamp01(v);
    setAgeVals();
}

void MacroFactors::setMuscle(float v) {
    muscle_ = clamp01(v);
    setMuscleVals();
}

void MacroFactors::setWeight(float v) {
    weight_ = clamp01(v);
    setWeightVals();
}

void MacroFactors::setHeight(float v) {
    height_ = clamp01(v);
    setHeightVals();
}

void MacroFactors::setBreastSize(float v) {
    breastSize_ = clamp01(v);
    setBreastSizeVals();
}

void MacroFactors::setBreastFirmness(float v) {
    breastFirmness_ = clamp01(v);
    setBreastFirmnessVals();
}

void MacroFactors::setBodyProportions(float v) {
    bodyProportions_ = clamp01(v);
    setBodyProportionVals();
}

void MacroFactors::setCaucasian(float v) {
    caucasian_ = clamp01(v);
    setEthnicVals(MacroValue::Caucasian);
}

void MacroFactors::setAsian(float v) {
    asian_ = clamp01(v);
    setEthnicVals(MacroValue::Asian);
}

void MacroFactors::setAfrican(float v) {
    african_ = clamp01(v);
    setEthnicVals(MacroValue::African);
}

// ---------------------------------------------------------- derivations ---
void MacroFactors::setGenderVals() {
    // human.py:517-519
    set(values_, MacroValue::Male, gender_);
    set(values_, MacroValue::Female, 1.0F - gender_);
}

void MacroFactors::setAgeVals() {
    // human.py:574-600. The piecewise map is
    //   1y      10y      25y            90y
    //   baby   child    young           old
    //   |--------|--------|--------------|
    //   0     0.1875     0.5             1
    if (age_ < 0.5F) {
        set(values_, MacroValue::Old, 0.0F);
        const float baby  = std::max(0.0F, 1.0F - age_ * 5.333F);     // 1/0.1875
        const float young = std::max(0.0F, (age_ - 0.1875F) * 3.2F);  // 1/(0.5-0.1875)
        const float child = std::max(0.0F, std::min(1.0F, 5.333F * age_) - young);
        set(values_, MacroValue::Baby, baby);
        set(values_, MacroValue::Young, young);
        set(values_, MacroValue::Child, child);
    } else {
        set(values_, MacroValue::Child, 0.0F);
        set(values_, MacroValue::Baby, 0.0F);
        const float old = std::max(0.0F, age_ * 2.0F - 1.0F);
        set(values_, MacroValue::Old, old);
        set(values_, MacroValue::Young, 1.0F - old);
    }
}

void MacroFactors::setMuscleVals() {
    // human.py:672-675. Note the average uses 1 - (max + min), NOT the if/else
    // form used by height/cup/firmness/proportions below. Identical in result;
    // reproduced as written.
    const float mx = std::max(0.0F, muscle_ * 2.0F - 1.0F);
    const float mn = std::max(0.0F, 1.0F - muscle_ * 2.0F);
    set(values_, MacroValue::MaxMuscle, mx);
    set(values_, MacroValue::MinMuscle, mn);
    set(values_, MacroValue::AverageMuscle, 1.0F - (mx + mn));
}

void MacroFactors::setWeightVals() {
    // human.py:640-643
    const float mx = std::max(0.0F, weight_ * 2.0F - 1.0F);
    const float mn = std::max(0.0F, 1.0F - weight_ * 2.0F);
    set(values_, MacroValue::MaxWeight, mx);
    set(values_, MacroValue::MinWeight, mn);
    set(values_, MacroValue::AverageWeight, 1.0F - (mx + mn));
}

void MacroFactors::setHeightVals() {
    // human.py:708-714
    const float mx = std::max(0.0F, height_ * 2.0F - 1.0F);
    const float mn = std::max(0.0F, 1.0F - height_ * 2.0F);
    set(values_, MacroValue::MaxHeight, mx);
    set(values_, MacroValue::MinHeight, mn);
    set(values_, MacroValue::AverageHeight, (mx > mn) ? 1.0F - mx : 1.0F - mn);
}

void MacroFactors::setBreastSizeVals() {
    // human.py:733-739
    const float mx = std::max(0.0F, breastSize_ * 2.0F - 1.0F);
    const float mn = std::max(0.0F, 1.0F - breastSize_ * 2.0F);
    set(values_, MacroValue::MaxCup, mx);
    set(values_, MacroValue::MinCup, mn);
    set(values_, MacroValue::AverageCup, (mx > mn) ? 1.0F - mx : 1.0F - mn);
}

void MacroFactors::setBreastFirmnessVals() {
    // human.py:758-764
    const float mx = std::max(0.0F, breastFirmness_ * 2.0F - 1.0F);
    const float mn = std::max(0.0F, 1.0F - breastFirmness_ * 2.0F);
    set(values_, MacroValue::MaxFirmness, mx);
    set(values_, MacroValue::MinFirmness, mn);
    set(values_, MacroValue::AverageFirmness, (mx > mn) ? 1.0F - mx : 1.0F - mn);
}

void MacroFactors::setBodyProportionVals() {
    // human.py:781-787
    const float ideal    = std::max(0.0F, bodyProportions_ * 2.0F - 1.0F);
    const float uncommon = std::max(0.0F, 1.0F - bodyProportions_ * 2.0F);
    set(values_, MacroValue::IdealProportions, ideal);
    set(values_, MacroValue::UncommonProportions, uncommon);
    set(values_, MacroValue::RegularProportions,
        (ideal > uncommon) ? 1.0F - ideal : 1.0F - uncommon);
}

void MacroFactors::writeEthnicValues() {
    set(values_, MacroValue::Caucasian, caucasian_);
    set(values_, MacroValue::Asian, asian_);
    set(values_, MacroValue::African, african_);
}

void MacroFactors::setEthnicVals(std::optional<MacroValue> exclude) {
    // human.py:847-888. Renormalises so the three sum to 1, holding `exclude`
    // fixed, with three degenerate branches when the others sum to zero.
    auto ref = [&](MacroValue v) -> float& {
        switch (v) {
            case MacroValue::Caucasian: return caucasian_;
            case MacroValue::Asian: return asian_;
            default: return african_;
        }
    };

    constexpr std::array<MacroValue, 3> kAll{MacroValue::African, MacroValue::Asian,
                                             MacroValue::Caucasian};

    float others   = 0.0F;
    int otherCount = 0;
    for (const MacroValue e : kAll) {
        if (exclude && e == *exclude) continue;
        others += ref(e);
        ++otherCount;
    }
    const float remaining = exclude ? (1.0F - ref(*exclude)) : 1.0F;

    if (others == 0.0F) {
        const auto closeTo = [](float v, float limit) { return std::abs(v - limit) <= 0.001F; };

        if (otherCount == 3 || (exclude && ref(*exclude) == 0.0F)) {
            // All zero: cannot be. Reset to the default third each.
            for (const MacroValue e : kAll)
                ref(e) = 1.0F / 3.0F;
        } else if (exclude && closeTo(ref(*exclude), 1.0F)) {
            // One ethnicity is 1, the rest 0.
            for (const MacroValue e : kAll) {
                if (e != *exclude) ref(e) = 0.0F;
            }
            ref(*exclude) = 1.0F;
        } else {
            // Nudge the zeroed others up, then renormalise.
            for (const MacroValue e : kAll) {
                if (!exclude || e != *exclude) ref(e) = 0.01F;
            }
            setEthnicVals(exclude);
            return;
        }
    } else {
        for (const MacroValue e : kAll) {
            if (exclude && e == *exclude) continue;
            ref(e) = remaining * (ref(e) / others);
        }
    }
    writeEthnicValues();
}

float MacroFactors::ageYears() const noexcept {
    // human.py:552-559
    return (age_ < 0.5F) ? (1.0F + age_ * 48.0F) : (25.0F + (age_ - 0.5F) * 130.0F);
}

void MacroFactors::recomputeAll() {
    setGenderVals();
    setAgeVals();
    setMuscleVals();
    setWeightVals();
    setHeightVals();
    setBreastSizeVals();
    setBreastFirmnessVals();
    setBodyProportionVals();
    writeEthnicValues();
}

}  // namespace mh::core
