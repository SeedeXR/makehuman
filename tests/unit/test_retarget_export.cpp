// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Retargeting on EXPORT: the other half of the owner's naming decision.
//
// Import asks "this file says UpArm_L, which of MY bones is that?". Export asks
// the same question backwards: "this bone is shoulder01.L, what does Mixamo
// call it?". One table answers both, read in opposite directions.
//
// The load-bearing facts are that the inversion is EXACT -- both shipped tables
// are gated injective, so nothing is lost -- and that a bone the other skeleton
// has no word for keeps this rig's own name rather than being dropped or
// blanked.
#include "makehuman/rig/RetargetMap.h"

#include "makehuman/rig/Skeleton.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <filesystem>
#include <string>
#include <utility>
#include <vector>

using namespace mh::rig;
namespace fs = std::filesystem;

namespace {

fs::path dataDir() {
    return fs::path(MH_DATA_DIR);
}

RetargetMap table(const std::string& stem) {
    auto map = loadRetargetMap(dataDir() / "rigs" / (stem + "_retarget.json"));
    REQUIRE(map.has_value());
    return *map;
}

}  // namespace

// ---------------------------------------------------------------- unit

TEST_CASE("inverting a shipped table loses nothing", "[rig][retarget][export]") {
    for (const char* stem : {"makehuman1", "mixamo"}) {
        const auto forward = table(stem);
        size_t collisions  = 0;
        const auto back    = invertRetargetMap(forward, &collisions);

        INFO(stem << " must invert exactly");
        // Gated injective elsewhere, so inversion is total: same size, nothing
        // dropped. If a future edit makes a table non-injective this is the
        // test that says so, rather than pairs vanishing in silence.
        CHECK(collisions == 0);
        CHECK(back.size() == forward.size());

        for (const auto& [source, bone] : forward.toBone) {
            INFO(source << " -> " << bone << " must come back");
            const auto it = back.toBone.find(bone);
            REQUIRE(it != back.toBone.end());
            CHECK(it->second == source);
        }
    }
}

TEST_CASE("inverting twice is the original table", "[rig][retarget][export]") {
    const auto forward = table("makehuman1");
    const auto twice   = invertRetargetMap(invertRetargetMap(forward));
    CHECK(twice.toBone == forward.toBone);
}

TEST_CASE("a non-injective table reports its collisions", "[rig][retarget][export]") {
    // Two sources claiming one bone cannot both survive the inversion. The
    // count is what stops that being silent -- a dropped pair means a bone
    // exports under the wrong skeleton's name, which looks like valid output.
    RetargetMap clash;
    clash.toBone.emplace("LeftArm", "shoulder01.L");
    clash.toBone.emplace("UpArm_L", "shoulder01.L");
    size_t collisions = 0;
    const auto back   = invertRetargetMap(clash, &collisions);
    CHECK(collisions == 1);
    CHECK(back.size() == 1);
    // And WHICH one survives is fixed, not whichever the hash table happened to
    // visit first: `unordered_map` iteration order is unspecified, so picking by
    // arrival would make one table export different files on different runs.
    // Lexicographically smaller wins, so "LeftArm" beats "UpArm_L".
    REQUIRE(back.toBone.contains("shoulder01.L"));
    CHECK(back.toBone.at("shoulder01.L") == "LeftArm");
}

TEST_CASE("renameBones renames what the table covers and nothing else", "[rig][retarget][export]") {
    const auto back = invertRetargetMap(table("makehuman1"));

    std::vector<std::string> names{"shoulder01.L", "not-a-bone", "head", "shoulder01.R"};
    const size_t renamed = renameBones(names, back);

    CHECK(renamed == 3);
    CHECK(names[0] == "UpArm_L");
    // Untouched, and NOT blanked: a bone the other skeleton has no word for
    // keeps this rig's own name.
    CHECK(names[1] == "not-a-bone");
    CHECK(names[2] == "Head");
    CHECK(names[3] == "UpArm_R");
}

TEST_CASE("renaming an already-renamed list is a no-op", "[rig][retarget][export]") {
    // Applying the table twice must not walk names through a second hop. It
    // cannot here -- the targets are MakeHuman 1.x names and the sources are
    // this rig's -- and this pins that the two vocabularies stay disjoint.
    const auto back = invertRetargetMap(table("makehuman1"));
    std::vector<std::string> names{"shoulder01.L", "head"};
    CHECK(renameBones(names, back) == 2);
    const std::vector<std::string> once = names;
    CHECK(renameBones(names, back) == 0);
    CHECK(names == once);
}

// ----------------------------------------------------------- regression

TEST_CASE("every exported name is a name the source skeleton uses",
          "[rig][retarget][export][regression]") {
    // The whole point: after renaming, a file written for Mixamo must contain
    // Mixamo's words. Anything still carrying this rig's spelling is a bone
    // Mixamo has no name for, which is allowed -- but it must be one the table
    // genuinely does not cover, not one the rename missed.
    const auto forward = table("mixamo");
    const auto back    = invertRetargetMap(forward);
    const auto skel    = loadSkeleton(dataDir() / "rigs" / "mixamo_superset.mhskel");
    REQUIRE(skel.has_value());

    std::vector<std::string> names;
    names.reserve(skel->bones.size());
    for (const auto& bone : skel->bones)
        names.push_back(bone.name);

    const size_t renamed = renameBones(names, back);
    // MEASURED: the Mixamo table covers 65 of the superset's 179 bones, and one
    // of those 65 is an IDENTITY pair -- `HeadTop_End` is Mixamo's own word,
    // which this rig borrowed. So 64 names change and 65 are covered.
    CHECK(renamed == 64);
    CHECK(names.size() == skel->boneCount());

    size_t stillNative = 0;
    for (size_t i = 0; i < names.size(); ++i) {
        if (names[i] != skel->bones[i].name) continue;
        ++stillNative;
        // Unchanged is allowed for exactly two reasons: the table has no word
        // for this bone, or its word is the one we already use. Anything else
        // is a rename that silently missed.
        const auto it = back.toBone.find(names[i]);
        INFO(names[i] << " kept its native name");
        CHECK((it == back.toBone.end() || it->second == names[i]));
    }
    CHECK(stillNative == skel->boneCount() - 64);
}

TEST_CASE("an empty table renames nothing", "[rig][retarget][export][regression]") {
    // `--rig-names native` is this case, and it must be exactly the old
    // behaviour: every bone keeps the name this rig gave it.
    const RetargetMap none;
    std::vector<std::string> names{"shoulder01.L", "head"};
    const std::vector<std::string> before = names;
    CHECK(renameBones(names, none) == 0);
    CHECK(names == before);
}

// ------------------------------------------------- naming fit (chooser)

namespace {

std::vector<std::pair<std::string, RetargetMap>> shippedTables() {
    std::vector<std::pair<std::string, RetargetMap>> out;
    for (const char* stem : {"makehuman1", "mixamo"})
        out.emplace_back(stem, table(stem));
    return out;
}

fs::path walk() {
    return dataDir() / "animations" / "walks" / "walk1.bvh";
}

}  // namespace

TEST_CASE("the shipped walks need a naming, and rankNamings says which",
          "[rig][retarget][naming]") {
    const auto skel = loadSkeleton(dataDir() / "rigs" / "mixamo_superset.mhskel");
    REQUIRE(skel.has_value());
    const auto tables = shippedTables();

    const auto ranked = rankNamings(walk(), *skel, tables);
    // Every candidate is ranked, plus the file's own names, so "nothing helps"
    // is an answer rather than an empty result.
    REQUIRE(ranked.size() == tables.size() + 1);

    // MEASURED on all three shipped animations: 0 native, 59 makehuman1,
    // 5 mixamo. So the 1.x table wins and native comes last.
    CHECK(ranked.front().naming == "makehuman1");
    CHECK(ranked.front().driven == 59);
    CHECK(ranked.back().naming == "native");
    CHECK(ranked.back().driven == 0);

    // Ranked, not merely listed.
    for (size_t i = 1; i < ranked.size(); ++i)
        CHECK(ranked[i - 1].driven >= ranked[i].driven);
}

TEST_CASE("a pose written for THIS rig ranks its own names first", "[rig][retarget][naming]") {
    // The counterpart, and the reason native is a candidate rather than a
    // fallback: tpose.bvh is authored against this rig, so no table beats it.
    const auto skel = loadSkeleton(dataDir() / "rigs" / "mixamo_superset.mhskel");
    REQUIRE(skel.has_value());
    const auto ranked = rankNamings(dataDir() / "poses" / "tpose.bvh", *skel, shippedTables());
    REQUIRE_FALSE(ranked.empty());
    CHECK(ranked.front().naming == "native");
    CHECK(ranked.front().driven > 100);
}

TEST_CASE("an unreadable file ranks nothing rather than guessing", "[rig][retarget][naming]") {
    const auto skel = loadSkeleton(dataDir() / "rigs" / "mixamo_superset.mhskel");
    REQUIRE(skel.has_value());
    const auto ranked = rankNamings(dataDir() / "no_such_file.bvh", *skel, shippedTables());
    CHECK(ranked.empty());
}

TEST_CASE("no candidates still ranks the file's own names", "[rig][retarget][naming]") {
    const auto skel = loadSkeleton(dataDir() / "rigs" / "mixamo_superset.mhskel");
    REQUIRE(skel.has_value());
    const auto ranked = rankNamings(walk(), *skel, {});
    REQUIRE(ranked.size() == 1);
    CHECK(ranked.front().naming == "native");
    CHECK(ranked.front().driven == 0);
}
