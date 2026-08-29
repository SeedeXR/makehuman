// SPDX-License-Identifier: AGPL-3.0-or-later
//
// The design system, checked rather than described. `memory/design.md` makes
// specific, falsifiable claims -- WCAG ratios, a 28-entry icon map, a bundled
// typeface -- and a design token table that nothing verifies drifts from the
// document that defines it within a release.
#include "makehuman/ui/MainWindow.h"
#include "makehuman/ui/PanelTitleBar.h"
#include "makehuman/ui/Theme.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <QDockWidget>
#include <QImage>
#include <QMenu>
#include <QRegularExpression>
#include <QSet>
#include <QToolButton>

#include <memory>

#include <filesystem>
#include <string>
#include <vector>

using Catch::Matchers::WithinAbs;
namespace theme = mh::ui::theme;

namespace {

/// Every colour in the palette. Hand-listed: a token added to the struct is NOT
/// picked up here automatically, so add it below too.
std::vector<QColor> allTokens() {
    const theme::Palette& p = theme::palette();
    return {p.bgViewport,  p.bgViewportFar, p.bgBase,       p.bgPanel,      p.bgElevated,
            p.bgInput,     p.bgHover,       p.bgActive,     p.borderSubtle, p.borderStrong,
            p.textPrimary, p.textSecondary, p.textTertiary, p.textDisabled, p.accent,
            p.accentHover, p.accentPress,   p.success,      p.warning,      p.danger,
            p.info};
}

/// A window, its Modelling dock and that dock's title bar -- the preamble all
/// four panel tests need.
struct Panel {
    mh::ui::MainWindow window{MH_SHADER_DIR};
    QDockWidget* dock{};
    mh::ui::PanelTitleBar* bar{};

    Panel() {
        dock = window.findChild<QDockWidget*>(QStringLiteral("dock.modelling"));
        REQUIRE(dock != nullptr);
        bar = qobject_cast<mh::ui::PanelTitleBar*>(dock->titleBarWidget());
        REQUIRE(bar != nullptr);
    }

    /// Builds a fresh menu and fires one entry by object name.
    void trigger(const char* actionName) {
        const std::unique_ptr<QMenu> menu = bar->buildMenu();
        auto* a = menu->findChild<QAction*>(QString::fromLatin1(actionName));
        REQUIRE(a != nullptr);
        a->trigger();
    }
};

/// icon() reads from a directory the application sets at start-up; the tests
/// point it at the shipped one.
void useShippedIcons() {
    theme::setIconDir(std::filesystem::path(MH_RESOURCE_DIR) / "icons" / "lucide");
}

}  // namespace

TEST_CASE("the contrast formula agrees with the WCAG anchors", "[theme]") {
    // Black on white is 21:1 and anything against itself is 1:1. Without these
    // the ratios below would only be self-consistent, not correct.
    CHECK_THAT(theme::contrastRatio(Qt::black, Qt::white), WithinAbs(21.0, 1e-9));
    CHECK_THAT(theme::contrastRatio(Qt::white, Qt::black), WithinAbs(21.0, 1e-9));
    CHECK_THAT(theme::contrastRatio(Qt::white, Qt::white), WithinAbs(1.0, 1e-9));
}

TEST_CASE("text tokens meet the accessibility level design.md claims", "[theme]") {
    const theme::Palette& p = theme::palette();

    // Measured values, not the ones design.md originally carried -- those were
    // overstated (13.1 / 6.2 / 3.4). The conclusions survived; the figures did
    // not, and design.md has been corrected to these.
    CHECK_THAT(theme::contrastRatio(p.textPrimary, p.bgPanel), WithinAbs(12.12, 0.01));
    CHECK_THAT(theme::contrastRatio(p.textSecondary, p.bgPanel), WithinAbs(6.05, 0.01));
    CHECK_THAT(theme::contrastRatio(p.textTertiary, p.bgPanel), WithinAbs(3.17, 0.01));

    // The thresholds are the point; the numbers above only pin the exact value.
    CHECK(theme::contrastRatio(p.textPrimary, p.bgPanel) >= 7.0);    // AAA body
    CHECK(theme::contrastRatio(p.textSecondary, p.bgPanel) >= 4.5);  // AA body
    CHECK(theme::contrastRatio(p.textTertiary, p.bgPanel) >= 3.0);   // AA large text only
}

TEST_CASE("primary text is readable on every surface it can land on", "[theme]") {
    const theme::Palette& p = theme::palette();
    for (const QColor& surface :
         {p.bgBase, p.bgPanel, p.bgElevated, p.bgInput, p.bgHover, p.bgActive, p.bgViewport}) {
        INFO("surface " << surface.name().toStdString());
        CHECK(theme::contrastRatio(p.textPrimary, surface) >= 4.5);
    }
}

TEST_CASE("the focus accent clears 3:1 against the fills it outlines", "[theme]") {
    // design.md 3: "Focus rings are 2 px --accent at 3:1 against any adjacent
    // fill." A focus ring nobody can see is not an accessible focus ring.
    const theme::Palette& p = theme::palette();
    for (const QColor& fill : {p.bgBase, p.bgPanel, p.bgElevated, p.bgInput, p.bgHover}) {
        INFO("fill " << fill.name().toStdString());
        CHECK(theme::contrastRatio(p.accent, fill) >= 3.0);
    }
}

TEST_CASE("every token is a distinct colour", "[theme]") {
    // Two tokens with the same value means one of them is not doing anything,
    // and a copy-paste slip in a 21-entry table is invisible by eye.
    QSet<QRgb> seen;
    for (const QColor& c : allTokens())
        seen.insert(c.rgb());
    CHECK(seen.size() == static_cast<int>(allTokens().size()));
}

TEST_CASE("the stylesheet is built from tokens, not literals", "[theme]") {
    const QString css = theme::styleSheet();
    REQUIRE_FALSE(css.isEmpty());

    QSet<QString> tokens;
    for (const QColor& c : allTokens())
        tokens.insert(c.name(QColor::HexRgb).toLower());

    // Any #rrggbb in the sheet must be a palette entry. A hand-typed colour is
    // exactly what makes a "token system" stop being one.
    const QRegularExpression rx(QStringLiteral("#[0-9a-fA-F]{6}"));
    auto it   = rx.globalMatch(css);
    int found = 0;
    while (it.hasNext()) {
        const QString literal = it.next().captured(0).toLower();
        INFO("stylesheet colour " << literal.toStdString() << " is not a palette token");
        CHECK(tokens.contains(literal));
        ++found;
    }
    // Guards the guard: if the regex stopped matching, the loop above would
    // pass while checking nothing.
    CHECK(found > 10);

    // An unsubstituted placeholder carries no '#', so the loop above sails past
    // it while Qt rejects the whole sheet and the app silently reverts to the
    // platform theme. That is the failure this line exists for.
    INFO(css.toStdString());
    CHECK_FALSE(css.contains(QLatin1Char('%')));
}

TEST_CASE("every icon the design map names is present", "[theme]") {
    useShippedIcons();
    // design.md 4. A missing file yields a null QIcon and a silently blank
    // button, so this is checked by name rather than discovered by listing.
    const std::vector<std::string> mapped{"folder-open",
                                          "save",
                                          "upload",
                                          "download",
                                          "undo-2",
                                          "redo-2",
                                          "rotate-ccw",
                                          "spline",
                                          "flip-horizontal-2",
                                          "camera",
                                          "settings",
                                          "sliders-horizontal",
                                          "shirt",
                                          "image",
                                          "grip-horizontal",
                                          "user",
                                          "rotate-3d",
                                          "chevron-left",
                                          "chevron-right",
                                          "chevron-up",
                                          "chevron-down",
                                          "focus",
                                          "grid-3x3",
                                          "box",
                                          "person-standing",
                                          "circle-help",
                                          "search",
                                          "palette",
                                          "bone",
                                          "globe",
                                          "x"};

    for (const std::string& name : mapped) {
        INFO("icon " << name);
        CHECK(std::filesystem::exists(theme::iconDir() / (name + ".svg")));
    }
}

TEST_CASE("an icon is recoloured to the token, not left black", "[theme]") {
    useShippedIcons();
    const QColor want = theme::palette().accent;
    const QIcon ic    = theme::icon("bone", want, 32);
    REQUIRE_FALSE(ic.isNull());

    const QImage img = ic.pixmap(32, 32).toImage().convertToFormat(QImage::Format_ARGB32);
    REQUIRE(img.width() == 32);

    // Lucide strokes are `currentColor`; rendered without substitution every
    // pixel comes out black, which still produces a perfectly valid icon.
    int opaque = 0, onHue = 0;
    for (int y = 0; y < img.height(); ++y) {
        for (int x = 0; x < img.width(); ++x) {
            const QColor px = img.pixelColor(x, y);
            if (px.alpha() < 200) continue;
            ++opaque;
            // Antialiasing shifts value but not hue.
            if (std::abs(px.hue() - want.hue()) <= 8) ++onHue;
        }
    }
    INFO("opaque pixels " << opaque << ", on the accent hue " << onHue);
    CHECK(opaque > 20);
    CHECK(onHue > opaque / 2);
}

TEST_CASE("a missing icon is null rather than a black square", "[theme]") {
    useShippedIcons();
    CHECK(theme::icon("no-such-glyph", Qt::white, 16).isNull());
}

TEST_CASE("the bundled typeface installs under the name design.md names", "[theme]") {
    const QString family = theme::installFonts(MH_RESOURCE_DIR "/fonts");
    INFO("installed family: " << family.toStdString());
    CHECK(family == QStringLiteral("42dot Sans"));
}

// --- the six-dot panel menu (design.md 6.3) --------------------------------

TEST_CASE("the panel menu offers exactly the documented entries", "[panel]") {
    useShippedIcons();
    Panel panel;

    // Both title-bar controls exist and are keyboard reachable.
    auto* grip  = panel.bar->findChild<QToolButton*>(QStringLiteral("panel.menu"));
    auto* close = panel.bar->findChild<QToolButton*>(QStringLiteral("panel.close"));
    REQUIRE(grip != nullptr);
    REQUIRE(close != nullptr);
    CHECK_FALSE(grip->icon().isNull());
    CHECK(grip->focusPolicy() != Qt::NoFocus);

    const std::unique_ptr<QMenu> menu = panel.bar->buildMenu();
    const auto has                    = [&](const char* name) {
        return menu->findChild<QAction*>(QString::fromLatin1(name)) != nullptr;
    };
    CHECK(has("panel.float"));
    CHECK(has("panel.left"));
    CHECK(has("panel.right"));
    CHECK(has("panel.tabwith"));
    CHECK(has("panel.reset"));
    CHECK(has("panel.closeAction"));

    // Top and bottom are NOT offered: these docks do not allow those areas, and
    // an entry that silently does nothing is worse than no entry.
    CHECK_FALSE(has("panel.top"));
    CHECK_FALSE(has("panel.bottom"));
}

TEST_CASE("the menu that buildMenu returns is owned by the caller", "[panel]") {
    useShippedIcons();
    // The menu must have no QObject parent. When it had one, the window and the
    // unique_ptr both owned it and only declaration order decided whether the
    // suite double-freed -- ASan caught exactly that.
    auto window = std::make_unique<mh::ui::MainWindow>(MH_SHADER_DIR);
    auto* dock  = window->findChild<QDockWidget*>(QStringLiteral("dock.modelling"));
    REQUIRE(dock != nullptr);
    auto* bar = qobject_cast<mh::ui::PanelTitleBar*>(dock->titleBarWidget());
    REQUIRE(bar != nullptr);

    std::unique_ptr<QMenu> menu = bar->buildMenu();
    REQUIRE(menu != nullptr);
    CHECK(menu->parent() == nullptr);

    // Destroying the window first must leave the menu ours to free.
    window.reset();
    menu.reset();
    SUCCEED("no double free");
}

TEST_CASE("the menu actually moves, floats and closes the panel", "[panel]") {
    useShippedIcons();
    Panel panel;
    REQUIRE(panel.window.dockWidgetArea(panel.dock) == Qt::LeftDockWidgetArea);

    panel.trigger("panel.right");
    CHECK(panel.window.dockWidgetArea(panel.dock) == Qt::RightDockWidgetArea);

    panel.trigger("panel.float");
    CHECK(panel.dock->isFloating());

    // Reset must undo BOTH -- floating and the wrong area -- not just one.
    panel.trigger("panel.reset");
    CHECK_FALSE(panel.dock->isFloating());
    CHECK(panel.window.dockWidgetArea(panel.dock) == Qt::LeftDockWidgetArea);

    panel.trigger("panel.closeAction");
    CHECK(panel.dock->isHidden());
}

TEST_CASE("Tab with lists the other panels and nothing else", "[panel]") {
    useShippedIcons();
    Panel panel;
    auto* other = panel.window.findChild<QDockWidget*>(QStringLiteral("dock.materials"));
    REQUIRE(other != nullptr);

    const std::unique_ptr<QMenu> menu = panel.bar->buildMenu();
    auto* tabWith                     = menu->findChild<QAction*>(QStringLiteral("panel.tabwith"));
    REQUIRE(tabWith != nullptr);
    REQUIRE(tabWith->menu() != nullptr);

    // Itself must not be listed -- tabifying a dock with itself is a no-op the
    // user would reasonably read as broken.
    CHECK(tabWith->menu()->actions().size() == 1);
    CHECK(tabWith->isEnabled());

    auto* entry = menu->findChild<QAction*>(QStringLiteral("panel.tabwith.dock.materials"));
    REQUIRE(entry != nullptr);
    entry->trigger();
    CHECK(panel.window.tabifiedDockWidgets(other).contains(panel.dock));
}

TEST_CASE("a Tab with entry dies with the panel it targets", "[panel]") {
    useShippedIcons();
    Panel panel;
    auto* other = panel.window.findChild<QDockWidget*>(QStringLiteral("dock.materials"));
    REQUIRE(other != nullptr);

    const std::unique_ptr<QMenu> menu = panel.bar->buildMenu();
    auto* entry = menu->findChild<QAction*>(QStringLiteral("panel.tabwith.dock.materials"));
    REQUIRE(entry != nullptr);

    // The entry's context object is `other`, so deleting it disconnects the
    // handler. With `dock` as context the action stayed live and handed a freed
    // pointer to tabifyDockWidget.
    delete other;
    entry->trigger();
    SUCCEED("triggering an orphaned entry does not use freed memory");
}

TEST_CASE("Tab with is disabled when there is nothing to tab with", "[panel]") {
    useShippedIcons();
    Panel panel;
    auto* other = panel.window.findChild<QDockWidget*>(QStringLiteral("dock.materials"));
    REQUIRE(other != nullptr);
    other->hide();

    const std::unique_ptr<QMenu> menu = panel.bar->buildMenu();
    auto* tabWith                     = menu->findChild<QAction*>(QStringLiteral("panel.tabwith"));
    REQUIRE(tabWith != nullptr);
    CHECK_FALSE(tabWith->isEnabled());
}
