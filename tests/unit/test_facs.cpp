// SPDX-License-Identifier: AGPL-3.0-or-later
//
// FACS Action Units over the 60 shipped face pose units.
//
// The table this exercises is DERIVED, not chosen: an AU's number and name are
// published FACS, the muscle each AU codes is published with it, and the pose
// units carry those muscles in their own names (`oris`, `levator`, `risorius`,
// `platysma`). The test that matters most is therefore the cheapest one -- that
// every unit the table names actually exists in the shipped library, because a
// typo there produces a face that is subtly wrong and reports nothing.

#include "makehuman/rig/Facs.h"

#include "makehuman/rig/PoseUnits.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <filesystem>
#include <set>
#include <string>
#include <vector>

using namespace mh;
namespace fs = std::filesystem;

namespace {

std::vector<std::string> shippedUnitNames() {
    auto names =
        rig::loadPoseUnitNames(fs::path(MH_DATA_DIR) / "poseunits" / "face-poseunits.json");
    REQUIRE(names.has_value());
    return *names;
}

std::vector<std::string> resolve(const std::string& au, float weight) {
    const std::array<rig::ActionUnit, 1> want{{{au, weight}}};
    const auto expr = rig::facsExpression(want);
    REQUIRE(expr.has_value());
    std::vector<std::string> out;
    for (const rig::WeightedUnit& u : expr->units)
        out.push_back(u.name);
    return out;
}

}  // namespace

TEST_CASE("every pose unit the FACS table names exists in the shipped library", "[rig][facs]") {
    const std::vector<std::string> shipped = shippedUnitNames();
    REQUIRE(shipped.size() == 60);

    size_t checked = 0;
    for (const rig::ActionUnitInfo& au : rig::actionUnits()) {
        for (const std::string_view unit : au.units()) {
            if (unit.empty()) continue;
            ++checked;
            INFO(au.code << " names \"" << unit << '"');
            CHECK(std::ranges::find(shipped, unit) != shipped.end());
        }
    }
    // Not an incidental number: it fails if the loop stops walking the table.
    CHECK(checked > 40);
}

TEST_CASE("an Action Unit resolves to the units that carry its muscle", "[rig][facs]") {
    // AU12 is the Lip Corner Puller -- zygomaticus major, which pulls both
    // mouth corners up. Bilateral, so both sides come back.
    CHECK(resolve("AU12", 1.0F) == std::vector<std::string>{"MouthLeftPullUp", "MouthRightPullUp"});

    // AU9, the Nose Wrinkler, is one midline muscle and has ONE unit.
    CHECK(resolve("AU9", 1.0F) == std::vector<std::string>{"NoseWrinkler"});

    // AU22, the Lip Funneler, is orbicularis oris acting on both lips: two
    // units, neither of them sided.
    CHECK(resolve("AU22", 1.0F) == std::vector<std::string>{"UpperLipForward", "lowerLipForward"});
}

TEST_CASE("a unilateral Action Unit gives one side only", "[rig][facs]") {
    CHECK(resolve("AU12L", 1.0F) == std::vector<std::string>{"MouthLeftPullUp"});
    CHECK(resolve("AU12R", 1.0F) == std::vector<std::string>{"MouthRightPullUp"});
}

TEST_CASE("an unsided Action Unit refuses a side", "[rig][facs]") {
    // AU9 has no left and right to choose between, so `AU9L` is a request that
    // cannot be honoured. Dropping the suffix silently would apply the whole
    // AU and look like it worked.
    const std::array<rig::ActionUnit, 1> want{{{"AU9L", 1.0F}}};
    const auto expr = rig::facsExpression(want);
    REQUIRE_FALSE(expr.has_value());
    CHECK(expr.error().kind == rig::FacsErrorKind::NotSided);
    CHECK(expr.error().message().find("AU9") != std::string::npos);
}

TEST_CASE("the weight reaches every unit of the Action Unit", "[rig][facs]") {
    const std::array<rig::ActionUnit, 1> want{{{"AU22", 0.25F}}};
    const auto expr = rig::facsExpression(want);
    REQUIRE(expr.has_value());
    REQUIRE(expr->units.size() == 2);
    for (const rig::WeightedUnit& u : expr->units)
        CHECK(u.weight == 0.25F);
}

TEST_CASE("Action Units compose in the order they were asked for", "[rig][facs]") {
    // PoseUnits::blend does not commute, so the request order has to survive.
    const std::array<rig::ActionUnit, 2> want{{{"AU9", 1.0F}, {"AU12", 0.5F}}};
    const auto expr = rig::facsExpression(want);
    REQUIRE(expr.has_value());
    REQUIRE(expr->units.size() == 3);
    CHECK(expr->units[0].name == "NoseWrinkler");
    CHECK(expr->units[1].name == "MouthLeftPullUp");
    CHECK(expr->units[2].name == "MouthRightPullUp");
}

TEST_CASE("an unknown Action Unit is named, not ignored", "[rig][facs]") {
    // Skipping it would apply the rest of the request and produce a face that
    // is missing one component, with nothing said.
    const std::array<rig::ActionUnit, 2> want{{{"AU12", 1.0F}, {"AU99", 1.0F}}};
    const auto expr = rig::facsExpression(want);
    REQUIRE_FALSE(expr.has_value());
    CHECK(expr.error().kind == rig::FacsErrorKind::UnknownUnit);
    CHECK(expr.error().message().find("AU99") != std::string::npos);
}

TEST_CASE("an empty request is refused", "[rig][facs]") {
    const auto expr = rig::facsExpression({});
    REQUIRE_FALSE(expr.has_value());
    CHECK(expr.error().kind == rig::FacsErrorKind::Empty);
}

TEST_CASE("the AUs this rig cannot express are listed, not silently absent", "[rig][facs]") {
    // The gap is measured rather than hidden. Eleven of the 59 non-Rest units
    // have no Action Unit: the seven tongue shapes beyond Tongue Show,
    // UpperLipStretched, ChinDown, and the two lateral mouth shifts. FACS
    // codes none of those as an AU with a single muscle, and inventing a
    // number for them would put a fictional AU in a user's file.
    const std::vector<std::string> shipped = shippedUnitNames();
    std::set<std::string> reachable;
    for (const rig::ActionUnitInfo& au : rig::actionUnits()) {
        for (const std::string_view unit : au.units()) {
            if (!unit.empty()) reachable.emplace(unit);
        }
    }

    std::vector<std::string> unreached;
    for (const std::string& n : shipped) {
        if (n != "Rest" && !reachable.contains(n)) unreached.push_back(n);
    }
    std::ranges::sort(unreached);
    CHECK(unreached == std::vector<std::string>{"ChinDown", "MouthMoveLeft", "MouthMoveRight",
                                                "TongueDown", "TongueLeft", "TonguePointDown",
                                                "TonguePointUp", "TongueRight", "TongueUp",
                                                "TongueUshape", "UpperLipStretched"});
}

TEST_CASE("no two Action Units share a code", "[rig][facs]") {
    std::set<std::string_view> seen;
    for (const rig::ActionUnitInfo& au : rig::actionUnits()) {
        INFO("duplicate code " << au.code);
        CHECK(seen.insert(au.code).second);
    }
}
