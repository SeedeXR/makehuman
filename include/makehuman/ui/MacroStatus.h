// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <QString>

namespace mh::ui {

/// The numbers behind the reference's persistent status line.
///
/// Plain floats, NOT a `core::Human`. `mh_ui` is Apache-2.0 and must never
/// depend on an AGPL module (CLAUDE.md hard rule 4, LICENSING.md 4), and
/// `core` is AGPL — so the app, which owns the character, reads them out and
/// passes them in. That is the same shape as the File-menu signals: this module
/// formats, it does not know what a human is.
struct MacroStats {
    /// 0..1, the raw macro scalar. The reference branches on the EXACT
    /// endpoints, so this is deliberately not pre-classified into a label.
    float gender{0.5F};
    /// Already in years — the 1..25 / 25..90 split lives in
    /// `MacroFactors::ageYears()`, not here.
    float ageYears{25.0F};
    /// 0..1.
    float muscle{0.5F};
    /// 0..1.
    float weight{0.5F};
    /// Centimetres, i.e. 10x the mesh's Y extent in the internal decimetre
    /// units (`legacy/python/apps/human.py:694-700`).
    float heightCm{0.0F};
};

/// The status line, formatted as the reference formats it
/// (`legacy/python/apps/gui/guimodifier.py:152-185`).
///
/// Two details here are NOT what a reasonable guess produces, and both are
/// copied deliberately:
///
///   * **Weight is `50 + 100 * weight`, not `100 * weight`.** The slider's 0..1
///     maps onto a 50 %..150 % display range, so a default character reads
///     "100.00 %" and not "50.00 %".
///   * **Gender has four branches, and the two endpoints are compared
///     exactly.** 0.0 is "female", 1.0 is "male", within 0.01 of 0.5 is
///     "neutral", and everything else is the split percentage — so 0.999 reads
///     "0.10 % female, 99.90 % male" rather than "male".
///
/// Strings go through `QCoreApplication::translate`, so a language change
/// restyles them like every other UI string.
[[nodiscard]] QString macroStatusLine(const MacroStats& stats);

/// Just the gender clause, exposed because it carries all four branches and is
/// the part worth testing at its boundaries.
[[nodiscard]] QString genderLabel(float gender);

}  // namespace mh::ui
