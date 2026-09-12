// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Writing a `.mhpose`.
//
// The port could read an expression and derive one from FACS, but never write
// one -- so the reference's Save button (`plugins/7_expression_mixer.py:221`)
// had no counterpart and a user could not keep an expression they had composed.
// That is also why `memory/taskviews.md` could say "this port ships zero
// .mhpose files" without anyone being able to do something about it.
//
// The order of `unit_poses` is the load-bearing detail. `PoseUnits::blend`
// multiplies quaternions and does not commute, so the file's order IS part of
// the expression. `loadExpression` learnt that the hard way -- its comment
// records a measured 0.0245 divergence when it used sorted `nlohmann::json` --
// and a writer that sorts is the same bug facing the other way.

#include "makehuman/rig/PoseUnits.h"

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

using namespace mh::rig;
namespace fs = std::filesystem;

namespace {

fs::path scratch(const std::string& stem) {
    const auto dir = fs::temp_directory_path() / "mh-save-expression";
    fs::create_directories(dir);
    return dir / (stem + ".mhpose");
}

/// Deliberately NOT alphabetical, and deliberately not the order any container
/// would produce by accident.
Expression sample() {
    Expression e;
    e.name        = "Smirk";
    e.description = "One corner only";
    e.tags        = {"happy", "asymmetric"};
    e.units       = {{"RightOuterBrowUp", 0.25F}, {"LeftLipCornerUp", 1.0F}, {"ArchBrows", 0.5F}};
    return e;
}

std::string readAll(const fs::path& p) {
    std::ifstream in(p);
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

}  // namespace

TEST_CASE("a saved expression reloads with every field intact", "[rig][expression]") {
    const auto file = scratch("roundtrip");
    REQUIRE(saveExpression(file, sample()).has_value());

    const auto back = loadExpression(file);
    REQUIRE(back.has_value());
    CHECK(back->name == "Smirk");
    CHECK(back->description == "One corner only");
    CHECK(back->tags == std::vector<std::string>{"happy", "asymmetric"});
    REQUIRE(back->units.size() == 3);
}

TEST_CASE("the unit ORDER survives the round trip", "[rig][expression]") {
    const auto file = scratch("order");
    REQUIRE(saveExpression(file, sample()).has_value());

    const auto back = loadExpression(file);
    REQUIRE(back.has_value());
    // Alphabetical would be ArchBrows, LeftLipCornerUp, RightOuterBrowUp.
    // Anything that sorts -- including plain `nlohmann::json`, whose object is
    // a std::map -- produces exactly that and a different face.
    REQUIRE(back->units.size() == 3);
    CHECK(back->units[0].name == "RightOuterBrowUp");
    CHECK(back->units[1].name == "LeftLipCornerUp");
    CHECK(back->units[2].name == "ArchBrows");
    CHECK(back->units[0].weight == 0.25F);
    CHECK(back->units[1].weight == 1.0F);
    CHECK(back->units[2].weight == 0.5F);
}

TEST_CASE("the order is in the FILE, not just in the parse", "[rig][expression]") {
    const auto file = scratch("ontext");
    REQUIRE(saveExpression(file, sample()).has_value());

    // Read the bytes, so this fails even if a future loader started sorting and
    // the round-trip above agreed with itself.
    const std::string text = readAll(file);
    const auto right       = text.find("RightOuterBrowUp");
    const auto left        = text.find("LeftLipCornerUp");
    const auto arch        = text.find("ArchBrows");
    REQUIRE(right != std::string::npos);
    REQUIRE(left != std::string::npos);
    REQUIRE(arch != std::string::npos);
    CHECK(right < left);
    CHECK(left < arch);
}

TEST_CASE("a zero-weight unit is dropped, as the reference drops it", "[rig][expression]") {
    Expression e = sample();
    e.units.insert(e.units.begin() + 1, {"Blink", 0.0F});

    const auto file = scratch("zero");
    REQUIRE(saveExpression(file, e).has_value());

    const auto back = loadExpression(file);
    REQUIRE(back.has_value());
    // `unitpose_values = dict([(m,v) for m, v in self.modifiers.items()
    //  if v != 0])` -- 7_expression_mixer.py:223. Every slider is a modifier,
    // so without the filter a saved file would name all 60 units and say
    // nothing about which ones the author actually moved.
    CHECK(back->units.size() == 3);
    for (const auto& u : back->units)
        CHECK(u.name != "Blink");
}

TEST_CASE("an expression with nothing in it is refused, not written", "[rig][expression]") {
    Expression e;
    e.name = "Empty";

    const auto file = scratch("empty");
    fs::remove(file);
    const auto saved = saveExpression(file, e);
    REQUIRE(!saved.has_value());
    CHECK(saved.error().kind == PoseUnitsErrorKind::Malformed);
    // `loadExpression` refuses a file whose `unit_poses` is empty, because the
    // reference raises "needs to contain at least one entry". Writing one would
    // be producing a file our own reader rejects.
    CHECK(!fs::exists(file));
}

TEST_CASE("an expression whose units are ALL zero is refused too", "[rig][expression]") {
    Expression e;
    e.name  = "AllZero";
    e.units = {{"Blink", 0.0F}, {"ArchBrows", 0.0F}};

    const auto file = scratch("allzero");
    fs::remove(file);
    const auto saved = saveExpression(file, e);
    // The filter above is what makes this reachable: a caller can hand over a
    // full slider set at rest and the result is an empty file, which is the
    // case the reference raises RuntimeError on (7_expression_mixer.py:224).
    REQUIRE(!saved.has_value());
    CHECK(!fs::exists(file));
}

TEST_CASE("an unnamed expression is refused", "[rig][expression]") {
    Expression e = sample();
    e.name.clear();

    const auto file = scratch("noname");
    fs::remove(file);
    const auto saved = saveExpression(file, e);
    // `loadExpression` requires "name"; writing a file it would reject is the
    // same mistake as the empty one.
    REQUIRE(!saved.has_value());
    CHECK(!fs::exists(file));
}

TEST_CASE("an unwritable path reports that, and does not throw", "[rig][expression]") {
    const auto dir = fs::temp_directory_path() / "mh-save-expression" / "nope";
    fs::remove_all(dir);
    const auto saved = saveExpression(dir / "deeper" / "x.mhpose", sample());
    REQUIRE(!saved.has_value());
    CHECK(saved.error().kind == PoseUnitsErrorKind::Unreadable);
}

TEST_CASE("description and tags are optional on the way out", "[rig][expression]") {
    Expression e;
    e.name  = "Bare";
    e.units = {{"ArchBrows", 1.0F}};

    const auto file = scratch("bare");
    REQUIRE(saveExpression(file, e).has_value());

    const auto back = loadExpression(file);
    REQUIRE(back.has_value());
    CHECK(back->name == "Bare");
    CHECK(back->description.empty());
    CHECK(back->tags.empty());
    REQUIRE(back->units.size() == 1);
}

TEST_CASE("a unit named twice is refused, because JSON cannot hold it", "[rig][expression]") {
    Expression e;
    e.name = "Twice";
    // Reachable from the command line today: `--facs AU12=0.3 --facs AU12=0.7`
    // asks for the same two pose units twice, and `blend` applies both --
    // quaternion composition, not addition, so the result is neither 0.3 nor
    // 0.7 nor 1.0.
    //
    // `unit_poses` is a JSON OBJECT keyed by unit name, so it has no way to say
    // "this unit, twice". Writing it anyway keeps the last weight and produces
    // a file that loads cleanly into a different face: measured, 1,793 of
    // 14,444 vertices away from what was asked for. Refusing is the only honest
    // option the format leaves.
    e.units = {{"MouthLeftPullUp", 0.3F}, {"MouthLeftPullUp", 0.7F}};

    const auto file = scratch("twice");
    fs::remove(file);
    const auto saved = saveExpression(file, e);
    REQUIRE(!saved.has_value());
    CHECK(saved.error().kind == PoseUnitsErrorKind::Malformed);
    CHECK(!fs::exists(file));
}

TEST_CASE("a unit repeated only at zero weight is not a duplicate", "[rig][expression]") {
    Expression e;
    e.name = "ZeroThenReal";
    // The zeros are dropped before the check, so this is one unit, not two --
    // and refusing it would reject a slider set that merely touched a unit and
    // put it back.
    e.units = {{"ArchBrows", 0.0F}, {"ArchBrows", 0.5F}};

    const auto file = scratch("zerothenreal");
    REQUIRE(saveExpression(file, e).has_value());
    const auto back = loadExpression(file);
    REQUIRE(back.has_value());
    REQUIRE(back->units.size() == 1);
    CHECK(back->units[0].weight == 0.5F);
}
