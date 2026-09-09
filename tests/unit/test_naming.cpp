// SPDX-License-Identifier: Apache-2.0
//
// Naming profiles (owner directive 12.1).
//
// "ONE canonical identity per asset, preset and material slot, and those
// identities never change regardless of profile. Two name tables map legacy
// names and modern names onto canonical IDs. One resolver reads them."
//
// The rules this file exists to hold:
//   * the mapping is DATA, not code -- so the parser and its refusals matter;
//   * resolution tries the active profile, falls back to the other, and SAYS
//     it fell back, which is what lets old- and new-named assets coexist;
//   * a CANONICAL id always resolves, whatever the profile, because canonical
//     ids are what save files persist.

#include "makehuman/foundation/Naming.h"

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <string>

using namespace mh;
using foundation::NamingProfile;

namespace {

std::filesystem::path writeTable(const std::string& name, const std::string& body) {
    const auto p = std::filesystem::temp_directory_path() / ("mh_names_" + name + ".names");
    std::ofstream(p) << body;
    return p;
}

const char* kGood = R"(# canonical            legacy      modern
workspace.modelling    Modelling   Modelling
workspace.assets       Materials   Assets

# a comment after a blank line
workspace.export       Export      Export
)";

}  // namespace

TEST_CASE("a table parses into canonical ids", "[foundation][naming]") {
    const auto table = foundation::loadNameTable(writeTable("good", kGood));
    REQUIRE(table.has_value());
    CHECK(table->size() == 3);
}

TEST_CASE("the active profile's own name resolves without falling back", "[foundation][naming]") {
    const auto t = foundation::loadNameTable(writeTable("good", kGood));
    REQUIRE(t.has_value());

    const auto legacy = foundation::resolve(*t, NamingProfile::Legacy, "Materials");
    REQUIRE(legacy.has_value());
    CHECK(legacy->canonical == "workspace.assets");
    CHECK_FALSE(legacy->viaFallback);

    const auto modern = foundation::resolve(*t, NamingProfile::Modern, "Assets");
    REQUIRE(modern.has_value());
    CHECK(modern->canonical == "workspace.assets");
    CHECK_FALSE(modern->viaFallback);
}

TEST_CASE("the OTHER profile's name still resolves, and says so", "[foundation][naming]") {
    // This is what makes migration painless: a workspace holding old- and
    // new-named things keeps working, and the caller is told once so the user
    // learns the new name rather than being stopped by it.
    const auto t = foundation::loadNameTable(writeTable("good", kGood));
    REQUIRE(t.has_value());

    const auto modernNameInLegacy = foundation::resolve(*t, NamingProfile::Legacy, "Assets");
    REQUIRE(modernNameInLegacy.has_value());
    CHECK(modernNameInLegacy->canonical == "workspace.assets");
    CHECK(modernNameInLegacy->viaFallback);

    const auto legacyNameInModern = foundation::resolve(*t, NamingProfile::Modern, "Materials");
    REQUIRE(legacyNameInModern.has_value());
    CHECK(legacyNameInModern->canonical == "workspace.assets");
    CHECK(legacyNameInModern->viaFallback);
}

TEST_CASE("a name that is the same in both profiles never reports a fallback",
          "[foundation][naming]") {
    // Most names do not change. Reporting a fallback for them would warn about
    // nothing on almost every lookup and train the user to ignore the warning.
    const auto t = foundation::loadNameTable(writeTable("good", kGood));
    REQUIRE(t.has_value());
    for (const NamingProfile p : {NamingProfile::Legacy, NamingProfile::Modern}) {
        const auto r = foundation::resolve(*t, p, "Modelling");
        REQUIRE(r.has_value());
        CHECK(r->canonical == "workspace.modelling");
        CHECK_FALSE(r->viaFallback);
    }
}

TEST_CASE("a canonical id resolves to itself in every profile", "[foundation][naming]") {
    // Canonical ids are what save files persist, so loading one must work
    // whatever profile the reader is in -- "a file saved in legacy mode and
    // opened in modern mode becomes a support ticket" otherwise.
    const auto t = foundation::loadNameTable(writeTable("good", kGood));
    REQUIRE(t.has_value());
    for (const NamingProfile p : {NamingProfile::Legacy, NamingProfile::Modern}) {
        const auto r = foundation::resolve(*t, p, "workspace.assets");
        REQUIRE(r.has_value());
        CHECK(r->canonical == "workspace.assets");
        CHECK_FALSE(r->viaFallback);
    }
}

TEST_CASE("an unknown name resolves to nothing", "[foundation][naming]") {
    const auto t = foundation::loadNameTable(writeTable("good", kGood));
    REQUIRE(t.has_value());
    CHECK_FALSE(foundation::resolve(*t, NamingProfile::Legacy, "Nonsense").has_value());
}

TEST_CASE("the display name is the active profile's", "[foundation][naming]") {
    const auto t = foundation::loadNameTable(writeTable("good", kGood));
    REQUIRE(t.has_value());
    CHECK(foundation::displayName(*t, NamingProfile::Legacy, "workspace.assets") == "Materials");
    CHECK(foundation::displayName(*t, NamingProfile::Modern, "workspace.assets") == "Assets");
    CHECK_FALSE(foundation::displayName(*t, NamingProfile::Modern, "workspace.nope").has_value());
}

TEST_CASE("a malformed table is refused, with the line", "[foundation][naming]") {
    // The table is DATA, so its parser is a trust boundary: a row silently
    // dropped is a name that stops resolving, and the failure surfaces
    // somewhere else entirely.
    using foundation::NameErrorKind;

    SECTION("too few columns") {
        const auto t = foundation::loadNameTable(writeTable("short", "a.b  Legacy\n"));
        REQUIRE_FALSE(t.has_value());
        CHECK(t.error().kind == NameErrorKind::Malformed);
        CHECK(t.error().line == 1);
    }
    SECTION("too many columns") {
        const auto t = foundation::loadNameTable(writeTable("long", "a.b  L  M  extra\n"));
        REQUIRE_FALSE(t.has_value());
        CHECK(t.error().kind == NameErrorKind::Malformed);
    }
    SECTION("a duplicate canonical id") {
        const auto t = foundation::loadNameTable(writeTable("dupc", "a.b  L  M\na.b  X  Y\n"));
        REQUIRE_FALSE(t.has_value());
        CHECK(t.error().kind == NameErrorKind::Duplicate);
        CHECK(t.error().line == 2);
    }
    SECTION("a duplicate legacy name") {
        // Two canonical ids answering to one name is a lookup with no answer.
        const auto t = foundation::loadNameTable(writeTable("dupl", "a.b  L  M\nc.d  L  N\n"));
        REQUIRE_FALSE(t.has_value());
        CHECK(t.error().kind == NameErrorKind::Duplicate);
    }
    SECTION("a duplicate modern name") {
        const auto t = foundation::loadNameTable(writeTable("dupm", "a.b  L  M\nc.d  N  M\n"));
        REQUIRE_FALSE(t.has_value());
        CHECK(t.error().kind == NameErrorKind::Duplicate);
    }
    SECTION("a display name that collides with another row's canonical id") {
        // Canonical ids resolve to themselves, so a display name equal to a
        // DIFFERENT row's canonical id has two answers.
        const auto t = foundation::loadNameTable(writeTable("colc", "a.b  L  M\nc.d  a.b  N\n"));
        REQUIRE_FALSE(t.has_value());
        CHECK(t.error().kind == NameErrorKind::Duplicate);
    }
    SECTION("a missing file") {
        const auto t = foundation::loadNameTable("/nonexistent/names.names");
        REQUIRE_FALSE(t.has_value());
        CHECK(t.error().kind == NameErrorKind::Unreadable);
    }
}

TEST_CASE("the shipped workspace table maps Materials to Assets", "[foundation][naming][slow]") {
    // The rename the owner asked for in step 1, as DATA. Legacy keeps
    // "Materials" -- the default -- and modern reads "Assets"; both resolve.
    const auto t = foundation::loadNameTable(std::filesystem::path(MH_DATA_DIR) / "naming" /
                                             "workspace.names");
    REQUIRE(t.has_value());
    CHECK(foundation::displayName(*t, NamingProfile::Legacy, "workspace.assets") == "Materials");
    CHECK(foundation::displayName(*t, NamingProfile::Modern, "workspace.assets") == "Assets");

    // Every shipped preset is in the table, or `--workspace` would stop
    // resolving one of them.
    for (const char* id : {"workspace.modelling", "workspace.rigging", "workspace.assets",
                           "workspace.export", "workspace.tabbed"}) {
        INFO(id);
        CHECK(foundation::displayName(*t, NamingProfile::Legacy, id).has_value());
    }
}
