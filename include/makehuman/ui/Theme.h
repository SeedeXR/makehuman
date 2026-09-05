// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <QColor>
#include <QIcon>
#include <QString>

#include <filesystem>
#include <string_view>

namespace mh::ui::theme {

/// The colour tokens from `memory/design.md` §3, in one place.
///
/// Every colour the UI uses comes from here. That is what makes a light theme a
/// table swap rather than a refactor, and it is what lets the contrast tests
/// check the real values instead of a copy of them.
struct Palette {
    QColor bgViewport{0x1a, 0x1a, 0x1c};
    QColor bgViewportFar{0x0f, 0x0f, 0x11};
    QColor bgBase{0x21, 0x21, 0x24};
    QColor bgPanel{0x2a, 0x2a, 0x2e};
    QColor bgElevated{0x32, 0x32, 0x38};
    QColor bgInput{0x1c, 0x1c, 0x1f};
    QColor bgHover{0x3a, 0x3a, 0x41};
    QColor bgActive{0x45, 0x45, 0x4e};
    QColor borderSubtle{0x35, 0x35, 0x3b};
    QColor borderStrong{0x4a, 0x4a, 0x52};

    QColor textPrimary{0xec, 0xec, 0xee};
    QColor textSecondary{0xa8, 0xa8, 0xb0};
    QColor textTertiary{0x76, 0x76, 0x7e};
    QColor textDisabled{0x5a, 0x5a, 0x62};

    QColor accent{0xf5, 0x82, 0x20};
    QColor accentHover{0xff, 0x96, 0x42};
    QColor accentPress{0xd9, 0x6a, 0x10};

    QColor success{0x4e, 0xa8, 0x7a};
    QColor warning{0xe0, 0xa3, 0x3e};
    QColor danger{0xe0, 0x5c, 0x5c};
    QColor info{0x5b, 0x9d, 0xd9};
};

[[nodiscard]] const Palette& palette();

/// WCAG 2.1 contrast ratio, 1.0 (identical) to 21.0 (black on white).
///
/// Spelled out rather than taken from Qt because Qt has no such function and
/// because the accessibility claims in `design.md` are only worth something if
/// something checks them.
[[nodiscard]] double contrastRatio(const QColor& a, const QColor& b);

/// The application stylesheet, built from the tokens above.
[[nodiscard]] QString styleSheet();

/// Whether the user has asked for reduced motion (`design.md` §9).
///
/// Implemented in `Motion.mm`, the project's only Objective-C++ file; the
/// rationale lives there.
/// Whether the user has asked for reduced motion.
///
/// `MH_REDUCE_MOTION` overrides it when set (`1`/`t`/`y` for on, anything else
/// for off); unset asks macOS. The override exists because the system answer
/// cannot be changed from a test or a build box without changing a real user
/// preference, so it is the only way the ON branch is ever exercised -- and it
/// is a useful escape hatch in its own right.
[[nodiscard]] bool reduceMotion();

/// Registers the bundled typefaces.
///
/// @return the family name Qt actually installed, or empty if the font could
///         not be loaded -- which is a legitimate outcome (the caller falls
///         back to the system sans), not an error worth aborting for.
QString installFonts(const std::filesystem::path& fontDir);

/// A Lucide glyph recoloured to @p colour at @p px square.
///
/// Lucide ships `stroke="currentColor"`, which SVG resolves from CSS inheritance
/// that QSvgRenderer does not implement -- rendered as-is every icon comes out
/// black. The token colour is substituted into the source before rendering, so
/// one asset serves every state.
///
/// @return a null QIcon if the file is missing; callers show nothing rather
///         than a black square.
[[nodiscard]] QIcon icon(std::string_view name, const QColor& colour, int px = 20);

/// Where `icon()` looks. Set once at start-up.
void setIconDir(std::filesystem::path dir);
[[nodiscard]] const std::filesystem::path& iconDir();

}  // namespace mh::ui::theme
