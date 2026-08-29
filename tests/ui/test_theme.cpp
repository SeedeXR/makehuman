// SPDX-License-Identifier: AGPL-3.0-or-later
//
// The design system, checked rather than described. `memory/design.md` makes
// specific, falsifiable claims -- WCAG ratios, a 28-entry icon map, a bundled
// typeface -- and a design token table that nothing verifies drifts from the
// document that defines it within a release.
#include "makehuman/core/Modifier.h"
#include "makehuman/core/SliderLayout.h"
#include "makehuman/ui/AssetPanel.h"
#include "makehuman/ui/MainWindow.h"
#include "makehuman/ui/ModifierPanel.h"
#include "makehuman/ui/PanelTitleBar.h"
#include "makehuman/ui/Theme.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <QComboBox>
#include <QDockWidget>
#include <QImage>
#include <QLabel>
#include <QMenu>
#include <QPushButton>
#include <QRegularExpression>
#include <QSet>
#include <QSlider>
#include <QTabWidget>
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

// --- the modifier panel (M8 task registry + sliders) ------------------------

namespace {

/// Catch2's float matchers take double; widening at the call site keeps
/// -Wdouble-promotion on without weakening it.
constexpr double d(float v) {
    return static_cast<double>(v);
}

/// A small, hand-built layout: two views, three sections, four sliders, with
/// both range shapes ([0,1] and [-1,1]) and a non-zero default. Using the real
/// 291 here would make a failure hard to read and the test slow.
std::vector<mh::foundation::TaskViewSpec> toyLayout() {
    using mh::foundation::SliderSection;
    using mh::foundation::SliderSpec;
    using mh::foundation::TaskViewSpec;

    TaskViewSpec face;
    face.name = "Face";
    SliderSection head{"head shape", {}};
    head.sliders.push_back(
        SliderSpec{"head/head-age-decr|incr", "Age", "frontView", -1.0F, 1.0F, 0.0F});
    head.sliders.push_back(SliderSpec{"head/head-oval", "Oval", "", 0.0F, 1.0F, 0.0F});
    SliderSection nose{"nose", {}};
    nose.sliders.push_back(
        SliderSpec{"nose/nose-scale-depth-decr|incr", "Scale Depth", "", -1.0F, 1.0F, 0.25F});
    face.sections = {head, nose};

    TaskViewSpec gender;
    gender.name = "Gender";
    SliderSection macro{"macro", {}};
    macro.sliders.push_back(SliderSpec{"macrodetails/Gender", "Gender", "", 0.0F, 1.0F, 0.5F});
    gender.sections = {macro};

    return {face, gender};
}

}  // namespace

TEST_CASE("the panel builds a tab per view and a slider per spec", "[modifiers]") {
    const auto layout = toyLayout();
    mh::ui::ModifierPanel panel(layout);

    CHECK(panel.tabs()->count() == 2);
    CHECK(panel.tabs()->tabText(0) == QStringLiteral("Face"));
    CHECK(panel.tabs()->tabText(1) == QStringLiteral("Gender"));
    CHECK(panel.sliderCount() == 4);
}

TEST_CASE("a slider starts at its default, including a non-zero one", "[modifiers]") {
    const auto layout = toyLayout();
    mh::ui::ModifierPanel panel(layout);

    CHECK_THAT(d(panel.value(QStringLiteral("head/head-age-decr|incr"))), WithinAbs(0.0, 1e-4));
    // 0.25 on a [-1,1] range is the case a naive 0..1 mapping gets wrong.
    CHECK_THAT(d(panel.value(QStringLiteral("nose/nose-scale-depth-decr|incr"))),
               WithinAbs(0.25, 1e-3));
    CHECK_THAT(d(panel.value(QStringLiteral("macrodetails/Gender"))), WithinAbs(0.5, 1e-3));
}

TEST_CASE("moving a slider emits its id and mapped value", "[modifiers]") {
    const auto layout = toyLayout();
    mh::ui::ModifierPanel panel(layout);

    QString gotId;
    float gotValue = 0.0F;
    int emissions  = 0;
    QObject::connect(&panel, &mh::ui::ModifierPanel::valueChanged, [&](const QString& id, float v) {
        gotId    = id;
        gotValue = v;
        ++emissions;
    });

    auto* slider = panel.findChild<QSlider*>(QStringLiteral("slider:head/head-age-decr|incr"));
    REQUIRE(slider != nullptr);

    slider->setValue(slider->maximum());
    CHECK(emissions == 1);
    CHECK(gotId == QStringLiteral("head/head-age-decr|incr"));
    CHECK_THAT(d(gotValue), WithinAbs(1.0, 1e-4));

    slider->setValue(slider->minimum());
    CHECK_THAT(d(gotValue), WithinAbs(-1.0, 1e-4));

    // The midpoint must be exactly 0 on a symmetric range, not 0.001 -- a
    // default that cannot be returned to is the bug this pins.
    slider->setValue((slider->minimum() + slider->maximum()) / 2);
    CHECK_THAT(d(gotValue), WithinAbs(0.0, 1e-9));
}

TEST_CASE("setValue moves the slider without emitting", "[modifiers]") {
    const auto layout = toyLayout();
    mh::ui::ModifierPanel panel(layout);

    int emissions = 0;
    QObject::connect(&panel, &mh::ui::ModifierPanel::valueChanged,
                     [&](const QString&, float) { ++emissions; });

    panel.setValue(QStringLiteral("head/head-oval"), 0.75F);
    CHECK_THAT(d(panel.value(QStringLiteral("head/head-oval"))), WithinAbs(0.75, 1e-3));
    // Re-emitting would feed the value straight back to whatever set it.
    CHECK(emissions == 0);

    // An unknown id is ignored rather than crashing or moving something else:
    // sliderCount() could never change, so the falsifiable check is that no
    // known value moved and nothing was emitted.
    panel.setValue(QStringLiteral("no/such-modifier"), 1.0F);
    CHECK_THAT(d(panel.value(QStringLiteral("head/head-oval"))), WithinAbs(0.75, 1e-3));
    CHECK_THAT(d(panel.value(QStringLiteral("macrodetails/Gender"))), WithinAbs(0.5, 1e-3));
    CHECK(emissions == 0);
}

TEST_CASE("resetAll returns every slider to its spec default", "[modifiers]") {
    const auto layout = toyLayout();
    mh::ui::ModifierPanel panel(layout);

    panel.setValue(QStringLiteral("head/head-age-decr|incr"), -0.8F);
    panel.setValue(QStringLiteral("nose/nose-scale-depth-decr|incr"), 1.0F);
    panel.resetAll();

    CHECK_THAT(d(panel.value(QStringLiteral("head/head-age-decr|incr"))), WithinAbs(0.0, 1e-4));
    CHECK_THAT(d(panel.value(QStringLiteral("nose/nose-scale-depth-decr|incr"))),
               WithinAbs(0.25, 1e-3));
}

TEST_CASE("search filters by label and by modifier id", "[modifiers]") {
    const auto layout = toyLayout();
    mh::ui::ModifierPanel panel(layout);
    REQUIRE(panel.visibleSliderCount() == 4);

    panel.filter(QStringLiteral("oval"));  // matches a label
    CHECK(panel.visibleSliderCount() == 1);

    panel.filter(QStringLiteral("macrodetails"));  // matches an id, not a label
    CHECK(panel.visibleSliderCount() == 1);

    panel.filter(QStringLiteral("  NOSE  "));  // trimmed and case-insensitive
    CHECK(panel.visibleSliderCount() == 1);

    panel.filter(QStringLiteral("zzz-nothing"));
    CHECK(panel.visibleSliderCount() == 0);

    panel.filter(QString{});
    CHECK(panel.visibleSliderCount() == 4);
}

TEST_CASE("a section with nothing showing is hidden with its heading", "[modifiers]") {
    const auto layout = toyLayout();
    mh::ui::ModifierPanel panel(layout);

    const auto headings = panel.findChildren<QLabel*>(QStringLiteral("modifiers.section"));
    REQUIRE(headings.size() == 3);

    panel.filter(QStringLiteral("oval"));
    // Only "head shape" still has a slider; a bare heading over nothing is
    // noise, not structure.
    int visible = 0;
    for (QLabel* h : headings) {
        if (!h->parentWidget()->isHidden()) ++visible;
    }
    CHECK(visible == 1);
}

TEST_CASE("the panel carries the real 291 sliders", "[modifiers][slow]") {
    // The toy layout above proves the behaviour; this proves it survives the
    // actual shipped registry, which is the thing that will be on screen.
    std::vector<mh::core::Modifier> mods;
    for (const char* f :
         {"modeling_modifiers.json", "bodyshapes_modifiers.json", "measurement_modifiers.json"}) {
        auto m = mh::core::loadModifiers(std::filesystem::path(MH_DATA_DIR) / "modifiers" / f);
        REQUIRE(m.has_value());
        mods.insert(mods.end(), m->begin(), m->end());
    }
    std::vector<mh::foundation::TaskViewSpec> views;
    for (const char* f :
         {"modeling_sliders.json", "bodyshapes_sliders.json", "measurement_sliders.json"}) {
        auto v =
            mh::core::loadSliderLayout(std::filesystem::path(MH_DATA_DIR) / "modifiers" / f, mods);
        REQUIRE(v.has_value());
        views.insert(views.end(), v->begin(), v->end());
    }

    mh::ui::ModifierPanel panel(views);
    CHECK(panel.tabs()->count() == 7);
    CHECK(panel.sliderCount() == 291);
    CHECK(panel.visibleSliderCount() == 291);
}

// --- the asset panel (skin and pose pickers) --------------------------------

namespace {

std::vector<mh::foundation::AssetGroup> toyAssets() {
    using mh::foundation::AssetGroup;
    AssetGroup skin{"Skin", {{"/l/african.png", "African"}, {"/l/caucasian.png", "Caucasian"}}, 1};
    AssetGroup pose{"Pose", {{"rest", "A-pose (rest)"}, {"/p/tpose.bvh", "Tpose"}}, 0};
    return {skin, pose};
}

}  // namespace

TEST_CASE("the asset panel builds a picker per group with the right selection", "[assets]") {
    const auto groups = toyAssets();
    mh::ui::AssetPanel panel(groups);

    CHECK(panel.findChildren<QComboBox*>().size() == 2);
    CHECK(panel.choice(QStringLiteral("Skin")) == QStringLiteral("/l/caucasian.png"));
    CHECK(panel.choice(QStringLiteral("Pose")) == QStringLiteral("rest"));
    // An unknown group is empty rather than a crash or a wrong answer.
    CHECK(panel.choice(QStringLiteral("Hair")).isEmpty());
}

TEST_CASE("choosing emits the group and the id, not the label", "[assets]") {
    const auto groups = toyAssets();
    mh::ui::AssetPanel panel(groups);

    QString gotGroup;
    QString gotId;
    int emissions = 0;
    QObject::connect(&panel, &mh::ui::AssetPanel::chosen, [&](const QString& g, const QString& id) {
        gotGroup = g;
        gotId    = id;
        ++emissions;
    });

    auto* picker = panel.findChild<QComboBox*>(QStringLiteral("assets:Pose"));
    REQUIRE(picker != nullptr);
    picker->setCurrentIndex(1);

    CHECK(emissions == 1);
    CHECK(gotGroup == QStringLiteral("Pose"));
    // The id, so a translated or renamed label cannot break selection.
    CHECK(gotId == QStringLiteral("/p/tpose.bvh"));
}

TEST_CASE("a group with no explicit default shows its first choice", "[assets]") {
    std::vector<mh::foundation::AssetGroup> groups{{"Hair", {{"a", "A"}, {"b", "B"}}, -1}};
    mh::ui::AssetPanel panel(groups);
    // QComboBox selects index 0 on the first insert, so -1 only survives if the
    // panel does not override it -- which it does not, and that is honest:
    // "no explicit default" and "the first one" are the same thing to a combo.
    CHECK(panel.choice(QStringLiteral("Hair")) == QStringLiteral("a"));
}
