// SPDX-License-Identifier: AGPL-3.0-or-later
#include "makehuman/core/TargetIndex.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <filesystem>
#include <fstream>

using Catch::Matchers::WithinAbs;
using namespace mh::core;

namespace {

/// A throwaway target tree, for the tokenisation cases the shipped data does
/// not happen to contain.
class TempTree {
public:
    TempTree() {
        static int counter = 0;
        root_ = std::filesystem::temp_directory_path() / ("mh_idx_" + std::to_string(++counter));
        std::filesystem::create_directories(root_);
    }

    ~TempTree() {
        std::error_code ec;
        std::filesystem::remove_all(root_, ec);
    }

    TempTree(const TempTree&)            = delete;
    TempTree& operator=(const TempTree&) = delete;

    void add(const std::string& relative) const {
        const auto p = root_ / relative;
        std::filesystem::create_directories(p.parent_path());
        std::ofstream out(p);
        out << "0 0 0 0\n";
    }

    [[nodiscard]] const std::filesystem::path& root() const { return root_; }

private:
    std::filesystem::path root_;
};

}  // namespace

TEST_CASE("a leading 'targets' directory is dropped from the key", "[core][index]") {
    // targets.py:205-214 -- the shortcut that keeps data/targets/foo keyed
    // "foo" rather than "targets-foo".
    const TempTree t;
    t.add("targets/head/head-oval.target");
    const auto idx = TargetIndex::build(t.root());

    CHECK(idx.componentCount() == 1);
    CHECK(idx.group("head-head-oval").size() == 1);
    CHECK(idx.group("targets-head-head-oval").empty());
}

TEST_CASE("a non-targets top directory stays in the key", "[core][index]") {
    // The reference notes this explicitly: a .target elsewhere in data/ keeps
    // its containing folder, e.g. "poses-..." (targets.py:209-213).
    const TempTree t;
    t.add("poses/standing.target");
    const auto idx = TargetIndex::build(t.root());
    CHECK(idx.group("poses-standing").size() == 1);
}

TEST_CASE("the literal 'target' token is dropped, not keyed", "[core][index]") {
    // The extension splits to a token because '.' is a separator
    // (targets.py:128-129).
    const TempTree t;
    t.add("targets/head/oval.target");
    const auto idx = TargetIndex::build(t.root());
    CHECK(idx.group("head-oval").size() == 1);
    CHECK(idx.group("head-oval-target").empty());
}

TEST_CASE("underscores and dots separate like dashes", "[core][index]") {
    const TempTree t;
    t.add("targets/head/big_nose.target");
    const auto idx = TargetIndex::build(t.root());
    CHECK(idx.group("head-big-nose").size() == 1);
}

TEST_CASE("macro tokens become dependencies, not key parts", "[core][index]") {
    const TempTree t;
    t.add("targets/macrodetails/african-female-baby.target");
    const auto idx = TargetIndex::build(t.root());

    const auto g = idx.group("macrodetails");
    REQUIRE(g.size() == 1);
    CHECK(g[0].variables().size() == 3);
    CHECK(g[0].data[static_cast<size_t>(MacroCategory::Race)] == MacroValue::African);
    CHECK(g[0].data[static_cast<size_t>(MacroCategory::Gender)] == MacroValue::Female);
    CHECK(g[0].data[static_cast<size_t>(MacroCategory::Age)] == MacroValue::Baby);
}

TEST_CASE("a target claiming one category twice is rejected", "[core][index]") {
    // targets.py:112-113 raises RuntimeError. Nothing in the shipped set does
    // this, so only a synthetic file reaches it.
    const TempTree t;
    t.add("targets/macrodetails/male-female-young.target");
    t.add("targets/macrodetails/male-young.target");
    const auto idx = TargetIndex::build(t.root());

    // The conflicting file is skipped; the valid one still indexes.
    CHECK(idx.componentCount() == 1);
    CHECK(idx.group("macrodetails").size() == 1);
}

TEST_CASE("the same category with the same value twice is fine", "[core][index]") {
    const TempTree t;
    t.add("targets/macrodetails/male-male-young.target");
    const auto idx = TargetIndex::build(t.root());
    CHECK(idx.componentCount() == 1);
}

TEST_CASE("weight is value times the product of factors", "[core][index]") {
    // humanmodifier.py:644-652
    const TempTree t;
    t.add("targets/macrodetails/african-female-baby.target");
    const auto idx = TargetIndex::build(t.root());
    const auto g   = idx.group("macrodetails");
    REQUIRE(g.size() == 1);

    MacroFactors f;
    f.setGender(0.0F);   // female = 1
    f.setAge(0.0F);      // baby = 1
    f.setAfrican(1.0F);  // african = 1
    CHECK_THAT(targetWeight(g[0], f), WithinAbs(1.0, 1e-4));

    f.setGender(1.0F);  // female = 0 -> the whole product collapses
    CHECK_THAT(targetWeight(g[0], f), WithinAbs(0.0, 1e-6));
}

TEST_CASE("the slider value and group factor both scale the weight", "[core][index]") {
    const TempTree t;
    t.add("targets/head/oval.target");
    const auto idx = TargetIndex::build(t.root());
    const auto g   = idx.group("head-oval");
    REQUIRE(g.size() == 1);

    // No macro dependencies, so weight = value * groupFactor.
    const MacroFactors f;
    CHECK_THAT(targetWeight(g[0], f, 0.5F, 1.0F), WithinAbs(0.5, 1e-6));
    CHECK_THAT(targetWeight(g[0], f, 1.0F, 0.25F), WithinAbs(0.25, 1e-6));
    CHECK_THAT(targetWeight(g[0], f, 0.5F, 0.5F), WithinAbs(0.25, 1e-6));
}

TEST_CASE("an empty or missing root yields an empty index", "[core][index]") {
    const auto idx = TargetIndex::build("/definitely/not/a/directory");
    CHECK(idx.groupCount() == 0);
    CHECK(idx.componentCount() == 0);
    CHECK(idx.group("anything").empty());
}
