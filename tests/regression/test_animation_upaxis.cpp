// SPDX-License-Identifier: AGPL-3.0-or-later
//
// The up axis of the shipped animations, checked against what their own
// metadata declares rather than against the reader's opinion of itself.
//
// `io::readBvh` GUESSES the up axis when `UpAxis::Auto` is set (the default):
// it finds one of `kUpProbeJoints` -- `spine03`, `upperleg02.L`, `head` and
// three more -- takes the direction to its first child, and calls the file Z-up
// when |z| beats |y| (`src/io/BvhReader.cpp:165-184`). Two files anywhere pin
// that guess, and both are ones the test itself chose.
//
// `data/animations/*.mhanim` is an INDEPENDENT declaration: each carries one
// `# anim <Name> <file> z_is_up` line per BVH, authored by whoever made the
// motion. Nothing in this port or in the reference reads those files -- checked
// -- so this is the one fact in them worth extracting, and extracting it as a
// test rather than as a parser nothing else would call.
//
// The check matters because `kUpProbeJoints` are THIS rig's bone names and the
// shipped animations name the OLD MakeHuman skeleton, so the probe loop cannot
// match any of them. What the reader then concludes is exactly what this pins.
#include "makehuman/io/BvhReader.h"

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>
#include <string>

namespace fs = std::filesystem;

namespace {

fs::path dataDir() {
    return fs::path(MH_DATA_DIR);
}

/// `# anim <Name> <file.bvh> [z_is_up]` -> {absolute bvh path, declared Z-up}.
///
/// Deliberately a test-local reader, not a library one. `.mhanim` is read by
/// nothing in this port and nothing in the reference, so a production parser
/// for it would be a format nobody consumes; the one fact worth having is the
/// axis declaration, and this is where it is needed.
std::map<fs::path, bool> declaredUpAxis() {
    std::map<fs::path, bool> declared;
    for (const auto& entry : fs::recursive_directory_iterator(dataDir() / "animations")) {
        if (entry.path().extension() != ".mhanim") continue;
        std::ifstream in(entry.path());
        std::string line;
        while (std::getline(in, line)) {
            std::istringstream fields(line);
            std::string hash;
            std::string key;
            if (!(fields >> hash >> key) || hash != "#" || key != "anim") continue;
            std::string name;
            std::string file;
            if (!(fields >> name >> file)) continue;
            std::string flag;
            bool zUp = false;
            while (fields >> flag) {
                if (flag == "z_is_up") zUp = true;
            }
            declared.emplace(entry.path().parent_path() / file, zUp);
        }
    }
    return declared;
}

}  // namespace

TEST_CASE("every shipped animation is declared by a .mhanim", "[io][bvh][upaxis][regression]") {
    // A motion file that no `.mhanim` mentions would silently skip the check
    // below, so the coverage is pinned before the claim is.
    const auto declared = declaredUpAxis();
    size_t bvhFiles     = 0;
    for (const auto& entry : fs::recursive_directory_iterator(dataDir() / "animations")) {
        if (entry.path().extension() != ".bvh") continue;
        ++bvhFiles;
        INFO(entry.path().filename().string() << " is not named by any .mhanim");
        CHECK(declared.contains(entry.path()));
    }
    // MEASURED: dance1, walk1, zombieWalk1.
    CHECK(bvhFiles == 3);
    CHECK(declared.size() == 3);
}

TEST_CASE("the reader's up-axis guess agrees with what the file declares",
          "[io][bvh][upaxis][regression]") {
    // THE cross-check. `convertedFromZUp` is the reader's own conclusion; the
    // `.mhanim` flag is the author's. They must agree, or one of them is wrong
    // about a file that ships.
    const auto declared = declaredUpAxis();
    // Not vacuous: an empty map -- data moved, the `# anim` spelling changed,
    // a partial mis-parse -- would run this loop zero times and report green
    // while checking nothing.
    //
    // SURVIVING MUTATION, recorded: deleting this line breaks no test, because
    // the map is never empty with the data that ships. It is a guard against a
    // failure that cannot be induced without corrupting `data/`, which is the
    // same shape as every must-not-happen assertion -- it earns its place by
    // what it catches the day the parse does break, not by what it catches
    // today.
    REQUIRE(declared.size() == 3);

    for (const auto& [bvh, declaredZUp] : declared) {
        const auto file = mh::io::readBvh(bvh, {});
        REQUIRE(file.has_value());
        INFO(bvh.filename().string()
             << ": .mhanim says z_is_up=" << declaredZUp
             << ", readBvh says convertedFromZUp=" << file->convertedFromZUp);
        CHECK(file->convertedFromZUp == declaredZUp);
    }
}

TEST_CASE("an explicit up axis overrides the guess", "[io][bvh][upaxis][regression]") {
    // The escape hatch, pinned: whatever the heuristic concludes, a caller who
    // KNOWS can say so. This is what a fix would lean on if the guess and the
    // declaration ever disagree for a shipped file.
    const auto walk = dataDir() / "animations" / "walks" / "walk1.bvh";

    const auto forced = mh::io::readBvh(walk, {.upAxis = mh::io::UpAxis::ZUp});
    REQUIRE(forced.has_value());
    CHECK(forced->convertedFromZUp);

    const auto asIs = mh::io::readBvh(walk, {.upAxis = mh::io::UpAxis::YUp});
    REQUIRE(asIs.has_value());
    CHECK_FALSE(asIs->convertedFromZUp);
}

TEST_CASE("a Y-up rig with foreign joint names is left alone", "[io][bvh][upaxis][regression]") {
    // The direction this heuristic can now get WRONG, and which no shipped file
    // can cover: every BVH under data/ measures Z-up, so nothing here would
    // notice `Auto` rotating a Y-up file. Written out rather than assumed.
    //
    // The names are deliberately foreign -- the old measurement looked for this
    // rig's bone names and left anything else alone, so a file like this used
    // to be safe by accident. It must now be safe by measurement.
    const fs::path dir = fs::temp_directory_path() / "mh_upaxis_yup";
    fs::create_directories(dir);
    const fs::path bvh = dir / "yup.bvh";
    {
        std::ofstream out(bvh);
        out << "HIERARCHY\n"
               "ROOT Pelvis\n{\n  OFFSET 0 0 0\n"
               "  CHANNELS 6 Xposition Yposition Zposition Zrotation Xrotation Yrotation\n"
               "  JOINT Torso\n  {\n    OFFSET 0 40 0\n"
               "    CHANNELS 3 Zrotation Xrotation Yrotation\n"
               "    JOINT Noggin\n    {\n      OFFSET 0 30 0\n"
               "      CHANNELS 3 Zrotation Xrotation Yrotation\n"
               "      End Site\n      {\n        OFFSET 0 10 0\n      }\n"
               "    }\n  }\n}\n"
               "MOTION\nFrames: 1\nFrame Time: 0.041667\n"
               "0 0 0 0 0 0 0 0 0 0 0 0\n";
    }

    const auto file = mh::io::readBvh(bvh, {});
    REQUIRE(file.has_value());
    // 80 of vertical extent against 0 of depth: unambiguously Y-up, and the
    // reader must not rotate it.
    CHECK_FALSE(file->convertedFromZUp);

    std::error_code ec;
    fs::remove_all(dir, ec);
}
