// SPDX-License-Identifier: AGPL-3.0-or-later
//
// The randomiser, ported from `legacy/python/plugins/0_modeling_8_random.py`.
//
// It is seeded deliberately. A randomiser with no seed cannot be tested beyond
// "it did something", and a user who generates a character they like has no way
// back to it.

#include "makehuman/core/Modifier.h"
#include "makehuman/core/Random.h"
#include "makehuman/core/TargetIndex.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <map>
#include <set>

using namespace mh::core;
using Catch::Matchers::WithinAbs;

namespace {

std::vector<Modifier> shippedModifiers() {
    auto mods =
        loadModifiers(std::filesystem::path(MH_DATA_DIR) / "modifiers" / "modeling_modifiers.json");
    REQUIRE(mods.has_value());
    return std::move(*mods);
}

}  // namespace

// The `-`-separated component must be EXACTLY "l" or "r". A starts_with test
// would call `lowerface-...` a left-side modifier and pair it with a
// non-existent `rowerface-...`.
TEST_CASE("symmetry side reads a whole name component", "[random]") {
    CHECK(symmetrySide("l-eye-height") == 'l');
    CHECK(symmetrySide("r-eye-height") == 'r');
    CHECK(symmetrySide("eye-l-corner") == 'l');

    CHECK(symmetrySide("lowerface-scale") == '\0');
    CHECK(symmetrySide("round-shape") == '\0');  // contains 'r' but not as a component
    CHECK(symmetrySide("") == '\0');
    CHECK(symmetrySide("nose-scale-horiz-decr|incr") == '\0');
}

TEST_CASE("the symmetric opposite flips every side component", "[random]") {
    Modifier m;
    m.group = "eyes";
    m.name  = "l-eye-height1-decr|incr";
    CHECK(symmetricOpposite(m) == "eyes/r-eye-height1-decr|incr");

    m.name = "r-eye-height1-decr|incr";
    CHECK(symmetricOpposite(m) == "eyes/l-eye-height1-decr|incr");

    // No side, no opposite -- and it must return empty rather than the name
    // unchanged, or the caller would pair a modifier with itself.
    m.name = "nose-scale-horiz-decr|incr";
    CHECK(symmetricOpposite(m).empty());
}

// Reflection, not clamping. Clamping piles probability onto the endpoints, so a
// run of characters would share identical extreme features; reflecting folds it
// back inside. Reference: 0_modeling_8_random.py:184-192.
TEST_CASE("a random value stays inside its range", "[random]") {
    uint64_t state       = 12345;
    size_t atABound      = 0;
    constexpr int kDraws = 2000;
    for (int i = 0; i < kDraws; ++i) {
        // A wide sigma so draws land outside constantly and the reflection is
        // actually exercised rather than being dead code in the test.
        const float v = randomValue(-1.0F, 1.0F, 0.0F, 2.0F, state);
        CAPTURE(i, v);
        REQUIRE(v >= -1.0F);
        REQUIRE(v <= 1.0F);
        REQUIRE(std::isfinite(v));
        if (v == -1.0F || v == 1.0F) ++atABound;
    }

    // THE BOUNDS CHECK ABOVE CANNOT TELL REFLECTION FROM CLAMPING -- verified by
    // mutation: deleting the reflection left every assertion above green,
    // because the clamp alone keeps values in range. The comment claiming
    // reflection mattered was doing no work.
    //
    // What separates them is WHERE the probability goes. Clamping piles every
    // out-of-range draw onto the two endpoints, so a run of characters shares
    // identical extreme features; reflection folds them back inside.
    //
    // Measured, at sigmaFactor 0.3 -- the real spread for macro modifiers:
    // reflecting lands on a bound 0.00% of the time, clamping 9.50%. At the 2.0
    // used above the two converge (45% vs 80%), because a draw more than a full
    // range out reflects past the OTHER bound and is clamped anyway -- which is
    // why this second loop exists rather than reusing the first.
    uint64_t spread  = 999;
    size_t onABound  = 0;
    constexpr int kN = 20000;
    for (int i = 0; i < kN; ++i) {
        const float v = randomValue(-1.0F, 1.0F, 0.0F, 0.3F, spread);
        if (v == -1.0F || v == 1.0F) ++onABound;
    }
    INFO("at sigma 0.3, draws exactly on a bound: " << onABound << " of " << kN);
    CHECK(onABound < static_cast<size_t>(kN) / 100);  // clamping would give ~1900
    (void)atABound;

    // A zero-width range has exactly one answer. normal_distribution with
    // sigma 0 is undefined behaviour, not a constant, so this is a real guard.
    uint64_t s2 = 7;
    CHECK_THAT(randomValue(0.5F, 0.5F, 0.5F, 0.2F, s2), WithinAbs(0.5, 1e-6));
}

TEST_CASE("randomisation is reproducible from its seed", "[random]") {
    const TargetIndex index = TargetIndex::build(std::filesystem::path(MH_DATA_DIR) / "targets");

    Human a(&index, shippedModifiers());
    Human b(&index, shippedModifiers());
    Human c(&index, shippedModifiers());

    const RandomOptions opts;
    const auto first = randomize(a, opts, 42);
    const auto same  = randomize(b, opts, 42);
    const auto other = randomize(c, opts, 43);

    REQUIRE_FALSE(first.empty());
    CHECK(first == same);

    // A different seed must give a different character, or the seed is not
    // reaching the generator at all.
    CHECK(first != other);
}

TEST_CASE("every randomised value is inside its modifier's range", "[random]") {
    const TargetIndex index = TargetIndex::build(std::filesystem::path(MH_DATA_DIR) / "targets");
    Human human(&index, shippedModifiers());

    RandomOptions opts;
    opts.height        = true;  // everything on, so nothing is left unexercised
    const auto applied = randomize(human, opts, 2026);
    REQUIRE(applied.size() > 20);

    for (const auto& [name, value] : applied) {
        CAPTURE(name, value);
        const Modifier* m = human.findModifier(name);
        REQUIRE(m != nullptr);
        REQUIRE(std::isfinite(value));
        REQUIRE(value >= m->minValue());
        REQUIRE(value <= m->maxValue());
        // And it actually reached the character, rather than only the map.
        REQUIRE_THAT(human.modifierValue(name), WithinAbs(static_cast<double>(value), 1e-5));
    }
}

TEST_CASE("full symmetry gives a symmetric face", "[random]") {
    const TargetIndex index = TargetIndex::build(std::filesystem::path(MH_DATA_DIR) / "targets");
    Human human(&index, shippedModifiers());

    RandomOptions opts;
    opts.symmetry      = 1.0F;
    const auto applied = randomize(human, opts, 99);

    std::map<std::string, float> byName(applied.begin(), applied.end());
    size_t pairsChecked = 0;
    for (const auto& [name, value] : byName) {
        const Modifier* m = human.findModifier(name);
        REQUIRE(m != nullptr);
        const std::string opposite = symmetricOpposite(*m);
        if (opposite.empty()) continue;
        const auto it = byName.find(opposite);
        if (it == byName.end()) continue;
        CAPTURE(name, opposite);
        CHECK_THAT(it->second, WithinAbs(static_cast<double>(value), 1e-6));
        ++pairsChecked;
    }
    // If this were 0 the test above would pass vacuously.
    INFO("symmetric pairs checked: " << pairsChecked);
    CHECK(pairsChecked > 10);
}

// CLAUDE.md hard rule 3: never port a known-broken behaviour, and say so.
//
// The reference's pregnancy guard is
//     if Gender > 0.5 or Age < 0.2 or Age < 0.75:
// while its own comment says "No pregnancy for male, too young or TOO OLD
// subjects". The third clause is `<` where the comment means `>`, so it fires
// for every age below 0.75 -- pregnancy is zeroed on nearly every character and
// the second clause is entirely redundant. We implement the stated intent.
TEST_CASE("pregnancy is suppressed for the stated reasons, not the shipped bug",
          "[random][divergence]") {
    const TargetIndex index = TargetIndex::build(std::filesystem::path(MH_DATA_DIR) / "targets");

    // A seed where the guard does NOT fire must be able to leave pregnancy
    // non-zero. Under the reference's condition this is unreachable for any
    // character with Age < 0.75, which is nearly all of them.
    bool sawNonZeroPregnancy = false;
    for (uint64_t seed = 0; seed < 60 && !sawNonZeroPregnancy; ++seed) {
        Human human(&index, shippedModifiers());
        RandomOptions opts;
        const auto applied = randomize(human, opts, seed);
        std::map<std::string, float> byName(applied.begin(), applied.end());

        const auto preg = byName.find("stomach/stomach-pregnant-decr|incr");
        if (preg == byName.end()) continue;
        const float gender =
            byName.count("macrodetails/Gender") != 0 ? byName["macrodetails/Gender"] : 0.0F;
        const float age = byName.count("macrodetails/Age") != 0 ? byName["macrodetails/Age"] : 0.5F;

        if (gender > 0.5F || age < 0.2F || age > 0.75F) {
            // The guard should have fired.
            CAPTURE(seed, gender, age);
            CHECK_THAT(preg->second, WithinAbs(0.0, 1e-6));
        } else if (preg->second != 0.0F) {
            sawNonZeroPregnancy = true;
        }
    }
    INFO("a character in range kept a non-zero pregnancy value");
    CHECK(sawNonZeroPregnancy);
}

TEST_CASE("the option flags select what changes", "[random]") {
    const TargetIndex index = TargetIndex::build(std::filesystem::path(MH_DATA_DIR) / "targets");

    const auto groupsTouched = [&index](const RandomOptions& o) {
        Human human(&index, shippedModifiers());
        std::set<std::string> groups;
        for (const auto& [name, value] : randomize(human, o, 7)) {
            groups.insert(name.substr(0, name.find('/')));
        }
        return groups;
    };

    RandomOptions faceOnly;
    faceOnly.macro  = false;
    faceOnly.body   = false;
    const auto face = groupsTouched(faceOnly);
    REQUIRE_FALSE(face.empty());
    CHECK(face.contains("nose"));
    CHECK_FALSE(face.contains("macrodetails"));
    CHECK_FALSE(face.contains("torso"));

    RandomOptions macroOnly;
    macroOnly.face   = false;
    macroOnly.body   = false;
    const auto macro = groupsTouched(macroOnly);
    CHECK(macro.contains("macrodetails"));
    CHECK_FALSE(macro.contains("nose"));

    // height is OFF by default and separate from macro: a randomiser that
    // changes stature is unwanted while iterating on a face.
    CHECK_FALSE(macro.contains("macrodetails-height"));
    RandomOptions withHeight;
    withHeight.height = true;
    CHECK(groupsTouched(withHeight).contains("macrodetails-height"));
}
