// SPDX-License-Identifier: AGPL-3.0-or-later
//
// The BODY pose units, and the bone table without which a third of them are
// silently inert.
//
// `data/poseunits/body-poseunits.json` holds 61 poses, each naming its bones
// directly as `[w,x,y,z]` quaternions -- no BVH and no frames, unlike the face
// units. It was authored against a differently-named skeleton, so MEASURED
// against both shipped rigs: 29 resolve fully, 24 partially, and 8 not at all.
//
// The load-bearing assertion here is not that the file parses. It is that the
// table moves those numbers, and that a pose which cannot resolve moves nothing
// rather than moving something plausible and wrong.
#include "makehuman/rig/PoseUnits.h"

#include "makehuman/rig/RetargetMap.h"
#include "makehuman/rig/Skeleton.h"

#include "makehuman/foundation/Transform.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <set>
#include <string>

using namespace mh::rig;
namespace fs = std::filesystem;

namespace {

fs::path dataDir() {
    return fs::path(MH_DATA_DIR);
}

fs::path unitsFile() {
    return dataDir() / "poseunits" / "body-poseunits.json";
}

Skeleton superset() {
    auto skel = loadSkeleton(dataDir() / "rigs" / "mixamo_superset.mhskel");
    REQUIRE(skel.has_value());
    return *skel;
}

RetargetMap boneTable() {
    auto map = loadRetargetMap(dataDir() / "poseunits" / "body-poseunits-bones.json");
    REQUIRE(map.has_value());
    return *map;
}

/// How many of @p units's poses move at least one bone.
size_t movingUnits(const PoseUnits& units) {
    size_t moving = 0;
    for (size_t u = 0; u < units.unitCount(); ++u) {
        const auto unit = units.unit(u);
        const bool any  = std::any_of(unit.begin(), unit.end(), [](const mh::foundation::Mat4& m) {
            return m.m != mh::foundation::Mat4::identity().m;
        });
        if (any) ++moving;
    }
    return moving;
}

/// Writes a body-poseunits JSON into a temp dir and removes it on scope exit.
/// Deliberately NOT under data/ -- the asset-index test counts files there.
struct TempUnits {
    fs::path dir  = fs::temp_directory_path() / "mh_body_poseunits";
    fs::path file = dir / "units.json";

    explicit TempUnits(std::string_view body) {
        fs::create_directories(dir);
        std::ofstream(file) << body;
    }

    ~TempUnits() {
        std::error_code ec;
        fs::remove_all(dir, ec);
    }

    TempUnits(const TempUnits&)            = delete;
    TempUnits& operator=(const TempUnits&) = delete;
};

}  // namespace

// ---------------------------------------------------------------- unit

TEST_CASE("the body pose units load with one entry per pose", "[rig][poseunits][body]") {
    const auto skel  = superset();
    const auto units = loadBodyPoseUnits(unitsFile(), skel);
    REQUIRE(units.has_value());

    // MEASURED: 61 poses in the shipped asset.
    CHECK(units->unitCount() == 61);
    // One transform per bone of the rig, per unit -- the same shape the face
    // units produce, so `blend` and `indexOf` cannot tell the producers apart.
    CHECK(units->boneCount == skel.boneCount());
    CHECK(units->data.size() == 61 * skel.boneCount());

    // Named, and reachable by name.
    CHECK(units->indexOf("UpperArmUpLeft1").has_value());
    CHECK_FALSE(units->indexOf("NoSuchUnitAtAll").has_value());
}

TEST_CASE("the bone table revives the poses that named a renamed bone", "[rig][poseunits][body]") {
    const auto skel = superset();

    const auto plain = loadBodyPoseUnits(unitsFile(), skel);
    REQUIRE(plain.has_value());
    const auto table  = boneTable();
    const auto mapped = loadBodyPoseUnits(unitsFile(), skel, &table);
    REQUIRE(mapped.has_value());

    // THE measurement. Without the table 8 poses drive nothing at all; with it,
    // 5 -- `LowerLegBendLeft1`, `LowerLegBendLeft2` and `UpperLegForwardLeft`
    // each name `upperleg.L` and nothing else, so one rename revives all three.
    CHECK(movingUnits(*plain) == 61 - 8);
    CHECK(movingUnits(*mapped) == 61 - 5);

    for (const char* revived : {"LowerLegBendLeft1", "LowerLegBendLeft2", "UpperLegForwardLeft"}) {
        const auto at = mapped->indexOf(revived);
        REQUIRE(at.has_value());
        const auto unit = mapped->unit(*at);
        INFO(revived << " must move something once the table is applied");
        CHECK(std::any_of(unit.begin(), unit.end(), [](const mh::foundation::Mat4& m) {
            return m.m != mh::foundation::Mat4::identity().m;
        }));
    }
}

TEST_CASE("the table is only the three renames that are unambiguous", "[rig][poseunits][body]") {
    // Pinned because the temptation is to add more. `spine1..4` would have to
    // land on `spine01..05` -- four into five -- and guessing twists the torso;
    // the rest have no counterpart in this rig at all.
    const auto table = boneTable();
    CHECK(table.size() == 3);
    CHECK(table.toBone.at("neck") == "neck01");
    CHECK(table.toBone.at("shoulder.L") == "shoulder01.L");
    CHECK(table.toBone.at("upperleg.L") == "upperleg01.L");
}

TEST_CASE("every table target is a real bone of both shipped rigs", "[rig][poseunits][body]") {
    // A table naming a bone that does not exist resolves to nothing while
    // looking like it works -- the same trap the retarget tables are gated for.
    const auto table = boneTable();
    for (const char* rig : {"mixamo_superset", "default"}) {
        const auto skel = loadSkeleton(dataDir() / "rigs" / (std::string(rig) + ".mhskel"));
        REQUIRE(skel.has_value());
        for (const auto& [source, target] : table.toBone) {
            const bool present = std::any_of(skel->bones.begin(), skel->bones.end(),
                                             [&target](const Bone& b) { return b.name == target; });
            INFO(rig << ": " << source << " -> " << target);
            CHECK(present);
        }
    }
}

TEST_CASE("the quaternion is read as [w,x,y,z], not [x,y,z,w]", "[rig][poseunits][body]") {
    // The classic trap this project names in CLAUDE.md, and the one every other
    // test here was blind to: they check whether a unit MOVES, never whether it
    // moves correctly. Measured -- swapping the component order changed no test
    // outcome, because a wrong rotation is still a rotation.
    //
    // `LowerLegBendLeft1` drives `upperleg.L` with [0.965926, 0, -0.258819, 0].
    // Read as [w,x,y,z] that is a 30-degree turn about Y. Read as [x,y,z,w] it
    // is a 180-degree turn about a mostly-X axis -- a completely different body.
    const auto skel  = superset();
    const auto table = boneTable();
    const auto units = loadBodyPoseUnits(unitsFile(), skel, &table);
    REQUIRE(units.has_value());

    const auto at = units->indexOf("LowerLegBendLeft1");
    REQUIRE(at.has_value());

    const auto bone = std::find_if(skel.bones.begin(), skel.bones.end(),
                                   [](const Bone& b) { return b.name == "upperleg01.L"; });
    REQUIRE(bone != skel.bones.end());
    const auto index               = static_cast<size_t>(std::distance(skel.bones.begin(), bone));
    const mh::foundation::Mat4 got = units->unit(*at)[index];

    // Exactly what the file says, in this project's order.
    const mh::foundation::Mat4 want =
        mh::foundation::quaternionMatrix(mh::foundation::Quat{0.965926, 0.0, -0.258819, 0.0});
    for (size_t r = 0; r < 4; ++r) {
        for (size_t c = 0; c < 4; ++c) {
            INFO("element [" << r << "][" << c << "]");
            CHECK(got.m[r][c] == Catch::Approx(want.m[r][c]).margin(1e-6));
        }
    }

    // And the geometric fact behind it, which reads without a debugger: a turn
    // about Y leaves Y alone. The swapped order does not.
    CHECK(got.m[0][1] == Catch::Approx(0.0).margin(1e-6));
    CHECK(got.m[1][1] == Catch::Approx(1.0).margin(1e-6));
    CHECK(got.m[2][1] == Catch::Approx(0.0).margin(1e-6));
}

TEST_CASE("no body unit is shadowed by a face unit of the same name", "[rig][poseunits][body]") {
    // `main.cpp` merges the 60 face units and the 61 body units into ONE
    // library so `--pose-unit` reaches both, and `indexOf` returns the FIRST
    // match. A name in both sets would therefore make the body unit permanently
    // unreachable -- with no error, because asking for it still finds something.
    //
    // MEASURED today: disjoint, 121 distinct names. This pins it, because the
    // day an asset edit breaks it nothing else would say so.
    const auto skel = superset();

    const auto faceNames = loadPoseUnitNames(dataDir() / "poseunits" / "face-poseunits.json");
    REQUIRE(faceNames.has_value());
    const auto body = loadBodyPoseUnits(unitsFile(), skel);
    REQUIRE(body.has_value());

    // Not vacuous: two empty sets are trivially disjoint.
    REQUIRE(faceNames->size() == 60);
    REQUIRE(body->names.size() == 61);

    const std::set<std::string> face(faceNames->begin(), faceNames->end());
    std::vector<std::string> shared;
    std::ranges::set_intersection(face,
                                  std::set<std::string>(body->names.begin(), body->names.end()),
                                  std::back_inserter(shared));
    INFO("names in BOTH libraries, so the body unit is unreachable");
    CHECK(shared.empty());
}

// ----------------------------------------------------------- regression

TEST_CASE("a pose naming bones this rig lacks stays completely still",
          "[rig][poseunits][body][regression]") {
    // `FootDownLeft` needs heel and metatarsal bones this rig does not have,
    // and no table can invent them. It must move NOTHING -- a partially applied
    // foot pose would be a plausible wrong body, which is worse than a still
    // one. (It is also one of the four mis-named poses the audit pins:
    // `Finger1CloseLeft` and `Finger2CloseLeft` drive these same six bones.)
    const auto skel  = superset();
    const auto table = boneTable();
    const auto units = loadBodyPoseUnits(unitsFile(), skel, &table);
    REQUIRE(units.has_value());

    for (const char* still :
         {"FootDownLeft", "FootUpLeft", "Finger1CloseLeft", "Finger2CloseLeft", "TorsoRight"}) {
        const auto at = units->indexOf(still);
        REQUIRE(at.has_value());
        const auto unit = units->unit(*at);
        INFO(still << " names bones this rig lacks, so it must stay at identity");
        CHECK(std::all_of(unit.begin(), unit.end(), [](const mh::foundation::Mat4& m) {
            return m.m == mh::foundation::Mat4::identity().m;
        }));
    }
}

TEST_CASE("a quaternion that is not four numbers is an error, not a skipped bone",
          "[rig][poseunits][body][regression]") {
    // Silently dropping the bone is the worst option: the pose still loads, it
    // just moves less than it says, and nothing anywhere reports it. The sibling
    // loader in this module rejects a wrong-typed value outright
    // (`src/rig/RetargetMap.cpp:52-55`) and so must this one.
    const auto skel = superset();
    const TempUnits units(R"({"poses":{"Bend":{"upperleg01.L":[1.0, 0.0, 0.0]}}})");

    const auto loaded = loadBodyPoseUnits(units.file, skel);
    REQUIRE_FALSE(loaded.has_value());
    CHECK(loaded.error().kind == PoseUnitsErrorKind::Malformed);
    // The message has to name the bone, or a 61-pose asset gives the author
    // nothing to search for.
    CHECK(loaded.error().detail.find("upperleg01.L") != std::string::npos);
}

TEST_CASE("a pose that is not an object is an error, not an empty pose",
          "[rig][poseunits][body][regression]") {
    // The third way this file can lie: the entry parses, so the library loads
    // with the right NAME present and nothing behind it. `--pose-unit Bend=1`
    // would then report success and move nothing.
    const auto skel = superset();
    const TempUnits units(R"({"poses":{"Bend":5}})");

    const auto loaded = loadBodyPoseUnits(units.file, skel);
    REQUIRE_FALSE(loaded.has_value());
    CHECK(loaded.error().kind == PoseUnitsErrorKind::Malformed);
    CHECK(loaded.error().detail.find("Bend") != std::string::npos);
}

TEST_CASE("a quaternion of strings is an error, not a thrown exception",
          "[rig][poseunits][body][regression]") {
    // Four strings pass an `is_array() && size() == 4` guard and then throw
    // `json::type_error` out of `get<double>()` -- past the `std::expected`
    // that every caller is written against. An error return is the contract.
    const auto skel = superset();
    const TempUnits units(R"({"poses":{"Bend":{"upperleg01.L":["a","b","c","d"]}}})");

    std::expected<PoseUnits, PoseUnitsError> loaded =
        std::unexpected(PoseUnitsError{PoseUnitsErrorKind::NotFound, "", ""});
    REQUIRE_NOTHROW(loaded = loadBodyPoseUnits(units.file, skel));
    REQUIRE_FALSE(loaded.has_value());
    CHECK(loaded.error().kind == PoseUnitsErrorKind::Malformed);
}

TEST_CASE("an absent file is an error, not an empty library",
          "[rig][poseunits][body][regression]") {
    // Returning an empty library would make every `--pose-unit` request fail
    // with "no such unit" instead of naming the real problem.
    const auto skel  = superset();
    const auto units = loadBodyPoseUnits(dataDir() / "poseunits" / "no-such-file.json", skel);
    REQUIRE_FALSE(units.has_value());
    CHECK(units.error().kind == PoseUnitsErrorKind::NotFound);
}
