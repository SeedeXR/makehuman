// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <QString>

namespace mh::ui {

/// The About box, and what `--about` prints.
///
/// One function for both, because a licence stated two ways is a licence stated
/// wrongly in one of them. The reference puts this on a help TAB
/// (`plugins/6_help.py`); this port is dockable and menu-driven, so it arrives
/// as Help ▸ About and on the command line.
///
/// Every fact here is checked against the repository by ctest: the version
/// against `/VERSION`, and the two licence names against `LICENSE.CODE.md` and
/// `LICENSE.ASSETS.md`. A paragraph nobody verifies is how an application ends
/// up claiming a licence it no longer has.
[[nodiscard]] QString aboutText();

/// What this is built on and derived from.
///
/// Part of carrying the licence rather than courtesy: this is a derivative work
/// of MakeHuman and the AGPL travels with it.
[[nodiscard]] QString creditsText();

}  // namespace mh::ui
