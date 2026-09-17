// SPDX-License-Identifier: Apache-2.0
#include "makehuman/ui/About.h"

#include "makehuman/foundation/Version.h"

#include <QCoreApplication>

namespace mh::ui {

namespace {

QString version() {
    return QString::fromUtf8(foundation::kVersion.data(),
                             static_cast<qsizetype>(foundation::kVersion.size()));
}

}  // namespace

QString aboutText() {
    // EVERY CLAUSE HERE IS LOAD-BEARING, and the first draft got three of them
    // wrong by being more generous than the documents it summarises. Review
    // caught all three.
    //
    //  * LICENSE.md C ends with a caveat the first draft dropped: the CC0 grant
    //    covers assets BUNDLED with MakeHuman, and a third-party asset "might
    //    be different". This application loads those -- `--custom-targets`, the
    //    Assets panel, any `.mhclo` -- so an unconditional "no obligation at
    //    all" would be telling a user the wrong thing about a downloaded
    //    CC-BY-NC garment they just exported.
    //  * LICENSING.md 4 rule 5: the combined BINARY is AGPL, and the
    //    Apache-2.0 modules are separately reusable AS SOURCE. Saying only
    //    "derivatives stay under the same licence" erases the boundary this
    //    project's hard rule 4 exists to protect.
    //  * AGPL-3.0 5 (LICENSE.CODE.md:93-98) defines what an interactive UI owes:
    //    a copyright notice, AND that there is no warranty, that licensees may
    //    convey, and how to view the licence. The first draft supplied half of
    //    one of those while calling itself a compliance surface.
    return QCoreApplication::translate(
               "About",
               "<h3>MakeHuman %1</h3>"
               "<p>A native C++ and Qt port of MakeHuman, the parametric 3D human "
               "generator.</p>"
               "<p>Copyright © 2001–2020 MakeHuman Team and contributors.</p>"
               "<p><b>What you make with it is yours.</b> The assets bundled with "
               "MakeHuman are released under Creative Commons CC0 1.0, and the project's "
               "position is that output contains no program logic — so models, renders, "
               "games and films you create carry no obligation from this licence. An "
               "asset you obtain elsewhere keeps its own licence, and abiding by it "
               "remains yours to check.</p>"
               "<p><b>The application</b> is free software under the GNU Affero General "
               "Public License, version 3 or later, and that is what the binary as a "
               "whole is. Several modules are separately available under Apache-2.0 as "
               "source; <code>LICENSING.md</code> says which.</p>"
               "<p>There is <b>no warranty</b>, to the extent permitted by law. You may "
               "convey this work under the terms of the AGPL; its full text is in "
               "<code>LICENSE.CODE.md</code>, the asset licence in "
               "<code>LICENSE.ASSETS.md</code>, and every other licence this ships "
               "under — the icons, the typeface and Qt itself — in "
               "<code>LICENSING.md</code>.</p>")
        .arg(version());
}

QString creditsText() {
    // Named because they are actually here: each of these appears in
    // LICENSING.md 5 with its licence and its reason. Deliberately not an
    // exhaustive copy of that table -- it is the authority, this is the
    // acknowledgement.
    return QCoreApplication::translate(
        "About",
        "<h3>Credits</h3>"
        "<p>This is a port of <b>MakeHuman</b>, by the MakeHuman project and its "
        "contributors. The parametric mesh, the morph targets and the rigs are theirs; "
        "so is the licence this inherits.</p>"
        "<p>Built with <b>Qt</b>, <b>Eigen</b>, <b>oneTBB</b>, <b>assimp</b>, <b>draco</b>, "
        "<b>nlohmann/json</b> and <b>Catch2</b>. Icons are <b>Lucide</b>; the interface is set "
        "in <b>42dot Sans</b>.</p>"
        "<p>Every dependency, its licence and why it is here are listed in "
        "<code>LICENSING.md</code>.</p>");
}

}  // namespace mh::ui
