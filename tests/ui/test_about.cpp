// SPDX-License-Identifier: Apache-2.0
//
// The About and Credits text, which is the whole of `HelpTaskView` that this
// port can honestly supply.
//
// The reference's help tab (`legacy/python/plugins/6_help.py`) is About/License,
// Credits, and four buttons opening upstream community URLs. This window had no
// Help menu at all and no licence surface anywhere -- which for an AGPL
// application is closer to a compliance gap than to missing chrome.
//
// What these tests hold is that the text states FACTS THE REPOSITORY CAN
// CONFIRM, rather than a paragraph someone wrote once:
//
//   * the version comes from `/VERSION` through `foundation::kVersion`;
//   * BOTH licences are named, because there are two and conflating them is
//     the misunderstanding `LICENSE.md` exists to prevent -- the code is AGPL
//     and the bundled assets are CC0;
//   * the question a user actually has ("can I sell what I make with this?")
//     is answered where they will look.
//
// The anti-drift half lives in ctest, where `--about` is matched against the
// version CMake read from `/VERSION` and the licence files are checked to still
// say what this claims. A test that merely asserted "contains 2.0.0" would pass
// a hardcoded string and rot at the next release.

#include "makehuman/ui/About.h"

#include "makehuman/foundation/Version.h"

#include <catch2/catch_test_macros.hpp>

#include <QString>

using mh::ui::aboutText;
using mh::ui::creditsText;

TEST_CASE("the about text states the version this build was made from", "[ui][about]") {
    const QString about = aboutText();
    INFO(about.toStdString());
    CHECK(about.contains(QString::fromUtf8(
        mh::foundation::kVersion.data(), static_cast<qsizetype>(mh::foundation::kVersion.size()))));
}

TEST_CASE("the about text names BOTH licences, because there are two", "[ui][about]") {
    // The code is AGPL-3.0 and the bundled assets are CC0-1.0. Naming only one
    // is the exact confusion LICENSE.md is written to prevent, and it is the
    // one that matters commercially: a user who reads "AGPL" and stops has been
    // told the opposite of the truth about what they may sell.
    const QString about = aboutText();
    INFO(about.toStdString());
    CHECK(about.contains(QStringLiteral("Affero")));
    CHECK(about.contains(QStringLiteral("CC0")));
}

TEST_CASE("the about text answers the question a user actually has", "[ui][about]") {
    // LICENSE.md D: output carries no program logic, and the assets are CC0, so
    // there is no limitation on what a user does with what they make. That is
    // the single most consequential fact in the licence and it belongs where
    // someone will look for it.
    const QString about = aboutText().toLower();
    INFO(about.toStdString());
    CHECK(about.contains(QStringLiteral("what you make")));
}

TEST_CASE("the credits name the project this is a port of", "[ui][about]") {
    // Not politeness: the AGPL obligation travels with the code, and this is a
    // derivative work of MakeHuman. Saying so is part of carrying the licence.
    const QString credits = creditsText();
    INFO(credits.toStdString());
    CHECK(credits.contains(QStringLiteral("MakeHuman")));
    CHECK(credits.contains(QStringLiteral("Qt")));
}

TEST_CASE("neither text is a stub", "[ui][about]") {
    // A gate that only asks "does it contain X" passes on a string that is
    // nothing but X. These are paragraphs a user reads.
    CHECK(aboutText().size() > 200);
    CHECK(creditsText().size() > 120);
}
