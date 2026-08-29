// SPDX-License-Identifier: AGPL-3.0-or-later
#include "makehuman/core/Macro.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <cmath>

using Catch::Matchers::WithinAbs;
using namespace mh::core;

TEST_CASE("the macro value table matches the reference", "[core][macro]") {
    // legacy/python/lib/targets.py:50-61. Counted from the reference itself:
    //   len(targets._value_cat) == 27, len(targets._categories) == 9.
    // (An earlier note in memory/ said 26; that was wrong -- age has FOUR
    //  values, not three, which is what the extra one is.)
    CHECK(kMacroValueCount == 27);
    CHECK(kMacroCategoryCount == 9);

    CHECK(macroValueName(MacroValue::Male) == "male");
    CHECK(macroValueName(MacroValue::AverageMuscle) == "averagemuscle");
    CHECK(macroValueName(MacroValue::UncommonProportions) == "uncommonproportions");

    CHECK(categoryOf(MacroValue::Baby) == MacroCategory::Age);
    CHECK(categoryOf(MacroValue::MaxCup) == MacroCategory::BreastSize);
}

TEST_CASE("filename tokens resolve to macro values", "[core][macro]") {
    // This is what makes a target's filename its parameterisation
    // (targets.py:203-215).
    CHECK(macroValueFromToken("female") == MacroValue::Female);
    CHECK(macroValueFromToken("maxheight") == MacroValue::MaxHeight);
    CHECK_FALSE(macroValueFromToken("head").has_value());
    CHECK_FALSE(macroValueFromToken("").has_value());
    CHECK_FALSE(macroValueFromToken("Male").has_value());  // case-sensitive
}

TEST_CASE("defaults are the reference's neutral character", "[core][macro]") {
    const MacroFactors f;
    CHECK_THAT(f.gender(), WithinAbs(0.5, 1e-6));
    CHECK_THAT(f.age(), WithinAbs(0.5, 1e-6));
    CHECK_THAT(f.caucasian(), WithinAbs(1.0 / 3.0, 1e-6));

    // At the default the character is exactly "young", half male/female.
    CHECK_THAT(f.value(MacroValue::Young), WithinAbs(1.0, 1e-5));
    CHECK_THAT(f.value(MacroValue::Old), WithinAbs(0.0, 1e-6));
    CHECK_THAT(f.value(MacroValue::Male), WithinAbs(0.5, 1e-6));
    CHECK_THAT(f.value(MacroValue::Female), WithinAbs(0.5, 1e-6));
}

TEST_CASE("each category's values sum to one", "[core][macro]") {
    // The weighting rule is a separable multilinear interpolation over a
    // 1-of-N simplex per category, so each category must be a partition.
    MacroFactors f;
    for (const float s : {0.0F, 0.15F, 0.1875F, 0.3F, 0.5F, 0.7F, 1.0F}) {
        f.setGender(s);
        f.setAge(s);
        f.setMuscle(s);
        f.setWeight(s);
        f.setHeight(s);
        f.setBreastSize(s);
        f.setBreastFirmness(s);
        f.setBodyProportions(s);

        std::array<float, kMacroCategoryCount> sums{};
        for (size_t i = 0; i < kMacroValueCount; ++i) {
            const auto v = static_cast<MacroValue>(i);
            sums[static_cast<size_t>(categoryOf(v))] += f.value(v);
        }
        for (size_t c = 0; c < kMacroCategoryCount; ++c) {
            INFO("scalar " << s << " category " << c);
            CHECK_THAT(sums[c], WithinAbs(1.0, 1e-4));
        }
    }
}

TEST_CASE("the age curve hits its documented breakpoints", "[core][macro]") {
    // human.py:574-600: baby at 0, child at 0.1875, young at 0.5, old at 1.
    MacroFactors f;

    f.setAge(0.0F);
    CHECK_THAT(f.value(MacroValue::Baby), WithinAbs(1.0, 1e-4));

    f.setAge(0.1875F);
    CHECK_THAT(f.value(MacroValue::Child), WithinAbs(1.0, 1e-3));

    f.setAge(0.5F);
    CHECK_THAT(f.value(MacroValue::Young), WithinAbs(1.0, 1e-5));

    f.setAge(1.0F);
    CHECK_THAT(f.value(MacroValue::Old), WithinAbs(1.0, 1e-5));
}

TEST_CASE("age maps to years as the reference does", "[core][macro]") {
    // human.py:552-559
    MacroFactors f;
    f.setAge(0.0F);
    CHECK_THAT(f.ageYears(), WithinAbs(1.0, 1e-4));
    f.setAge(0.5F);
    CHECK_THAT(f.ageYears(), WithinAbs(25.0, 1e-4));
    f.setAge(1.0F);
    CHECK_THAT(f.ageYears(), WithinAbs(90.0, 1e-3));
}

TEST_CASE("scalars are clamped to [0,1]", "[core][macro]") {
    MacroFactors f;
    f.setGender(5.0F);
    CHECK_THAT(f.gender(), WithinAbs(1.0, 1e-6));
    f.setGender(-2.0F);
    CHECK_THAT(f.gender(), WithinAbs(0.0, 1e-6));
}

TEST_CASE("ethnic values renormalise to sum one", "[core][macro]") {
    // human.py:847-888
    MacroFactors f;
    f.setAfrican(1.0F);
    CHECK_THAT(f.african(), WithinAbs(1.0, 1e-5));
    CHECK_THAT(f.asian() + f.caucasian(), WithinAbs(0.0, 1e-5));

    f.setAsian(0.5F);
    CHECK_THAT(f.caucasian() + f.asian() + f.african(), WithinAbs(1.0, 1e-5));
    CHECK_THAT(f.asian(), WithinAbs(0.5, 1e-5));
}

TEST_CASE("setting one ethnicity to zero redistributes the rest", "[core][macro]") {
    MacroFactors f;
    f.setCaucasian(0.0F);
    CHECK_THAT(f.caucasian(), WithinAbs(0.0, 1e-5));
    CHECK_THAT(f.asian() + f.african(), WithinAbs(1.0, 1e-5));
}

TEST_CASE("the all-zero ethnic degenerate branch resets to thirds", "[core][macro]") {
    // human.py:868-873 -- "All values 0, this cannot be. Reset to default."
    MacroFactors f;
    f.setAfrican(0.0F);
    f.setAsian(0.0F);
    f.setCaucasian(0.0F);
    CHECK_THAT(f.caucasian() + f.asian() + f.african(), WithinAbs(1.0, 1e-4));
}

TEST_CASE("ethnic values are mirrored into the factor array", "[core][macro]") {
    MacroFactors f;
    f.setAsian(0.6F);
    CHECK_THAT(f.value(MacroValue::Asian), WithinAbs(static_cast<double>(f.asian()), 1e-6));
    CHECK_THAT(f.value(MacroValue::African), WithinAbs(static_cast<double>(f.african()), 1e-6));
    CHECK_THAT(f.value(MacroValue::Caucasian), WithinAbs(static_cast<double>(f.caucasian()), 1e-6));
}

TEST_CASE("muscle and weight use the sum form, height the if/else form", "[core][macro]") {
    // The reference genuinely writes these two differently (human.py:640-643
    // and :672-675 vs :708-714). Identical in result -- at most one of max/min
    // is non-zero -- but reproduced as written, so assert both give a partition.
    MacroFactors f;
    for (const float s : {0.0F, 0.25F, 0.5F, 0.75F, 1.0F}) {
        f.setMuscle(s);
        f.setHeight(s);
        CHECK_THAT(f.value(MacroValue::MinMuscle) + f.value(MacroValue::AverageMuscle) +
                       f.value(MacroValue::MaxMuscle),
                   WithinAbs(1.0, 1e-5));
        CHECK_THAT(f.value(MacroValue::MinHeight) + f.value(MacroValue::AverageHeight) +
                       f.value(MacroValue::MaxHeight),
                   WithinAbs(1.0, 1e-5));
    }
}
