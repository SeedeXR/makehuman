// SPDX-License-Identifier: AGPL-3.0-or-later
//
// The reference's Symmetry toolbar (`core/mhmain.py:1516-1522`), whose two
// one-shot commands are `Human.symmetrize` (`apps/human.py:1238-1264`).
//
// The direction letter names the TARGET side, and the reference's own naming
// crosses over: `applySymmetryRight()` calls `symmetrize('r')`, which copies
// every LEFT modifier onto its right counterpart. Getting that backwards
// produces a character that is still symmetric and still plausible, which is
// why the tests below pin the source values rather than only the equality.
#include "makehuman/core/Modifier.h"
#include "makehuman/core/Symmetry.h"
#include "makehuman/core/TargetIndex.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <algorithm>
#include <filesystem>
#include <string>
#include <vector>

using namespace mh::core;
using Catch::Matchers::WithinAbs;

namespace {

constexpr auto kLeft   = "eyes/l-eye-bag-decr|incr";
constexpr auto kRight  = "eyes/r-eye-bag-decr|incr";
constexpr auto kMiddle = "head/head-age-decr|incr";

std::vector<Modifier> shippedModifiers() {
    auto mods =
        loadModifiers(std::filesystem::path(MH_DATA_DIR) / "modifiers" / "modeling_modifiers.json");
    REQUIRE(mods.has_value());
    return std::move(*mods);
}

bool mentions(const std::vector<std::pair<std::string, float>>& changes, std::string_view name) {
    return std::ranges::any_of(changes, [&](const auto& c) { return c.first == name; });
}

}  // namespace

TEST_CASE("symmetrise copies the named side's opposite onto it", "[symmetry]") {
    const TargetIndex index = TargetIndex::build(std::filesystem::path(MH_DATA_DIR) / "targets");
    Human human(&index, shippedModifiers());

    REQUIRE(human.setModifierValue(kLeft, 0.8F));
    REQUIRE(human.setModifierValue(kRight, -0.3F));
    REQUIRE(human.setModifierValue(kMiddle, 0.5F));

    // 'r' is the TARGET: left is copied onto right, and the LEFT value is what
    // both must end up at. Asserting only that the two agree would pass on an
    // implementation that copied the wrong way.
    const auto changes = symmetrise(human, 'r');
    CHECK_THAT(human.modifierValue(kRight), WithinAbs(0.8, 1e-6));
    CHECK_THAT(human.modifierValue(kLeft), WithinAbs(0.8, 1e-6));

    // Only the target side is reported, and only what actually moved.
    CHECK(mentions(changes, kRight));
    CHECK_FALSE(mentions(changes, kLeft));

    // A modifier with no side is not part of any pair and must be left alone --
    // there are 291 of them and a symmetry command that reset them would wipe
    // the character.
    CHECK_THAT(human.modifierValue(kMiddle), WithinAbs(0.5, 1e-6));
    CHECK_FALSE(mentions(changes, kMiddle));
}

TEST_CASE("symmetrise the other way copies right onto left", "[symmetry]") {
    const TargetIndex index = TargetIndex::build(std::filesystem::path(MH_DATA_DIR) / "targets");
    Human human(&index, shippedModifiers());

    REQUIRE(human.setModifierValue(kLeft, 0.8F));
    REQUIRE(human.setModifierValue(kRight, -0.3F));

    const auto changes = symmetrise(human, 'l');
    CHECK_THAT(human.modifierValue(kLeft), WithinAbs(-0.3, 1e-6));
    CHECK_THAT(human.modifierValue(kRight), WithinAbs(-0.3, 1e-6));
    CHECK(mentions(changes, kLeft));
    CHECK_FALSE(mentions(changes, kRight));
}

TEST_CASE("symmetrise reports only what it changed, and settles", "[symmetry]") {
    const TargetIndex index = TargetIndex::build(std::filesystem::path(MH_DATA_DIR) / "targets");
    Human human(&index, shippedModifiers());

    // An untouched character is already symmetric, so there is nothing to do
    // and nothing to put on the undo stack.
    CHECK(symmetrise(human, 'r').empty());

    REQUIRE(human.setModifierValue(kLeft, 0.8F));
    const auto first = symmetrise(human, 'r');
    REQUIRE(first.size() == 1);
    CHECK(first.front().first == kRight);
    CHECK_THAT(first.front().second, WithinAbs(0.8, 1e-6));

    // Idempotent: running it again on an already mirrored character reports
    // nothing. A version that returned every pair it visited would give the
    // user an undo entry that undoes nothing.
    CHECK(symmetrise(human, 'r').empty());
}

TEST_CASE("symmetrise touches every side-bearing modifier, not just the first", "[symmetry]") {
    const TargetIndex index = TargetIndex::build(std::filesystem::path(MH_DATA_DIR) / "targets");
    Human human(&index, shippedModifiers());

    // The shipped set has 61 left-side modifiers. Moving all of them and
    // counting the result is what catches a loop that stops at the first pair.
    size_t leftSide = 0;
    for (const Modifier& m : human.modifiers()) {
        if (symmetrySide(m.name) != 'l') continue;
        ++leftSide;
        REQUIRE(human.setModifierValue(m.fullName, 0.4F));
    }
    REQUIRE(leftSide == 61);

    const auto changes = symmetrise(human, 'r');
    CHECK(changes.size() == leftSide);
    for (const auto& [name, value] : changes) {
        CAPTURE(name);
        CHECK(symmetrySide(name.substr(name.find('/') + 1)) == 'r');
        CHECK_THAT(value, WithinAbs(0.4, 1e-6));
    }
}

TEST_CASE("a direction that is neither side does nothing", "[symmetry]") {
    const TargetIndex index = TargetIndex::build(std::filesystem::path(MH_DATA_DIR) / "targets");
    Human human(&index, shippedModifiers());
    REQUIRE(human.setModifierValue(kLeft, 0.8F));

    // Without the guard, "anything that is not 'l'" reads as 'r' and a typo
    // silently mirrors the character in the direction the caller did not ask
    // for -- which looks like a working command.
    CHECK(symmetrise(human, 'x').empty());
    CHECK_THAT(human.modifierValue(kRight), WithinAbs(0.0, 1e-6));
    CHECK_THAT(human.modifierValue(kLeft), WithinAbs(0.8, 1e-6));
}

TEST_CASE("a modifier whose opposite is not installed is skipped", "[symmetry]") {
    const TargetIndex index = TargetIndex::build(std::filesystem::path(MH_DATA_DIR) / "targets");

    // One half of a pair, on its own. `findModifier` returns null for the
    // opposite, and the reference would raise here (`human.py:1262` calls
    // getModifier and dereferences it unconditionally).
    std::vector<Modifier> lonely;
    for (Modifier& m : shippedModifiers()) {
        if (m.fullName == kLeft) lonely.push_back(std::move(m));
    }
    REQUIRE(lonely.size() == 1);
    Human human(&index, std::move(lonely));
    REQUIRE(human.setModifierValue(kLeft, 0.8F));

    CHECK(symmetrise(human, 'r').empty());
    CHECK_THAT(human.modifierValue(kLeft), WithinAbs(0.8, 1e-6));
}

// Symmetry MODE: the reference's third symmetry button
// (`symmetryModeEnabled`, `core/mhmain.py:1526`). Unlike the two one-shot
// commands above it does not mirror the whole character -- it mirrors the ONE
// modifier the user is dragging, as they drag it.
//
// The reference puts this in the undoable ACTION rather than in `setValue`
// (`apps/humanmodifier.py:120-129`), and its randomiser switches the mode off
// while assigning values (`0_modeling_8_random.py:60-68`). Both say the same
// thing: this is a rule about a user EDIT, not about the model. So it is a
// function of an edit, and the batch paths simply never call it.
TEST_CASE("a mirrored edit carries the opposite side with it", "[symmetry]") {
    const TargetIndex index = TargetIndex::build(std::filesystem::path(MH_DATA_DIR) / "targets");
    Human human(&index, shippedModifiers());

    const auto edit = mirroredEdit(human, kLeft, 0.7F);
    REQUIRE(edit.size() == 2);
    // The edited modifier first: a caller that applies these in order must set
    // what the user touched before anything derived from it.
    CHECK(edit[0].first == kLeft);
    CHECK_THAT(edit[0].second, WithinAbs(0.7, 1e-6));
    CHECK(edit[1].first == kRight);
    CHECK_THAT(edit[1].second, WithinAbs(0.7, 1e-6));
}

TEST_CASE("an edit with no opposite is just itself", "[symmetry]") {
    const TargetIndex index = TargetIndex::build(std::filesystem::path(MH_DATA_DIR) / "targets");
    Human human(&index, shippedModifiers());

    // 291 of the shipped modifiers have no side. Mirroring one would have to
    // invent a target, and the reference guards on `getSymmetricOpposite()`
    // being non-empty for exactly that reason.
    const auto edit = mirroredEdit(human, kMiddle, 0.3F);
    REQUIRE(edit.size() == 1);
    CHECK(edit[0].first == kMiddle);
    CHECK_THAT(edit[0].second, WithinAbs(0.3, 1e-6));

    // A name this character does not have is not an edit at all.
    CHECK(mirroredEdit(human, "nosuch/modifier", 0.5F).empty());
}

TEST_CASE("mirroring reads the CURRENT edit, not the stored value", "[symmetry]") {
    const TargetIndex index = TargetIndex::build(std::filesystem::path(MH_DATA_DIR) / "targets");
    Human human(&index, shippedModifiers());
    REQUIRE(human.setModifierValue(kLeft, -0.9F));

    // The opposite takes the value being SET, not the one the modifier still
    // holds. Reading `human` instead would mirror the previous frame of a drag
    // and leave the two sides one step apart for the whole gesture.
    const auto edit = mirroredEdit(human, kLeft, 0.25F);
    REQUIRE(edit.size() == 2);
    CHECK_THAT(edit[1].second, WithinAbs(0.25, 1e-6));

    // And it does not APPLY anything -- the caller owns that, because the undo
    // stack has to record both values before either moves.
    CHECK_THAT(human.modifierValue(kLeft), WithinAbs(-0.9, 1e-6));
    CHECK_THAT(human.modifierValue(kRight), WithinAbs(0.0, 1e-6));
}
