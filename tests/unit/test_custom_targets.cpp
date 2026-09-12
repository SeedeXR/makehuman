// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Custom targets: a user's own `.target` morphs, kept wherever they keep them
// rather than under `data/targets`.
//
// The reference wraps each file in a `SimpleModifier('custom', dir, file)`
// (`plugins/0_modeling_9_custom_targets.py:199`), which names a FILE instead of
// resolving a target GROUP the way every shipped slider does. That is the whole
// difference, and it is why `ModifierKind::Simple` exists: a group lookup has
// nothing to find for a file that is not in the index.

#include "makehuman/core/Modifier.h"
#include "makehuman/core/Target.h"
#include "makehuman/core/TargetIndex.h"

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>

using namespace mh::core;

namespace {

/// A minimal but REAL target: two vertices that actually move.
///
/// Written to a temp directory rather than into `data/`, because being outside
/// the target root is the entire point of the feature.
std::filesystem::path writeCustomTarget(const std::string& stem) {
    const auto dir = std::filesystem::temp_directory_path() / "mh-custom-targets";
    std::filesystem::create_directories(dir);
    const auto file = dir / (stem + ".target");
    std::ofstream out(file);
    out << "0 0.100000 0.000000 0.000000\n";
    out << "1 0.000000 0.200000 0.000000\n";
    return file;
}

}  // namespace

TEST_CASE("a custom target directory becomes one modifier per file", "[core][custom]") {
    const auto file = writeCustomTarget("bulge");
    const auto mods = customModifiers(file.parent_path());

    REQUIRE(!mods.empty());
    const auto it = std::ranges::find_if(mods, [](const Modifier& m) { return m.name == "bulge"; });
    REQUIRE(it != mods.end());

    CHECK(it->group == "custom");
    CHECK(it->fullName == "custom/bulge");
    CHECK(it->kind == ModifierKind::Simple);
    // A one-sided slider, exactly like the reference's SimpleModifier: there is
    // no "negative bulge" target to travel towards.
    CHECK(it->minValue() == 0.0F);
    CHECK(it->maxValue() == 1.0F);
    // Canonical, not as-written: `customModifiers` resolves the path so the
    // stack key is stable regardless of where the process started or whether a
    // `..` was involved. Comparing against the raw string would agree only on
    // machines whose temp directory happens not to be a symlink.
    CHECK(it->targetPath == std::filesystem::weakly_canonical(file).string());
}

TEST_CASE("a custom modifier puts its FILE in the stack, not a group", "[core][custom]") {
    const auto file         = writeCustomTarget("bulge");
    const TargetIndex index = TargetIndex::build(std::filesystem::path(MH_DATA_DIR) / "targets");

    Human human(&index, customModifiers(file.parent_path()));
    REQUIRE(human.setModifierValue("custom/bulge", 1.0F));

    // Keyed by the absolute path, because `TargetLibrary::get` resolves an
    // absolute key against the filesystem rather than against its root -- the
    // behaviour pinned by "a target OUTSIDE the root loads by absolute path".
    const auto& stack = human.stack();
    const auto entry  = stack.find(std::filesystem::weakly_canonical(file).string());
    REQUIRE(entry != stack.end());
    CHECK(entry->second == 1.0F);
}

TEST_CASE("a custom modifier at zero leaves the stack alone", "[core][custom]") {
    const auto file         = writeCustomTarget("bulge");
    const TargetIndex index = TargetIndex::build(std::filesystem::path(MH_DATA_DIR) / "targets");

    Human human(&index, customModifiers(file.parent_path()));
    const auto key = std::filesystem::weakly_canonical(file).string();
    // Never set: the default is 0, and a zero weight must never enter the
    // stack -- the same rule every shipped modifier follows.
    CHECK(human.stack().find(key) == human.stack().end());

    REQUIRE(human.setModifierValue("custom/bulge", 1.0F));
    REQUIRE(human.stack().find(key) != human.stack().end());
    REQUIRE(human.setModifierValue("custom/bulge", 0.0F));
    CHECK(human.stack().find(key) == human.stack().end());
}

TEST_CASE("an empty or missing custom directory yields no modifiers", "[core][custom]") {
    const auto empty = std::filesystem::temp_directory_path() / "mh-custom-empty";
    std::filesystem::create_directories(empty);
    CHECK(customModifiers(empty).empty());

    // Missing is the overwhelmingly common case -- most users never make one --
    // and it must be silent rather than an error.
    CHECK(customModifiers(empty / "definitely-not-here").empty());
}
