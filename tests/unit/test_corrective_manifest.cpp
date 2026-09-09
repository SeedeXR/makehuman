// SPDX-License-Identifier: AGPL-3.0-or-later
//
// The corrective authoring manifest: stage one of owner directive 12.4's three
// layers.
//
//   AUTHORING  this manifest plus the existing sparse `.target` payloads.
//              Diffable, hand-editable, git-reviewable.
//   COMPILE    solve the RBF offline and bake an mmap-able blob.
//   RUNTIME    the blob only, a disposable cache invalidated on hash mismatch.
//
// So this file tests a READER, not a solver. It turns text into the four things
// the pieces already built need: which joints drive, which component of each
// (`foundation::swingTwist`), where each example pose sits in signal space and
// what it sculpts (`foundation::rbfSolve`, `core::CorrectiveBuffer`).
//
// The directive asks for the manifest format to be versioned CONSERVATIVELY and
// the blob format cheaply and aggressively -- so `formatVersion` is refused
// rather than best-guessed when unknown, which is most of what versioning is
// for.
//
// JSON rather than TOML, and that is a licensing consequence rather than a
// preference: nlohmann/json is already a recorded dependency (LICENSING.md 5.1)
// and a TOML library would be a new one. Hard rule 6 says no new dependency
// without recording it, and there is nothing TOML buys here that would justify
// the entry.
#include "makehuman/core/CorrectiveManifest.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <filesystem>
#include <fstream>
#include <string>

using namespace mh::core;
using Catch::Matchers::WithinAbs;

namespace {

/// Writes @p text to a scratch file and returns the path.
///
/// Each case gets its own directory: the loader resolves payload paths against
/// the manifest's own directory, so a shared one would let a stale file from
/// another case satisfy a lookup.
std::filesystem::path writeManifest(const std::string& name, const std::string& text) {
    const auto dir = std::filesystem::temp_directory_path() / "mh_manifest_tests" / name;
    std::filesystem::create_directories(dir);
    const auto path = dir / "correctives.json";
    std::ofstream out(path);
    out << text;
    out.close();
    return path;
}

/// A manifest that loads, so each failing case can differ from it by one thing.
const char* kGood = R"({
  "formatVersion": 1,
  "topologyHash": "e38c060123b5d0db",
  "kernel": "gaussian",
  "radius": 0.9,
  "drivers": [
    { "joint": "upperarm01.L", "component": "swing" },
    { "joint": "lowerarm01.L", "component": "twist" }
  ],
  "poses": [
    { "name": "arm_up",   "signal": [0.0, 1.2, 0.0, 0.4], "delta": "deltas/arm_up.target" },
    { "name": "arm_back", "signal": [0.3, 0.0, 0.8, 0.0], "delta": "deltas/arm_back.target" }
  ]
})";

}  // namespace

TEST_CASE("a well-formed manifest loads into what the runtime needs", "[core][manifest]") {
    const auto path = writeManifest("good", kGood);
    const auto m    = loadCorrectiveManifest(path);
    REQUIRE(m.has_value());

    CHECK(m->formatVersion == 1);
    CHECK(m->topologyHash == 0xe38c060123b5d0dbULL);
    CHECK(m->radius == 0.9);

    REQUIRE(m->drivers.size() == 2);
    CHECK(m->drivers[0].joint == "upperarm01.L");
    CHECK(m->drivers[0].component == DriverComponent::Swing);
    CHECK(m->drivers[1].joint == "lowerarm01.L");
    CHECK(m->drivers[1].component == DriverComponent::Twist);

    // THE number everything downstream is shaped by: a swing contributes the
    // three components of its rotation vector, a twist contributes one signed
    // angle. Derived rather than declared, so a manifest cannot claim a
    // dimension its own drivers do not add up to.
    CHECK(m->dimension() == 4);

    REQUIRE(m->poses.size() == 2);
    CHECK(m->poses[0].name == "arm_up");
    REQUIRE(m->poses[0].signal.size() == 4);
    CHECK_THAT(m->poses[0].signal[1], WithinAbs(1.2, 1e-12));

    // Payload paths come back resolved against the manifest's own directory, so
    // a caller never has to know where the manifest was.
    CHECK(m->poses[0].delta == path.parent_path() / "deltas" / "arm_up.target");
    CHECK(m->poses[1].delta == path.parent_path() / "deltas" / "arm_back.target");
}

TEST_CASE("the file itself has to be readable", "[core][manifest]") {
    SECTION("missing") {
        const auto m = loadCorrectiveManifest(std::filesystem::temp_directory_path() /
                                              "mh_manifest_tests" / "nope.json");
        REQUIRE_FALSE(m.has_value());
        CHECK(m.error().kind == CorrectiveManifestErrorKind::NotFound);
    }

    SECTION("a directory is not a manifest") {
        // The trap `foundation::openForRead` exists for: a directory satisfies
        // exists() and then parses as an empty file, and eight readers in this
        // project accepted one before that was fixed.
        const auto dir = std::filesystem::temp_directory_path() / "mh_manifest_tests" / "as_dir";
        std::filesystem::create_directories(dir);
        const auto m = loadCorrectiveManifest(dir);
        REQUIRE_FALSE(m.has_value());
        CHECK(m.error().kind == CorrectiveManifestErrorKind::Unreadable);
    }

    SECTION("not JSON at all") {
        const auto m = loadCorrectiveManifest(writeManifest("garbage", "{ this is not json"));
        REQUIRE_FALSE(m.has_value());
        CHECK(m.error().kind == CorrectiveManifestErrorKind::Malformed);
    }

    SECTION("JSON, but not an object") {
        const auto m = loadCorrectiveManifest(writeManifest("array", "[1, 2, 3]"));
        REQUIRE_FALSE(m.has_value());
        CHECK(m.error().kind == CorrectiveManifestErrorKind::Malformed);
    }
}

TEST_CASE("an unknown format version is refused, not guessed at", "[core][manifest]") {
    // What versioning is for. Directive 12.4 asks for the manifest format to be
    // versioned CONSERVATIVELY, and reading a version this build does not know
    // means silently ignoring whatever the newer one added -- which for a
    // corrective is geometry that quietly does not appear.
    SECTION("a future version") {
        std::string text = kGood;
        text.replace(text.find("\"formatVersion\": 1"), 18, "\"formatVersion\": 2");
        const auto m = loadCorrectiveManifest(writeManifest("v2", text));
        REQUIRE_FALSE(m.has_value());
        CHECK(m.error().kind == CorrectiveManifestErrorKind::UnsupportedVersion);
    }

    SECTION("no version at all") {
        std::string text = kGood;
        text.replace(text.find("\"formatVersion\": 1,"), 19, "");
        const auto m = loadCorrectiveManifest(writeManifest("nov", text));
        REQUIRE_FALSE(m.has_value());
        CHECK(m.error().kind == CorrectiveManifestErrorKind::UnsupportedVersion);
    }
}

TEST_CASE("the topology hash is the guard against a moved base mesh", "[core][manifest]") {
    // Directive 12.7's "topology hash guard, so a base mesh edit fails loudly
    // instead of silently corrupting every delta". A `.target` is a list of
    // VERTEX INDICES: renumber the base mesh and every delta lands somewhere
    // else, with nothing to notice it. The manifest records which topology it
    // was authored against; comparing is the caller's job, but a manifest that
    // cannot state it clearly is refused here.
    SECTION("must be present") {
        std::string text = kGood;
        text.replace(text.find("\"topologyHash\": \"e38c060123b5d0db\","), 36, "");
        const auto m = loadCorrectiveManifest(writeManifest("nohash", text));
        REQUIRE_FALSE(m.has_value());
        CHECK(m.error().kind == CorrectiveManifestErrorKind::Malformed);
    }

    SECTION("must be sixteen hex digits") {
        for (const char* bad :
             {"\"e38c\"", "\"e38c060123b5d0dbe38c\"", "\"zzzzzzzzzzzzzzzz\"", "12345"}) {
            std::string text = kGood;
            text.replace(text.find("\"e38c060123b5d0db\""), 18, bad);
            const auto m = loadCorrectiveManifest(writeManifest("badhash", text));
            REQUIRE_FALSE(m.has_value());
            CHECK(m.error().kind == CorrectiveManifestErrorKind::Malformed);
        }
    }

    SECTION("upper case reads the same as lower") {
        std::string text = kGood;
        text.replace(text.find("e38c060123b5d0db"), 16, "E38C060123B5D0DB");
        const auto m = loadCorrectiveManifest(writeManifest("upperhash", text));
        REQUIRE(m.has_value());
        CHECK(m->topologyHash == 0xe38c060123b5d0dbULL);
    }
}

TEST_CASE("the kernel and its radius are checked against what exists", "[core][manifest]") {
    SECTION("an unimplemented kernel") {
        // Thin-plate is the one that would need a pivoted factorisation and,
        // with it, the Eigen use cleared in LICENSING.md 5.1.1. Naming it in a
        // manifest today has to fail rather than fall back to gaussian and
        // produce a different shape from the one authored.
        std::string text = kGood;
        text.replace(text.find("\"gaussian\""), 10, "\"thin_plate\"");
        const auto m = loadCorrectiveManifest(writeManifest("kernel", text));
        REQUIRE_FALSE(m.has_value());
        CHECK(m.error().kind == CorrectiveManifestErrorKind::UnknownKernel);
    }

    SECTION("a radius of zero or less has no kernel") {
        for (const char* bad : {"0.0", "-1.0"}) {
            std::string text = kGood;
            text.replace(text.find("\"radius\": 0.9"), 13, std::string("\"radius\": ") + bad);
            const auto m = loadCorrectiveManifest(writeManifest("radius", text));
            REQUIRE_FALSE(m.has_value());
            CHECK(m.error().kind == CorrectiveManifestErrorKind::BadRadius);
        }
    }
}

TEST_CASE("the drivers define the signal space", "[core][manifest]") {
    SECTION("at least one, or there is nothing to key on") {
        std::string text = kGood;
        const auto from  = text.find("\"drivers\": [");
        const auto to    = text.find("],", from);
        text.replace(from, to + 1 - from, "\"drivers\": []");
        const auto m = loadCorrectiveManifest(writeManifest("nodrivers", text));
        REQUIRE_FALSE(m.has_value());
        CHECK(m.error().kind == CorrectiveManifestErrorKind::NoDrivers);
    }

    SECTION("an unknown component word") {
        // "euler" specifically, because it is the thing directive 12.3 rules
        // out and the word an author coming from another rig would reach for.
        std::string text = kGood;
        text.replace(text.find("\"swing\""), 7, "\"euler\"");
        const auto m = loadCorrectiveManifest(writeManifest("component", text));
        REQUIRE_FALSE(m.has_value());
        CHECK(m.error().kind == CorrectiveManifestErrorKind::UnknownComponent);
    }

    SECTION("the same joint and component twice") {
        // Not a harmless duplicate: it would count that joint's contribution
        // twice in the signal, so distances in RBF space would be wrong in a
        // way that still solves and still renders.
        std::string text = kGood;
        text.replace(text.find("\"lowerarm01.L\""), 14, "\"upperarm01.L\"");
        text.replace(text.find("\"twist\""), 7, "\"swing\"");
        const auto m = loadCorrectiveManifest(writeManifest("dupdriver", text));
        REQUIRE_FALSE(m.has_value());
        CHECK(m.error().kind == CorrectiveManifestErrorKind::DuplicateDriver);
    }

    SECTION("the same joint under DIFFERENT components is fine") {
        // Swing and twist of one joint are independent signals, and driving
        // both from one shoulder is the ordinary case.
        std::string text = kGood;
        text.replace(text.find("\"lowerarm01.L\""), 14, "\"upperarm01.L\"");
        const auto m = loadCorrectiveManifest(writeManifest("sharedjoint", text));
        REQUIRE(m.has_value());
        CHECK(m->dimension() == 4);
    }
}

TEST_CASE("every pose states where it sits in signal space", "[core][manifest]") {
    SECTION("at least one pose") {
        std::string text = kGood;
        const auto from  = text.find("\"poses\": [");
        text.replace(from, text.rfind("]") + 1 - from, "\"poses\": []");
        const auto m = loadCorrectiveManifest(writeManifest("noposes", text));
        REQUIRE_FALSE(m.has_value());
        CHECK(m.error().kind == CorrectiveManifestErrorKind::NoPoses);
    }

    SECTION("a signal that is not the drivers' dimension") {
        // The error a hand-edited manifest actually makes: add a driver and
        // forget to extend every pose. Caught here rather than as a shape
        // mismatch out of rbfSolve, which cannot say WHICH pose is wrong.
        for (const char* bad : {"[0.0, 1.2, 0.0]", "[0.0, 1.2, 0.0, 0.4, 9.9]"}) {
            std::string text = kGood;
            text.replace(text.find("[0.0, 1.2, 0.0, 0.4]"), 20, bad);
            const auto m = loadCorrectiveManifest(writeManifest("signallen", text));
            REQUIRE_FALSE(m.has_value());
            CHECK(m.error().kind == CorrectiveManifestErrorKind::SignalDimension);
            // Named, because "a pose has the wrong signal length" in a file of
            // forty poses is not a bug report anyone can act on.
            CHECK(m.error().detail.find("arm_up") != std::string::npos);
        }
    }

    SECTION("two poses at the same point in signal space") {
        // The realistic authoring mistake, and the one that makes the
        // interpolation matrix singular. rbfSolve would refuse it as
        // NotSolvable, which is true and tells the author nothing; here it can
        // name both poses.
        std::string text = kGood;
        text.replace(text.find("[0.3, 0.0, 0.8, 0.0]"), 20, "[0.0, 1.2, 0.0, 0.4]");
        const auto m = loadCorrectiveManifest(writeManifest("duppose", text));
        REQUIRE_FALSE(m.has_value());
        CHECK(m.error().kind == CorrectiveManifestErrorKind::DuplicatePose);
        CHECK(m.error().detail.find("arm_up") != std::string::npos);
        CHECK(m.error().detail.find("arm_back") != std::string::npos);
    }

    SECTION("two poses with the same name") {
        std::string text = kGood;
        text.replace(text.find("\"arm_back\""), 10, "\"arm_up\"");
        const auto m = loadCorrectiveManifest(writeManifest("dupname", text));
        REQUIRE_FALSE(m.has_value());
        CHECK(m.error().kind == CorrectiveManifestErrorKind::DuplicatePose);
    }

    SECTION("an unnamed pose") {
        std::string text = kGood;
        text.replace(text.find("\"name\": \"arm_up\","), 17, "");
        const auto m = loadCorrectiveManifest(writeManifest("noname", text));
        REQUIRE_FALSE(m.has_value());
        CHECK(m.error().kind == CorrectiveManifestErrorKind::Malformed);
    }
}

TEST_CASE("a payload path may not leave the manifest's directory", "[core][manifest]") {
    // A manifest is data, and data files are the boundary this project already
    // guards elsewhere. Reading through `..` or an absolute path would let one
    // name any file on the machine; refusing keeps a corrective asset to the
    // directory it ships in.
    for (const char* bad :
         {"\"../../etc/passwd\"", "\"/etc/passwd\"", "\"\"", "\"deltas/../../out.target\""}) {
        std::string text = kGood;
        text.replace(text.find("\"deltas/arm_up.target\""), 22, bad);
        const auto m = loadCorrectiveManifest(writeManifest("escape", text));
        REQUIRE_FALSE(m.has_value());
        CHECK(m.error().kind == CorrectiveManifestErrorKind::BadPayloadPath);
    }

    // A subdirectory below the manifest is the ordinary case and still works,
    // so the check is not refusing every path with a slash in it.
    std::string text = kGood;
    text.replace(text.find("\"deltas/arm_up.target\""), 22, "\"deltas/left/arm_up.target\"");
    const auto m = loadCorrectiveManifest(writeManifest("subdir", text));
    REQUIRE(m.has_value());
    CHECK(m->poses[0].delta.filename() == "arm_up.target");
}

TEST_CASE("the manifest hashes to something that changes when it does", "[core][manifest]") {
    // Directive 12.4: the compiled blob is a DISPOSABLE CACHE, "invalidated on
    // manifest hash mismatch". So the manifest has to hash, and the hash has to
    // move when anything the compiler reads moves.
    const auto a = loadCorrectiveManifest(writeManifest("hash_a", kGood));
    REQUIRE(a.has_value());

    // Byte-identical content in a DIFFERENT directory hashes the same: the hash
    // is of what was authored, not of where it sits, or every checkout would
    // invalidate every cache.
    const auto b = loadCorrectiveManifest(writeManifest("hash_b", kGood));
    REQUIRE(b.has_value());
    CHECK(a->hash == b->hash);

    // ...and it moves when the content does. Each of these changes something
    // the compiler would bake.
    struct Change {
        const char* from;
        const char* to;
    };

    const Change changes[]{
        {"\"radius\": 0.9", "\"radius\": 0.8"},
        {"upperarm01.L", "upperarm02.L"},
        {"[0.0, 1.2, 0.0, 0.4]", "[0.0, 1.3, 0.0, 0.4]"},
        {"deltas/arm_up.target", "deltas/arm_dn.target"},
        {"\"arm_up\"", "\"armup\""},
    };
    for (const auto& c : changes) {
        std::string text = kGood;
        text.replace(text.find(c.from), std::string(c.from).size(), c.to);
        const auto changed = loadCorrectiveManifest(writeManifest("hash_c", text));
        REQUIRE(changed.has_value());
        CHECK(changed->hash != a->hash);
    }

    // Whitespace and key order are NOT content: reformatting a manifest must
    // not throw away a compiled cache.
    const char* reflowed = R"({"kernel":"gaussian","radius":0.9,"formatVersion":1,
  "topologyHash":"e38c060123b5d0db",
  "drivers":[{"component":"swing","joint":"upperarm01.L"},
             {"component":"twist","joint":"lowerarm01.L"}],
  "poses":[{"delta":"deltas/arm_up.target","signal":[0.0,1.2,0.0,0.4],"name":"arm_up"},
           {"name":"arm_back","signal":[0.3,0.0,0.8,0.0],"delta":"deltas/arm_back.target"}]})";
    const auto r         = loadCorrectiveManifest(writeManifest("hash_reflowed", reflowed));
    REQUIRE(r.has_value());
    CHECK(r->hash == a->hash);
}
