// SPDX-License-Identifier: AGPL-3.0-or-later
//
// The retarget table, against the rig it claims to target.
//
// `data/rigs/mixamo_retarget.json` shipped as data with no reader. The tests
// that matter here are not "did the JSON parse" but "does the table still
// describe the rig": a mapping whose targets have been renamed out from under
// it parses perfectly and retargets nothing.
#include "makehuman/rig/RetargetMap.h"

#include "makehuman/rig/Skeleton.h"

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <set>
#include <string>

using namespace mh::rig;
namespace fs = std::filesystem;

namespace {

fs::path rigsDir() {
    return fs::path(MH_DATA_DIR) / "rigs";
}

fs::path table() {
    return rigsDir() / "mixamo_retarget.json";
}

}  // namespace

TEST_CASE("the shipped retarget table loads every pair", "[rig][retarget]") {
    const auto map = loadRetargetMap(table());
    REQUIRE(map.has_value());

    // MEASURED on the shipped file, and the provenance says the same: 65
    // Mixamo bones, all of which have a counterpart.
    CHECK(map->size() == 65);

    // Spot values, so a table that loaded the wrong KEY for the right count
    // cannot pass. `LeftArm -> upperarm01.L` is the interesting one: the
    // names share no substring, which is what makes a table necessary rather
    // than a name-mangling heuristic.
    //
    // It used to assert `shoulder01.L`, and that was pinning a DEFECT. The
    // expectation now comes from `data/rigs/mixamo_retarget.json` as
    // regenerated on 2026-09-20, and from the render that forced the change:
    // with `shoulder01.L` every shipped animation drew a body with its arms
    // torn off at the deltoid. The reasoning is in `tools/mixamo_mapping.py`
    // beside the mapping.
    REQUIRE(map->toBone.contains("Head"));
    CHECK(map->toBone.at("Head") == "head");
    REQUIRE(map->toBone.contains("LeftArm"));
    CHECK(map->toBone.at("LeftArm") == "upperarm01.L");
    REQUIRE(map->toBone.contains("Hips"));
    CHECK(map->toBone.at("Hips") == "hips");
}

TEST_CASE("every target of the retarget table is a real bone of the rig it names",
          "[rig][retarget]") {
    // THE assertion. A table is only worth loading if it still points at bones
    // that exist; rename a bone in the .mhskel and this fails, which is the
    // moment someone should regenerate the table with
    // `tools/mixamo_mapping.py --emit`.
    const auto map = loadRetargetMap(table());
    REQUIRE(map.has_value());
    const auto skel = loadSkeleton(rigsDir() / "mixamo_superset.mhskel");
    REQUIRE(skel.has_value());

    std::set<std::string> bones;
    for (const auto& bone : skel->bones)
        bones.insert(bone.name);
    INFO("superset bones: " << bones.size());
    CHECK(bones.size() == 179);

    for (const auto& [source, target] : map->toBone) {
        INFO(source << " -> " << target);
        CHECK(bones.contains(target));
    }
}

TEST_CASE("the retarget table is injective, so a pose cannot drive one bone twice",
          "[rig][retarget]") {
    // Two Mixamo joints mapping to one bone would make the later one silently
    // win, and which one wins would depend on hash order. The provenance
    // claims injectivity; this is what makes the claim checkable.
    const auto map = loadRetargetMap(table());
    REQUIRE(map.has_value());

    std::set<std::string> targets;
    for (const auto& [source, target] : map->toBone)
        targets.insert(target);
    CHECK(targets.size() == map->size());
}

TEST_CASE("a missing or malformed retarget table is an error, not an empty map",
          "[rig][retarget]") {
    // Empty is a plausible-looking success: it retargets nothing and reports
    // no problem, which is how a typo in a path becomes "the animation just
    // did not work".
    const auto missing = loadRetargetMap(rigsDir() / "no-such-table.json");
    REQUIRE_FALSE(missing.has_value());
    CHECK(missing.error().kind == SkeletonErrorKind::NotFound);

    const fs::path broken = fs::temp_directory_path() / "mh_broken_retarget.json";
    std::ofstream(broken) << "{ \"mapping\": { \"Head\": ";  // truncated
    const auto bad = loadRetargetMap(broken);
    fs::remove(broken);
    REQUIRE_FALSE(bad.has_value());
    CHECK(bad.error().kind == SkeletonErrorKind::Malformed);

    // An EMPTY target parses fine and is the dangerous one: stored as-is it
    // renames a joint to nothing, which every reader downstream sees as "the
    // rig has no such bone" -- a hole that looks like ordinary absence. Pinned
    // separately because the spot checks above would fail either way, so
    // without this nothing distinguishes refusing it from storing it.
    const fs::path empty = fs::temp_directory_path() / "mh_empty_retarget.json";
    std::ofstream(empty) << R"({ "mapping": { "Head": "" } })";
    const auto hole = loadRetargetMap(empty);
    fs::remove(empty);
    REQUIRE_FALSE(hole.has_value());
    CHECK(hole.error().kind == SkeletonErrorKind::Malformed);
}
