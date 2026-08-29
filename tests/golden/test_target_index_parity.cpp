// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Parity of the target index against the Python reference: every group key and
// every group's size, over the whole shipped target set.
//
// tests/golden/target_groups.txt is generated from the reference's own
// targets.getTargets().groups (legacy/python/lib/targets.py:188-227).

#include "makehuman/core/TargetIndex.h"

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <map>
#include <string>

using namespace mh::core;

namespace {

std::map<std::string, size_t> loadExpected() {
    std::map<std::string, size_t> out;
    std::ifstream in(std::filesystem::path(MH_GOLDEN_DIR) / "target_groups.txt");
    std::string line;
    while (std::getline(in, line)) {
        const size_t tab = line.find('\t');
        if (tab == std::string::npos) continue;
        out[line.substr(0, tab)] = std::stoul(line.substr(tab + 1));
    }
    return out;
}

}  // namespace

TEST_CASE("every target group matches the Python reference", "[golden][parity][fixture][index]") {
    const auto expected = loadExpected();
    if (expected.empty()) SKIP("target_groups.txt not present");

    const auto idx = TargetIndex::build(MH_DATA_DIR);
    if (idx.componentCount() == 0) SKIP("target data not present");

    // Measured from the reference: 653 groups over 1,280 targets.
    CHECK(expected.size() == 653);
    CHECK(idx.groupCount() == expected.size());
    CHECK(idx.componentCount() == 1280);

    // Every group the reference has, we have, at the same size.
    size_t missing   = 0;
    size_t wrongSize = 0;
    for (const auto& [name, count] : expected) {
        const auto g = idx.group(name);
        if (g.empty() && count > 0) {
            ++missing;
            UNSCOPED_INFO("missing group: " << name);
            continue;
        }
        if (g.size() != count) {
            ++wrongSize;
            UNSCOPED_INFO("group " << name << ": got " << g.size() << " want " << count);
        }
    }
    CHECK(missing == 0);
    CHECK(wrongSize == 0);

    // ...and no group we invented that the reference does not have.
    size_t extra = 0;
    for (const std::string& name : idx.groupNames()) {
        if (!expected.contains(name)) {
            ++extra;
            UNSCOPED_INFO("unexpected group: " << name);
        }
    }
    CHECK(extra == 0);
}

TEST_CASE("macro dependencies are extracted from filenames", "[golden][parity][fixture][index]") {
    const auto idx = TargetIndex::build(MH_DATA_DIR);
    if (idx.componentCount() == 0) SKIP("target data not present");

    // macrodetails: race x gender x age = 3 x 2 x 4 = 24, each with exactly
    // those three dependencies and nothing else.
    const auto mac = idx.group("macrodetails");
    REQUIRE(mac.size() == 24);
    for (const TargetComponent& c : mac) {
        INFO(c.relativePath);
        CHECK(c.variables().size() == 3);
        CHECK(c.data[static_cast<size_t>(MacroCategory::Race)].has_value());
        CHECK(c.data[static_cast<size_t>(MacroCategory::Gender)].has_value());
        CHECK(c.data[static_cast<size_t>(MacroCategory::Age)].has_value());
        CHECK_FALSE(c.data[static_cast<size_t>(MacroCategory::Muscle)].has_value());
    }

    // breast: gender x age x muscle x weight x cup x firmness = 6 dependencies.
    const auto breast = idx.group("breast");
    REQUIRE(breast.size() == 216);
    for (const TargetComponent& c : breast) {
        INFO(c.relativePath);
        CHECK(c.variables().size() == 6);
    }

    // A plain shape target carries no macro dependency at all.
    const auto plain = idx.group("armslegs-l-upperarm-scale-horiz-incr");
    REQUIRE(plain.size() == 1);
    CHECK(plain[0].variables().empty());
}

TEST_CASE("the default character's macro group weights sum to one",
          "[golden][parity][fixture][index]") {
    const auto idx = TargetIndex::build(MH_DATA_DIR);
    if (idx.componentCount() == 0) SKIP("target data not present");

    // At defaults (gender .5, age .5, race 1/3 each) the macrodetails group
    // should distribute exactly 1.0 across its 24 targets: the six
    // {african,asian,caucasian} x {male,female} x young combinations at
    // 1/3 * 0.5 * 1 each. This is the property that makes the whole scheme a
    // partition of unity rather than an arbitrary weighted sum.
    const MacroFactors f;
    float total = 0.0F;
    int nonZero = 0;
    for (const TargetComponent& c : idx.group("macrodetails")) {
        const float w = targetWeight(c, f);
        total += w;
        if (w > 1e-6F) ++nonZero;
    }
    CHECK(std::abs(total - 1.0F) < 1e-4F);
    CHECK(nonZero == 6);

    // macrodetails-universal likewise: averagemuscle and averageweight are both
    // 1 at defaults, so male-young and female-young carry 0.5 each.
    float uniTotal = 0.0F;
    int uniNonZero = 0;
    for (const TargetComponent& c : idx.group("macrodetails-universal")) {
        const float w = targetWeight(c, f);
        uniTotal += w;
        if (w > 1e-6F) ++uniNonZero;
    }
    CHECK(std::abs(uniTotal - 1.0F) < 1e-4F);
    CHECK(uniNonZero == 2);
}

TEST_CASE("height and proportions contribute nothing at the neutral default",
          "[golden][parity][fixture][index]") {
    const auto idx = TargetIndex::build(MH_DATA_DIR);
    if (idx.componentCount() == 0) SKIP("target data not present");

    // At height 0.5 both minheight and maxheight are 0, and the group ships no
    // averageheight files -- so the group contributes nothing. The same holds
    // for proportions. A port that assumed every group sums to 1 would be
    // wrong here, which is why this is asserted explicitly.
    const MacroFactors f;
    for (const char* g : {"macrodetails-height", "macrodetails-proportions"}) {
        float total = 0.0F;
        for (const TargetComponent& c : idx.group(g))
            total += targetWeight(c, f);
        INFO(g);
        CHECK(std::abs(total) < 1e-5F);
    }
}
