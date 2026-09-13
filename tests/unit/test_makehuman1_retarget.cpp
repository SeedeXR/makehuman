// SPDX-License-Identifier: AGPL-3.0-or-later
//
// The MakeHuman 1.x retarget table, checked against the rig AND against the
// animation it exists to make usable.
//
// `data/animations/**/*.bvh` name 75 joints of the OLD MakeHuman skeleton and
// match 0 bones of either shipped rig, which is why they pose nothing. This
// table is the second of the three namings the owner asked for.
#include "makehuman/rig/RetargetMap.h"

#include "makehuman/io/BvhReader.h"
#include "makehuman/rig/Skeleton.h"

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <set>
#include <string>
#include <unordered_map>

using namespace mh::rig;
namespace fs = std::filesystem;

namespace {

fs::path dataDir() {
    return fs::path(MH_DATA_DIR);
}

fs::path table() {
    return dataDir() / "rigs" / "makehuman1_retarget.json";
}

fs::path walk() {
    return dataDir() / "animations" / "walks" / "walk1.bvh";
}

}  // namespace

TEST_CASE("the MakeHuman 1.x table maps the joints the shipped walks name",
          "[rig][retarget][mh1]") {
    const auto map = loadRetargetMap(table());
    REQUIRE(map.has_value());

    // MEASURED: walk1.bvh names 75 joints, 16 of which are the `__`-prefixed
    // connectors that the MakeHuman 1.x exporter inserts. Those are left
    // UNMAPPED deliberately -- see the file's own note -- so 59 are mapped.
    CHECK(map->size() == 59);

    const auto bvh = mh::io::readBvh(walk(), {});
    REQUIRE(bvh.has_value());

    size_t named      = 0;
    size_t connectors = 0;
    for (const auto& joint : bvh->joints) {
        if (joint.endSite) continue;
        if (joint.name.starts_with("__")) {
            ++connectors;
            INFO(joint.name << " is a connector and must NOT be mapped");
            CHECK_FALSE(map->toBone.contains(joint.name));
            continue;
        }
        INFO("walk1 names " << joint.name << ", which the table must cover");
        CHECK(map->toBone.contains(joint.name));
        ++named;
    }
    CHECK(connectors == 16);
    CHECK(named == 59);
}

TEST_CASE("every MakeHuman 1.x target is a real bone of the superset rig", "[rig][retarget][mh1]") {
    const auto map = loadRetargetMap(table());
    REQUIRE(map.has_value());
    const auto skel = loadSkeleton(dataDir() / "rigs" / "mixamo_superset.mhskel");
    REQUIRE(skel.has_value());

    std::set<std::string> bones;
    for (const auto& bone : skel->bones)
        bones.insert(bone.name);
    for (const auto& [source, target] : map->toBone) {
        INFO(source << " -> " << target);
        CHECK(bones.contains(target));
    }
}

TEST_CASE("the MakeHuman 1.x table is injective", "[rig][retarget][mh1]") {
    const auto map = loadRetargetMap(table());
    REQUIRE(map.has_value());
    std::set<std::string> targets;
    for (const auto& [source, target] : map->toBone)
        targets.insert(target);
    CHECK(targets.size() == map->size());
}

TEST_CASE("every mapped joint's target descends from its mapped ancestor's target",
          "[rig][retarget][mh1]") {
    // THE assertion, and the one the Mixamo table's provenance claims in prose
    // while nothing checked it. A table can name real bones, be injective, and
    // still be anatomical nonsense -- an elbow mapped above a shoulder. Descent
    // is what rules that out, and it is checkable here because the SOURCE
    // hierarchy is in the BVH itself.
    const auto map = loadRetargetMap(table());
    REQUIRE(map.has_value());
    const auto skel = loadSkeleton(dataDir() / "rigs" / "mixamo_superset.mhskel");
    REQUIRE(skel.has_value());
    const auto bvh = mh::io::readBvh(walk(), {});
    REQUIRE(bvh.has_value());

    // Bone name -> parent bone name, built once. The rig is 179 bones and the
    // walk names 59, so rescanning the vector per hop is a scan nobody needs.
    std::unordered_map<std::string, std::string> parentOf;
    for (const auto& bone : skel->bones)
        if (bone.parent >= 0)
            parentOf[bone.name] = skel->bones[static_cast<size_t>(bone.parent)].name;

    const auto ancestorOf = [&parentOf](const std::string& maybe, std::string bone) {
        for (auto it = parentOf.find(bone); it != parentOf.end(); it = parentOf.find(bone)) {
            bone = it->second;
            if (bone == maybe) return true;
        }
        return false;
    };

    size_t checked = 0;
    for (size_t i = 0; i < bvh->joints.size(); ++i) {
        const auto& joint = bvh->joints[i];
        if (joint.endSite || !map->toBone.contains(joint.name)) continue;
        // The nearest MAPPED ancestor, skipping the unmapped connectors.
        int32_t p = joint.parent;
        while (p >= 0 && (bvh->joints[static_cast<size_t>(p)].endSite ||
                          !map->toBone.contains(bvh->joints[static_cast<size_t>(p)].name))) {
            p = bvh->joints[static_cast<size_t>(p)].parent;
        }
        if (p < 0) continue;
        const std::string& parentTarget = map->toBone.at(bvh->joints[static_cast<size_t>(p)].name);
        const std::string& ownTarget    = map->toBone.at(joint.name);
        INFO(joint.name << " -> " << ownTarget << " must descend from "
                        << bvh->joints[static_cast<size_t>(p)].name << " -> " << parentTarget);
        CHECK(ancestorOf(parentTarget, ownTarget));
        ++checked;
    }
    INFO("parent/child pairs checked: " << checked);
    CHECK(checked >= 40);
}
