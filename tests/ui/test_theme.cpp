// SPDX-License-Identifier: AGPL-3.0-or-later
//
// The design system, checked rather than described. `memory/design.md` makes
// specific, falsifiable claims -- WCAG ratios, a 28-entry icon map, a bundled
// typeface -- and a design token table that nothing verifies drifts from the
// document that defines it within a release.
#include "makehuman/core/Modifier.h"
#include "makehuman/core/SliderLayout.h"
#include "makehuman/ui/AssetPanel.h"
#include "makehuman/ui/MacroStatus.h"
#include "makehuman/ui/MainWindow.h"
#include "makehuman/ui/ModifierPanel.h"
#include "makehuman/ui/PanelTitleBar.h"
#include "makehuman/ui/TaskRegistry.h"
#include "makehuman/ui/Theme.h"
#include "makehuman/ui/UndoCommands.h"
#include "makehuman/ui/ViewportWidget.h"
#include "makehuman/ui/Workspace.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <algorithm>
#include <cmath>
#include <vector>

#include <QAbstractButton>
#include <QAbstractSlider>
#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QDockWidget>
#include <QFile>
#include <QFontMetrics>
#include <QImage>
#include <QJsonObject>
#include <QKeyEvent>
#include <QKeySequence>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMenuBar>
#include <QPainter>
#include <QPushButton>
#include <QRegularExpression>
#include <QScrollArea>
#include <QSet>
#include <QSlider>
#include <QStandardPaths>
#include <QStatusBar>
#include <QStyle>
#include <QStyleOptionSlider>
#include <QTabWidget>
#include <QTemporaryDir>
#include <QToolBar>
#include <QToolButton>
#include <QUndoStack>

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

/// QStandardPaths test mode, undone however the scope exits.
struct TestModePaths {
    TestModePaths() { QStandardPaths::setTestModeEnabled(true); }

    ~TestModePaths() { QStandardPaths::setTestModeEnabled(false); }

    TestModePaths(const TestModePaths&)            = delete;
    TestModePaths& operator=(const TestModePaths&) = delete;
};

/// The two panels the app registers, so a window built in a test has the same
/// docks the program does.
mh::ui::TaskRegistry shippedTasks() {
    mh::ui::TaskRegistry tasks;
    (void)tasks.add(QStringLiteral("Modelling"));
    (void)tasks.add(QStringLiteral("Materials"));
    return tasks;
}

/// A window, its Modelling dock and that dock's title bar -- the preamble all
/// four panel tests need.
struct Panel {
    mh::ui::MainWindow window{MH_SHADER_DIR, shippedTasks()};
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
    auto window = std::make_unique<mh::ui::MainWindow>(MH_SHADER_DIR, shippedTasks());
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

// --- the File menu ----------------------------------------------------------

TEST_CASE("the File menu offers open and save with the platform shortcuts", "[file]") {
    useShippedIcons();
    mh::ui::MainWindow w(MH_SHADER_DIR, shippedTasks());

    struct Expected {
        const char* name;
        QKeySequence::StandardKey key;
    };

    for (const Expected& e :
         {Expected{"file.open", QKeySequence::Open}, Expected{"file.save", QKeySequence::Save},
          Expected{"file.saveAs", QKeySequence::SaveAs}}) {
        INFO(e.name);
        auto* a = w.findChild<QAction*>(QString::fromLatin1(e.name));
        REQUIRE(a != nullptr);
        // QKeySequence::Save is Cmd+S on macOS and Ctrl+S elsewhere; asking for
        // the standard key rather than a literal keeps that right per platform.
        CHECK(a->shortcut() == QKeySequence(e.key));
        CHECK_FALSE(a->icon().isNull());
    }
}

TEST_CASE("triggering a File action reports intent rather than acting", "[file]") {
    useShippedIcons();
    mh::ui::MainWindow w(MH_SHADER_DIR, shippedTasks());

    int opened  = 0;
    int saved   = 0;
    int savedAs = 0;
    QObject::connect(&w, &mh::ui::MainWindow::openRequested, [&] { ++opened; });
    QObject::connect(&w, &mh::ui::MainWindow::saveRequested, [&] { ++saved; });
    QObject::connect(&w, &mh::ui::MainWindow::saveAsRequested, [&] { ++savedAs; });

    w.findChild<QAction*>(QStringLiteral("file.open"))->trigger();
    w.findChild<QAction*>(QStringLiteral("file.save"))->trigger();
    w.findChild<QAction*>(QStringLiteral("file.saveAs"))->trigger();

    // The UI must not know what a .mhm is; the app owns the character.
    CHECK(opened == 1);
    CHECK(saved == 1);
    CHECK(savedAs == 1);
}

TEST_CASE("the document path shows in the title, and clearing it restores the name", "[file]") {
    useShippedIcons();
    mh::ui::MainWindow w(MH_SHADER_DIR, shippedTasks());
    CHECK(w.windowTitle() == QStringLiteral("MakeHuman"));

    w.setDocumentPath(QStringLiteral("/tmp/some/where/hero.mhm"));
    // The file name, not the whole path -- a title bar full of directories tells
    // the user nothing they were looking for.
    CHECK(w.windowTitle().contains(QStringLiteral("hero.mhm")));
    CHECK_FALSE(w.windowTitle().contains(QStringLiteral("/tmp")));

    w.setDocumentPath(QString{});
    CHECK(w.windowTitle() == QStringLiteral("MakeHuman"));
}

// --- workspaces (design.md 6.4) ---------------------------------------------

TEST_CASE("a workspace survives the JSON round trip", "[workspace]") {
    mh::ui::WorkspaceFile in;
    in.name = QStringLiteral("Rigging");
    // Deliberately non-UTF8 bytes: these blobs are binary, which is why they are
    // base64 in the file rather than dropped into a JSON string raw.
    // sizeof - 1, not a hand-counted length: the first version said 20 for a
    // 17-byte literal and ASan caught the two-byte overread.
    static constexpr char kState[]    = "\x01\x02\xFE\xFF binary state";
    static constexpr char kGeometry[] = "\xFF\x7F geometry";
    in.state    = QByteArray(kState, static_cast<qsizetype>(sizeof(kState) - 1));
    in.geometry = QByteArray(kGeometry, static_cast<qsizetype>(sizeof(kGeometry) - 1));

    const auto back = mh::ui::workspaceFromJson(mh::ui::toJson(in));
    REQUIRE(back.has_value());
    CHECK(back->name == in.name);
    CHECK(back->state == in.state);
    CHECK(back->geometry == in.geometry);
    CHECK(back->schemaVersion == mh::ui::kWorkspaceSchemaVersion);
}

TEST_CASE("a workspace from a newer build is refused, not half-read", "[workspace]") {
    mh::ui::WorkspaceFile in;
    in.name            = QStringLiteral("Future");
    in.state           = QByteArray("x");
    QJsonObject object = mh::ui::toJson(in);

    object[QStringLiteral("schemaVersion")] = mh::ui::kWorkspaceSchemaVersion + 1;
    CHECK_FALSE(mh::ui::workspaceFromJson(object).has_value());

    object[QStringLiteral("schemaVersion")] = 0;
    CHECK_FALSE(mh::ui::workspaceFromJson(object).has_value());

    object.remove(QStringLiteral("schemaVersion"));
    CHECK_FALSE(mh::ui::workspaceFromJson(object).has_value());

    // A version that is present but not a number is not a version.
    object[QStringLiteral("schemaVersion")] = QStringLiteral("1");
    CHECK_FALSE(mh::ui::workspaceFromJson(object).has_value());
}

TEST_CASE("a corrupt state blob is refused rather than silently ignored", "[workspace]") {
    // restoreState() returns false on a bad blob and leaves the default layout,
    // which looks exactly like a successful load of a default workspace. Catch
    // it at the parse instead.
    mh::ui::WorkspaceFile in;
    in.name                         = QStringLiteral("Broken");
    QJsonObject object              = mh::ui::toJson(in);
    object[QStringLiteral("state")] = QStringLiteral("not!valid!base64!");
    CHECK_FALSE(mh::ui::workspaceFromJson(object).has_value());

    // A missing name is not a workspace either.
    QJsonObject noName = mh::ui::toJson(in);
    noName.remove(QStringLiteral("name"));
    CHECK_FALSE(mh::ui::workspaceFromJson(noName).has_value());
}

TEST_CASE("the four shipped presets are on the menu with the documented shortcuts", "[workspace]") {
    useShippedIcons();
    mh::ui::MainWindow w(MH_SHADER_DIR, shippedTasks());

    const auto& presets = mh::ui::workspacePresets();
    REQUIRE(presets.size() == 4);
    CHECK(presets[0].name == QStringLiteral("Modelling"));

    int i = 0;
    for (const auto& preset : presets) {
        const QString& name = preset.name;
        INFO(name.toStdString());
        auto* a = w.findChild<QAction*>(QStringLiteral("workspace.") + name);
        REQUIRE(a != nullptr);
        CHECK(a->shortcut() == QKeySequence(QStringLiteral("Ctrl+%1").arg(i + 1)));
        ++i;
    }
    CHECK(w.findChild<QAction*>(QStringLiteral("workspace.reset")) != nullptr);
}

TEST_CASE("a preset shows exactly the docks it names", "[workspace]") {
    useShippedIcons();
    mh::ui::MainWindow w(MH_SHADER_DIR, shippedTasks());
    auto* modelling = w.findChild<QDockWidget*>(QStringLiteral("dock.modelling"));
    auto* materials = w.findChild<QDockWidget*>(QStringLiteral("dock.materials"));
    REQUIRE(modelling != nullptr);
    REQUIRE(materials != nullptr);

    REQUIRE(w.applyWorkspacePreset(QStringLiteral("Materials")));
    CHECK(materials->isVisibleTo(&w));
    CHECK_FALSE(materials->isHidden());
    CHECK(modelling->isHidden());

    // Switching preset to preset must not accumulate. Visibility alone cannot
    // show this -- the loop sets it either way -- so move a dock first and check
    // the POSITION comes back too, which only the state restore does.
    w.addDockWidget(Qt::RightDockWidgetArea, modelling);
    REQUIRE(w.dockWidgetArea(modelling) == Qt::RightDockWidgetArea);
    REQUIRE(w.applyWorkspacePreset(QStringLiteral("Modelling")));
    CHECK(w.dockWidgetArea(modelling) == Qt::LeftDockWidgetArea);
    CHECK_FALSE(modelling->isHidden());
    CHECK_FALSE(materials->isHidden());

    // Export shows none of them.
    REQUIRE(w.applyWorkspacePreset(QStringLiteral("Export")));
    CHECK(modelling->isHidden());
    CHECK(materials->isHidden());

    CHECK_FALSE(w.applyWorkspacePreset(QStringLiteral("No Such Preset")));
}

TEST_CASE("a named workspace round-trips through the user directory", "[workspace]") {
    useShippedIcons();
    // AppDataLocation redirected so the suite never writes to the real one.
    // RAII because a failing REQUIRE below would otherwise unwind past the
    // reset and leave every later test in test mode.
    const TestModePaths redirected;

    const QString name = QStringLiteral("mh-test-workspace");
    {
        mh::ui::MainWindow w(MH_SHADER_DIR, shippedTasks());
        auto* dock = w.findChild<QDockWidget*>(QStringLiteral("dock.modelling"));
        REQUIRE(dock != nullptr);
        w.addDockWidget(Qt::RightDockWidgetArea, dock);
        REQUIRE(w.saveWorkspaceAs(name));
        CHECK(w.namedWorkspaces().contains(name));
    }

    mh::ui::MainWindow fresh(MH_SHADER_DIR, shippedTasks());
    REQUIRE(fresh.loadNamedWorkspace(name));
    auto* dock = fresh.findChild<QDockWidget*>(QStringLiteral("dock.modelling"));
    REQUIRE(dock != nullptr);
    CHECK(fresh.dockWidgetArea(dock) == Qt::RightDockWidgetArea);

    // A name that was never written is a clean failure, not a crash.
    CHECK_FALSE(fresh.loadNamedWorkspace(QStringLiteral("mh-no-such-workspace")));
    CHECK_FALSE(fresh.saveWorkspaceAs(QString{}));

    QFile::remove(mh::ui::MainWindow::workspaceDirectory() + QLatin1Char('/') + name +
                  QStringLiteral(".json"));
}

TEST_CASE("docks can nest and tab, so a drag has somewhere to snap", "[workspace]") {
    useShippedIcons();
    mh::ui::MainWindow w(MH_SHADER_DIR, shippedTasks());
    CHECK(w.isDockNestingEnabled());
    CHECK((w.dockOptions() & QMainWindow::AllowTabbedDocks) != 0);
    CHECK((w.dockOptions() & QMainWindow::AllowNestedDocks) != 0);
}

TEST_CASE("a workspace name cannot escape the workspaces directory", "[workspace]") {
    // The name reaches this from a free-text Save dialog and becomes a path.
    // `saveWorkspaceAs("../escaped")` used to write one level above the
    // workspaces directory, truncating whatever was there.
    for (const char* bad : {"../escaped", "..", ".", "a/b", "a\\b", "", "C:evil"}) {
        INFO("name \"" << bad << "\"");
        CHECK_FALSE(mh::ui::isValidWorkspaceName(QString::fromLatin1(bad)));
    }
    for (const char* good : {"Modelling", "my.layout", "with space", "Ünïcode"}) {
        INFO("name \"" << good << "\"");
        CHECK(mh::ui::isValidWorkspaceName(QString::fromUtf8(good)));
    }

    const TestModePaths redirected;
    mh::ui::MainWindow w(MH_SHADER_DIR, shippedTasks());
    CHECK_FALSE(w.saveWorkspaceAs(QStringLiteral("../escaped")));
    CHECK_FALSE(w.loadNamedWorkspace(QStringLiteral("../escaped")));
    // Nothing was created outside the directory.
    CHECK_FALSE(QFile::exists(mh::ui::MainWindow::workspaceDirectory() +
                              QStringLiteral("/../escaped.json")));
}

TEST_CASE("a workspace that restores nothing is a failure, not a success", "[workspace]") {
    // A truncated or hand-edited file used to report success while the layout
    // sat untouched -- the exact failure the base64 check exists to prevent.
    mh::ui::WorkspaceFile in;
    in.name            = QStringLiteral("Empty");
    QJsonObject object = mh::ui::toJson(in);  // state is empty
    CHECK_FALSE(mh::ui::workspaceFromJson(object).has_value());

    object.remove(QStringLiteral("state"));
    CHECK_FALSE(mh::ui::workspaceFromJson(object).has_value());
}

// --- undo/redo ---------------------------------------------------------------

TEST_CASE("a value change undoes and redoes through its callback", "[undo]") {
    QString gotKey;
    float gotValue   = -1.0F;
    int calls        = 0;
    const auto apply = [&](const QString& key, float v) {
        gotKey   = key;
        gotValue = v;
        ++calls;
    };

    QUndoStack stack;
    stack.push(new mh::ui::ValueChangeCommand(QStringLiteral("head/oval"), 0.0F, 0.8F, 0, apply));

    // QUndoStack calls redo() on push, so the value is applied immediately.
    CHECK(calls == 1);
    CHECK(gotKey == QStringLiteral("head/oval"));
    CHECK_THAT(d(gotValue), WithinAbs(0.8, 1e-6));

    stack.undo();
    CHECK_THAT(d(gotValue), WithinAbs(0.0, 1e-6));
    stack.redo();
    CHECK_THAT(d(gotValue), WithinAbs(0.8, 1e-6));
    CHECK(stack.count() == 1);
}

TEST_CASE("a drag collapses to one undo step, separate edits do not", "[undo]") {
    float value      = 0.0F;
    const auto apply = [&](const QString&, float v) { value = v; };

    QUndoStack stack;
    const QString key = QStringLiteral("head/oval");
    // One drag: many valueChanged, same merge group.
    stack.push(new mh::ui::ValueChangeCommand(key, 0.0F, 0.2F, 0, apply));
    stack.push(new mh::ui::ValueChangeCommand(key, 0.2F, 0.5F, 0, apply));
    stack.push(new mh::ui::ValueChangeCommand(key, 0.5F, 0.9F, 0, apply));
    CHECK(stack.count() == 1);

    // Undo goes all the way back to where the drag started, not to 0.5.
    stack.undo();
    CHECK_THAT(d(value), WithinAbs(0.0, 1e-6));
    stack.redo();
    CHECK_THAT(d(value), WithinAbs(0.9, 1e-6));

    // A new merge group is a new step, so two deliberate nudges stay separate.
    stack.push(new mh::ui::ValueChangeCommand(key, 0.9F, 1.0F, 1, apply));
    CHECK(stack.count() == 2);
    stack.undo();
    CHECK_THAT(d(value), WithinAbs(0.9, 1e-6));
}

TEST_CASE("different keys never merge, even in the same group", "[undo]") {
    float oval       = 0.0F;
    float age        = 0.0F;
    const auto apply = [&](const QString& key, float v) {
        (key == QStringLiteral("head/oval") ? oval : age) = v;
    };

    QUndoStack stack;
    stack.push(new mh::ui::ValueChangeCommand(QStringLiteral("head/oval"), 0.0F, 0.4F, 0, apply));
    stack.push(new mh::ui::ValueChangeCommand(QStringLiteral("head/age"), 0.0F, 0.6F, 0, apply));
    // Merging these would make undoing the second slider move the first.
    CHECK(stack.count() == 2);

    stack.undo();
    CHECK_THAT(d(age), WithinAbs(0.0, 1e-6));
    CHECK_THAT(d(oval), WithinAbs(0.4, 1e-6));
}

TEST_CASE("the Edit menu has undo and redo on the platform shortcuts", "[undo]") {
    useShippedIcons();
    mh::ui::MainWindow w(MH_SHADER_DIR, shippedTasks());
    REQUIRE(w.undoStack() != nullptr);

    auto* undo = w.findChild<QAction*>(QStringLiteral("edit.undo"));
    auto* redo = w.findChild<QAction*>(QStringLiteral("edit.redo"));
    REQUIRE(undo != nullptr);
    REQUIRE(redo != nullptr);
    CHECK(undo->shortcut() == QKeySequence(QKeySequence::Undo));
    CHECK(redo->shortcut() == QKeySequence(QKeySequence::Redo));

    // Qt disables them on an empty stack and enables them as it fills, which is
    // the behaviour createUndoAction exists to provide.
    CHECK_FALSE(undo->isEnabled());
    CHECK_FALSE(redo->isEnabled());

    float ignored = 0.0F;
    w.undoStack()->push(new mh::ui::ValueChangeCommand(
        QStringLiteral("k"), 0.0F, 1.0F, 0, [&](const QString&, float v) { ignored = v; }));
    CHECK(undo->isEnabled());
    CHECK_FALSE(redo->isEnabled());
    w.undoStack()->undo();
    CHECK(redo->isEnabled());
    CHECK_THAT(d(ignored), WithinAbs(0.0, 1e-6));
}

TEST_CASE("editingFinished lands AFTER the value it closes", "[undo]") {
    const auto layout = toyLayout();
    mh::ui::ModifierPanel panel(layout);

    // Order matters and emitting the signals by hand cannot show it:
    // QAbstractSlider::actionTriggered fires BEFORE the value lands, so closing
    // the merge group there closed it one edit too early and the next drag
    // merged into the keyboard step.
    QStringList order;
    QObject::connect(&panel, &mh::ui::ModifierPanel::valueChanged,
                     [&](const QString&, float) { order << QStringLiteral("changed"); });
    QObject::connect(&panel, &mh::ui::ModifierPanel::editingFinished,
                     [&] { order << QStringLiteral("finished"); });

    auto* slider = panel.findChild<QSlider*>(QStringLiteral("slider:head/head-oval"));
    REQUIRE(slider != nullptr);

    // triggerAction drives the real path a keyboard step takes.
    slider->triggerAction(QAbstractSlider::SliderSingleStepAdd);
    REQUIRE(order.size() == 2);
    CHECK(order[0] == QStringLiteral("changed"));
    CHECK(order[1] == QStringLiteral("finished"));

    // A drag in progress must not close the group.
    order.clear();
    slider->triggerAction(QAbstractSlider::SliderMove);
    CHECK_FALSE(order.contains(QStringLiteral("finished")));

    // A programmatic setValue is not a user edit at all.
    order.clear();
    panel.setValue(QStringLiteral("head/head-oval"), 0.5F);
    CHECK(order.isEmpty());

    // Releasing after a drag closes it.
    order.clear();
    emit slider->sliderReleased();
    CHECK(order == QStringList{QStringLiteral("finished")});
}

TEST_CASE("Reset is bracketed so it can be one undo step", "[undo]") {
    const auto layout = toyLayout();
    mh::ui::ModifierPanel panel(layout);

    QList<bool> brackets;
    int changes = 0;
    QObject::connect(&panel, &mh::ui::ModifierPanel::resetInProgress,
                     [&](bool active) { brackets << active; });
    QObject::connect(&panel, &mh::ui::ModifierPanel::valueChanged,
                     [&](const QString&, float) { ++changes; });

    panel.setValue(QStringLiteral("head/head-oval"), 0.9F);
    panel.setValue(QStringLiteral("head/head-age-decr|incr"), -0.7F);

    auto* reset = panel.findChild<QPushButton*>(QStringLiteral("modifiers.reset"));
    REQUIRE(reset != nullptr);
    reset->click();

    // Open before the first change and close after the last, so the app can
    // wrap the lot in one macro rather than leaving 291 undo steps.
    REQUIRE(brackets.size() == 2);
    CHECK(brackets[0] == true);
    CHECK(brackets[1] == false);
    CHECK(changes > 0);
}

// --- accessibility (design.md 9) ---------------------------------------------

namespace {

/// Every widget in @p root that a screen reader treats as a control.
///
/// Deliberately a type sweep rather than a hand-written list: the point is to
/// fail when a control added *later* arrives without a name.
QList<QWidget*> interactiveWidgets(QWidget* root) {
    QList<QWidget*> out;
    for (QWidget* w : root->findChildren<QWidget*>()) {
        // Everything this project creates carries an explicit object name, and
        // nothing Qt creates for us does -- its scroll bars, the clear button
        // inside a QLineEdit, the dock title buttons that survive a replaced
        // title bar. Their accessibility is Qt's business, not ours.
        if (w->objectName().isEmpty() || w->objectName().startsWith(QLatin1String("qt_"))) {
            continue;
        }
        if (qobject_cast<QAbstractSlider*>(w) || qobject_cast<QComboBox*>(w) ||
            qobject_cast<QAbstractButton*>(w) || qobject_cast<QLineEdit*>(w)) {
            out << w;
        }
    }
    return out;
}

}  // namespace

TEST_CASE("every control has an accessible name", "[a11y]") {
    useShippedIcons();
    mh::ui::MainWindow w(MH_SHADER_DIR, shippedTasks());
    const auto layout = toyLayout();
    REQUIRE(w.setPanel(QStringLiteral("Modelling"), new mh::ui::ModifierPanel(layout)));
    const auto assets = toyAssets();
    REQUIRE(w.setPanel(QStringLiteral("Materials"), new mh::ui::AssetPanel(assets)));

    const QList<QWidget*> controls = interactiveWidgets(&w);
    REQUIRE(controls.size() > 8);  // sliders, pickers, title-bar buttons, search, reset

    for (QWidget* c : controls) {
        INFO(c->metaObject()->className() << " objectName=" << c->objectName().toStdString());
        // Qt infers a name from the text of a labelled button, but an icon-only
        // button or a bare slider gets nothing, and design.md 9 requires the
        // property be set rather than inferred.
        const bool named = !c->accessibleName().isEmpty() ||
                           (qobject_cast<QAbstractButton*>(c) != nullptr &&
                            !qobject_cast<QAbstractButton*>(c)->text().isEmpty());
        CHECK(named);
    }
}

TEST_CASE("a slider announces what it changes and where it lives", "[a11y]") {
    const auto layout = toyLayout();
    mh::ui::ModifierPanel panel(layout);

    auto* slider = panel.findChild<QSlider*>(QStringLiteral("slider:head/head-oval"));
    REQUIRE(slider != nullptr);
    // "Oval" alone is ambiguous across sections, so the name carries both.
    CHECK(slider->accessibleName() == QStringLiteral("Oval, head shape"));

    // KNOWN GAP: the readout label beside the slider repeats the same number,
    // and VoiceOver announces it a second time. An empty accessibleName does
    // NOT suppress it -- Qt falls back to QLabel::text() -- so silencing it
    // needs a custom QAccessibleInterface. Recorded rather than papered over.
}

TEST_CASE("every control is reachable by keyboard", "[a11y]") {
    useShippedIcons();
    mh::ui::MainWindow w(MH_SHADER_DIR, shippedTasks());
    const auto layout = toyLayout();
    REQUIRE(w.setPanel(QStringLiteral("Modelling"), new mh::ui::ModifierPanel(layout)));

    for (QWidget* c : interactiveWidgets(&w)) {
        INFO(c->metaObject()->className() << " objectName=" << c->objectName().toStdString());
        // NoFocus means a mouse-only control -- a keyboard trap in reverse:
        // the user can never get to it at all.
        CHECK(c->focusPolicy() != Qt::NoFocus);
    }

    // The viewport is the central control and must be reachable too, or
    // orbiting is mouse-only.
    REQUIRE(w.viewport() != nullptr);
    CHECK(w.viewport()->focusPolicy() != Qt::NoFocus);
    CHECK_FALSE(w.viewport()->accessibleName().isEmpty());
}

TEST_CASE("the focus ring actually responds to focus", "[a11y]") {
    // Asserting the stylesheet CONTAINS "QSlider:focus" was vacuous: it is a
    // substring of `QSlider:focus::handle:horizontal`, which Qt silently treats
    // as unconditional -- every handle was ringed at rest and focus changed
    // nothing. Paint the widget twice instead and require the pixels to differ.
    QSlider slider(Qt::Horizontal);
    slider.setStyleSheet(mh::ui::theme::styleSheet());
    slider.resize(120, 24);

    const auto paint = [&](bool focused) {
        QImage image(slider.size(), QImage::Format_ARGB32);
        image.fill(Qt::transparent);
        QStyleOptionSlider option;
        option.initFrom(&slider);
        option.rect           = slider.rect();
        option.minimum        = slider.minimum();
        option.maximum        = slider.maximum();
        option.sliderPosition = slider.value();
        option.sliderValue    = slider.value();
        option.orientation    = Qt::Horizontal;
        option.subControls    = QStyle::SC_All;
        option.state.setFlag(QStyle::State_HasFocus, focused);
        QPainter painter(&image);
        slider.style()->drawComplexControl(QStyle::CC_Slider, &option, &painter, &slider);
        return image;
    };

    const QImage unfocused = paint(false);
    const QImage focused   = paint(true);
    CHECK(unfocused != focused);
}

TEST_CASE("the dock panels are named for a screen reader", "[a11y]") {
    useShippedIcons();
    mh::ui::MainWindow w(MH_SHADER_DIR, shippedTasks());
    for (const char* name : {"dock.modelling", "dock.materials"}) {
        auto* dock = w.findChild<QDockWidget*>(QLatin1String(name));
        REQUIRE(dock != nullptr);
        INFO(name);
        // Qt would infer this from windowTitle anyway; setting it explicitly
        // pins it against a future title change, which is what design.md §9
        // asks for ("set, not left to inference").
        CHECK_FALSE(dock->accessibleName().isEmpty());
    }
}

TEST_CASE("the viewport orbits from the keyboard", "[a11y]") {
    // design.md 9: every action reachable without a mouse. Giving the viewport
    // focus without handling keys would be worse than not focusing it -- a
    // keyboard user would tab into a control that does nothing.
    mh::ui::ViewportWidget v(MH_SHADER_DIR);
    const auto press = [&](int key, Qt::KeyboardModifiers mods = Qt::NoModifier) {
        QKeyEvent e(QEvent::KeyPress, key, mods);
        QApplication::sendEvent(&v, &e);
    };

    const auto start = v.camera();
    press(Qt::Key_Right);
    CHECK_THAT(d(v.camera().yawDegrees), WithinAbs(d(start.yawDegrees) + 3.0, 1e-4));
    press(Qt::Key_Left);
    CHECK_THAT(d(v.camera().yawDegrees), WithinAbs(d(start.yawDegrees), 1e-4));

    // Shift is the coarse step every DCC uses.
    press(Qt::Key_Right, Qt::ShiftModifier);
    CHECK_THAT(d(v.camera().yawDegrees), WithinAbs(d(start.yawDegrees) + 15.0, 1e-4));

    press(Qt::Key_Home);
    CHECK_THAT(d(v.camera().yawDegrees), WithinAbs(d(mh::render::Camera{}.yawDegrees), 1e-4));

    // Dolly, and the same clamps the mouse obeys.
    const float before = v.camera().distance;
    press(Qt::Key_Plus);
    CHECK(v.camera().distance < before);
    for (int i = 0; i < 200; ++i)
        press(Qt::Key_Plus);
    CHECK_THAT(d(v.camera().distance), WithinAbs(d(mh::ui::ViewportWidget::kMinDistance), 1e-4));
    for (int i = 0; i < 400; ++i)
        press(Qt::Key_Minus);
    CHECK_THAT(d(v.camera().distance), WithinAbs(d(mh::ui::ViewportWidget::kMaxDistance), 1e-4));

    // Pitch stops at the poles, as it does with the mouse.
    for (int i = 0; i < 200; ++i)
        press(Qt::Key_Down);
    CHECK_THAT(d(v.camera().pitchDegrees),
               WithinAbs(d(mh::ui::ViewportWidget::kMaxPitchDegrees), 1e-4));

    // A key the viewport does not use is left for the rest of the window.
    const auto held = v.camera();
    press(Qt::Key_A);
    CHECK_THAT(d(v.camera().yawDegrees), WithinAbs(d(held.yawDegrees), 1e-9));
}

TEST_CASE("captions can shrink at 200% text instead of pinning the dock open", "[a11y]") {
    // Measured, after a first attempt that asserted the wrong thing: at 200%
    // the longest shipped caption ("Scale depth of parietal side") wants 263 px
    // and the dock is 380, so nothing clips and the panel's own minimum is 153
    // either way. Word wrap is NOT fixing a clipping bug.
    //
    // What it does buy is real: it drops that label's MINIMUM from 263 px to
    // 72, so a user who drags the dock narrower still gets a usable panel
    // instead of one label holding it open. That is the property asserted here,
    // and it is the one that changes when setWordWrap goes.
    const QFont original = QApplication::font();
    QFont doubled        = original;
    doubled.setPointSizeF(original.pointSizeF() * 2.0);
    QApplication::setFont(doubled);

    std::vector<mh::foundation::TaskViewSpec> views;
    mh::foundation::TaskViewSpec view;
    view.name = "Face";
    mh::foundation::SliderSection section{"head shape", {}};
    // The longest real caption, plus a single word wrapping cannot help.
    for (const char* label : {"Scale depth of parietal side", "Invertedtriangular"}) {
        section.sliders.push_back({std::string("head/") + label, label, "", -1.0F, 1.0F, 0.0F});
    }
    view.sections = {section};
    views.push_back(view);

    mh::ui::ModifierPanel panel(views);
    // The app always applies the stylesheet, and QLabel's wrapped sizeHint
    // differs with it -- a bare panel measures a regime the program never runs
    // in.
    panel.setStyleSheet(mh::ui::theme::styleSheet());
    const int dockWidth = 380;  // what applyWorkspacePreset gives the focused dock
    panel.resize(dockWidth, 900);

    for (QLabel* caption : panel.findChildren<QLabel*>(QStringLiteral("modifiers.caption"))) {
        INFO(caption->text().toStdString());
        // Nothing clips at the dock's own width.
        CHECK(caption->sizeHint().width() <= dockWidth);
        if (caption->text().contains(QLatin1Char(' '))) {
            // A wrappable label must be able to give width back.
            CHECK(caption->minimumSizeHint().width() < caption->sizeHint().width());
        } else {
            // A single word cannot wrap, and pretending otherwise would be a
            // test that passes for the wrong reason.
            CHECK(caption->minimumSizeHint().width() == caption->sizeHint().width());
        }
    }
    // NOT panel.minimumSizeHint(): each tab page sits in a QScrollArea with
    // setWidgetResizable, whose minimum ignores its widget's, so that number is
    // a constant 151 in every configuration -- wrap on or off, 100% or 200% --
    // and cannot fail. The page inside the scroll area is what actually moves.
    auto* scroll = panel.findChild<QScrollArea*>();
    REQUIRE(scroll != nullptr);
    REQUIRE(scroll->widget() != nullptr);
    CHECK(scroll->widget()->minimumSizeHint().width() <= dockWidth);

    QApplication::setFont(original);
}

TEST_CASE("reduce motion is read from the system and drops the dock animation", "[a11y]") {
    useShippedIcons();
    mh::ui::MainWindow w(MH_SHADER_DIR, shippedTasks());
    // Both branches, because the system setting cannot be changed from a test
    // and asserting the window agrees with reduceMotion() is vacuous when both
    // sides call the same function -- on a machine with the setting off it
    // passes even if the window ignores it entirely.
    const auto moving  = mh::ui::MainWindow::dockOptionsFor(false);
    const auto reduced = mh::ui::MainWindow::dockOptionsFor(true);
    CHECK((moving & QMainWindow::AnimatedDocks) != 0);
    CHECK((reduced & QMainWindow::AnimatedDocks) == 0);

    // And the window really uses the helper rather than a copy of the logic.
    CHECK(w.dockOptions() == mh::ui::MainWindow::dockOptionsFor(mh::ui::theme::reduceMotion()));

    // The accessor must be callable without a running NSApplication -- these
    // tests run under the offscreen platform.
    (void)mh::ui::theme::reduceMotion();
}

// The ON branch, actually exercised.
//
// Everything above tests `dockOptionsFor` in isolation and then asserts the
// window agrees with `reduceMotion()` -- which on a machine with the setting
// OFF passes even if the ON path is broken, because nothing ever produces a
// true. `MH_REDUCE_MOTION` is what makes the true reachable without changing a
// real System Setting, on a build box or here.
TEST_CASE("a window built with reduce-motion on really stops animating", "[theme][a11y][motion]") {
    const QByteArray had = qgetenv("MH_REDUCE_MOTION");
    const bool wasSet    = qEnvironmentVariableIsSet("MH_REDUCE_MOTION");
    const auto restore   = [&] {
        if (wasSet) {
            qputenv("MH_REDUCE_MOTION", had);
        } else {
            qunsetenv("MH_REDUCE_MOTION");
        }
    };

    useShippedIcons();

    qputenv("MH_REDUCE_MOTION", "1");
    CHECK(mh::ui::theme::reduceMotion());
    {
        // Read once at construction, so the variable must be set BEFORE this.
        mh::ui::MainWindow on(MH_SHADER_DIR, shippedTasks());
        CHECK((on.dockOptions() & QMainWindow::AnimatedDocks) == 0);
        CHECK(on.dockOptions() == mh::ui::MainWindow::dockOptionsFor(true));
    }

    qputenv("MH_REDUCE_MOTION", "0");
    CHECK_FALSE(mh::ui::theme::reduceMotion());
    {
        mh::ui::MainWindow off(MH_SHADER_DIR, shippedTasks());
        CHECK((off.dockOptions() & QMainWindow::AnimatedDocks) != 0);
    }

    // Anything that is not an affirmative is off, rather than "any value means
    // on" -- MH_REDUCE_MOTION=0 must not enable it.
    for (const char* v : {"0", "no", "false", "off"}) {
        qputenv("MH_REDUCE_MOTION", v);
        INFO(v);
        CHECK_FALSE(mh::ui::theme::reduceMotion());
    }
    for (const char* v : {"1", "true", "yes", "T", "Y"}) {
        qputenv("MH_REDUCE_MOTION", v);
        INFO(v);
        CHECK(mh::ui::theme::reduceMotion());
    }

    // Unset falls back to asking macOS, which is the shipped default.
    qunsetenv("MH_REDUCE_MOTION");
    (void)mh::ui::theme::reduceMotion();
    restore();
}

TEST_CASE("a choice undoes and redoes, and choices never merge", "[undo]") {
    QStringList applied;
    const auto apply = [&](const QString& key, const QString& id) {
        applied << (key + QLatin1Char('=') + id);
    };

    QUndoStack stack;
    stack.push(new mh::ui::ChoiceChangeCommand(QStringLiteral("Skin"), QStringLiteral("caucasian"),
                                               QStringLiteral("african"), 0, apply));
    CHECK(applied == QStringList{QStringLiteral("Skin=african")});

    stack.undo();
    CHECK(applied.last() == QStringLiteral("Skin=caucasian"));
    stack.redo();
    CHECK(applied.last() == QStringLiteral("Skin=african"));

    // Same group, same run: merges. Arrow-keying a closed combo emits one
    // change per keystroke (measured: three Down presses, three changes), so
    // without this a traversal ending where it started costs three undo steps
    // and three full skeleton reloads.
    stack.push(new mh::ui::ChoiceChangeCommand(QStringLiteral("Skin"), QStringLiteral("african"),
                                               QStringLiteral("asian"), 0, apply));
    CHECK(stack.count() == 1);
    // One undo goes back to where the run began, not to the middle of it.
    stack.undo();
    CHECK(applied.last() == QStringLiteral("Skin=caucasian"));
    stack.redo();
    CHECK(applied.last() == QStringLiteral("Skin=asian"));

    // A different group never merges, even in the same run -- undoing a pose
    // must not move the skin.
    stack.push(new mh::ui::ChoiceChangeCommand(QStringLiteral("Pose"), QStringLiteral("rest"),
                                               QStringLiteral("tpose"), 0, apply));
    CHECK(stack.count() == 2);
    stack.undo();
    CHECK(applied.last() == QStringLiteral("Pose=rest"));

    // A new run does not merge with the previous one.
    stack.push(new mh::ui::ChoiceChangeCommand(QStringLiteral("Skin"), QStringLiteral("asian"),
                                               QStringLiteral("caucasian"), 1, apply));
    CHECK(stack.count() == 2);  // the redo tail was dropped, then one pushed
}

TEST_CASE("setChoice selects without emitting", "[assets]") {
    // Restored API: it was cut for having no production caller, and undo is
    // that caller -- restoring a choice must not report it as a fresh one.
    const auto groups = toyAssets();
    mh::ui::AssetPanel panel(groups);

    int emissions = 0;
    QObject::connect(&panel, &mh::ui::AssetPanel::chosen,
                     [&](const QString&, const QString&) { ++emissions; });

    panel.setChoice(QStringLiteral("Skin"), QStringLiteral("/l/african.png"));
    CHECK(panel.choice(QStringLiteral("Skin")) == QStringLiteral("/l/african.png"));
    CHECK(emissions == 0);

    // An unknown id leaves the selection alone rather than clearing it.
    panel.setChoice(QStringLiteral("Skin"), QStringLiteral("/l/nope.png"));
    CHECK(panel.choice(QStringLiteral("Skin")) == QStringLiteral("/l/african.png"));
    panel.setChoice(QStringLiteral("Nope"), QStringLiteral("x"));
    CHECK(emissions == 0);
}

// --- the task registry -------------------------------------------------------

TEST_CASE("categories keep registration order, not alphabetical", "[tasks]") {
    // The point of replacing the filename scheme. Sorted alphabetically these
    // three would come out Geometries, Materials, Modelling -- all three
    // positions differ, so an alphabetical implementation fails this.
    mh::ui::TaskRegistry tasks;
    REQUIRE(tasks.add(QStringLiteral("Modelling")));
    REQUIRE(tasks.add(QStringLiteral("Materials")));
    REQUIRE(tasks.add(QStringLiteral("Geometries")));

    CHECK(tasks.categories() == QStringList{QStringLiteral("Modelling"),
                                            QStringLiteral("Materials"),
                                            QStringLiteral("Geometries")});
}

TEST_CASE("a duplicate category is refused, ignoring case", "[tasks]") {
    mh::ui::TaskRegistry tasks;
    REQUIRE(tasks.add(QStringLiteral("Modelling")));
    CHECK_FALSE(tasks.add(QStringLiteral("Modelling")));
    // Case matters because the dock object name is the lower-cased category and
    // saveState keys on it -- "modelling" would silently share one dock.
    CHECK_FALSE(tasks.add(QStringLiteral("modelling")));
    CHECK_FALSE(tasks.add(QString{}));
    CHECK(tasks.categories().size() == 1);
}

TEST_CASE("the window builds one dock per registered category", "[tasks]") {
    useShippedIcons();
    mh::ui::TaskRegistry tasks;
    REQUIRE(tasks.add(QStringLiteral("Modelling")));
    REQUIRE(tasks.add(QStringLiteral("Materials")));
    REQUIRE(tasks.add(QStringLiteral("Geometries")));

    mh::ui::MainWindow w(MH_SHADER_DIR, tasks);
    for (const QString& category : tasks.categories()) {
        INFO(category.toStdString());
        auto* dock = w.findChild<QDockWidget*>(mh::ui::MainWindow::dockObjectName(category));
        REQUIRE(dock != nullptr);
        CHECK(dock->windowTitle() == category);
    }
    // The names the workspace presets restore by must not drift.
    CHECK(mh::ui::MainWindow::dockObjectName(QStringLiteral("Modelling")) ==
          QStringLiteral("dock.modelling"));

    // An empty registry gives a viewport and no panels.
    mh::ui::MainWindow bare(MH_SHADER_DIR, mh::ui::TaskRegistry{});
    CHECK(bare.findChildren<QDockWidget*>().isEmpty());
    CHECK(bare.viewport() != nullptr);
}

TEST_CASE("a mistyped category is refused without taking the widget", "[tasks]") {
    useShippedIcons();
    mh::ui::MainWindow w(MH_SHADER_DIR, shippedTasks());

    // "Modeling" with one L is the reference's own spelling, and setPanel takes
    // a free-form string. The previous version DELETED the widget here, so the
    // caller went on connecting signals to freed memory.
    auto* orphan = new QWidget;
    CHECK_FALSE(w.setPanel(QStringLiteral("Modeling"), orphan));
    // Still ours, still alive -- a leak is a far better failure than a
    // use-after-free, and the [[nodiscard]] makes the caller notice.
    CHECK(orphan->objectName().isEmpty());
    delete orphan;

    auto* real = new QWidget;
    CHECK(w.setPanel(QStringLiteral("Modelling"), real));
}

TEST_CASE("a category registered later is still reachable from a preset", "[tasks]") {
    useShippedIcons();
    // The regression this replaces: presets named dock objects literally, so a
    // third category was hidden by EVERY preset -- and once the layout was saved
    // on quit it never came back except through Reset Workspace. Measured across
    // all four presets before the fix.
    //
    // "Geometries" is deliberately not mentioned anywhere in workspacePresets().
    mh::ui::TaskRegistry tasks;
    REQUIRE(tasks.add(QStringLiteral("Modelling")));
    REQUIRE(tasks.add(QStringLiteral("Materials")));
    REQUIRE(tasks.add(QStringLiteral("Geometries")));

    mh::ui::MainWindow w(MH_SHADER_DIR, tasks);
    auto* newcomer =
        w.findChild<QDockWidget*>(mh::ui::MainWindow::dockObjectName(QStringLiteral("Geometries")));
    REQUIRE(newcomer != nullptr);

    // Hide it FIRST. On a window that was never shown every dock already
    // reports isHidden() == false, so checking visibility straight away would
    // pass even if applyWorkspacePreset did no work at all -- proven by
    // stubbing out its setVisible loop and watching this stay green.
    REQUIRE(w.applyWorkspacePreset(QStringLiteral("Materials")));
    CHECK(newcomer->isHidden());

    // The full layout brings it back without the preset table naming it.
    REQUIRE(w.applyWorkspacePreset(QStringLiteral("Modelling")));
    CHECK_FALSE(newcomer->isHidden());

    // And it comes back, which is what used to be impossible.
    REQUIRE(w.applyWorkspacePreset(QStringLiteral("Modelling")));
    CHECK_FALSE(newcomer->isHidden());
}

TEST_CASE("the full preset is the first one, and refuses to resolve to nothing", "[tasks]") {
    useShippedIcons();
    const auto& presets = mh::ui::workspacePresets();
    REQUIRE_FALSE(presets.empty());
    // Nothing means "every registered category". That is what makes a category
    // added later reachable by construction rather than by a test noticing.
    CHECK_FALSE(presets.front().categories.has_value());
    CHECK(presets.front().name == QStringLiteral("Modelling"));

    // A preset naming only categories nobody registered used to hide every
    // dock, return true, and print "Workspace: Materials" -- then the empty
    // layout was saved on quit, with no way back except Reset Workspace.
    mh::ui::TaskRegistry only;
    REQUIRE(only.add(QStringLiteral("Modelling")));
    mh::ui::MainWindow w(MH_SHADER_DIR, only);
    CHECK_FALSE(w.applyWorkspacePreset(QStringLiteral("Materials")));

    // The dock it does have is untouched by the refusal.
    auto* dock = w.findChild<QDockWidget*>(QStringLiteral("dock.modelling"));
    REQUIRE(dock != nullptr);
    CHECK_FALSE(dock->isHidden());

    // Export names nothing on purpose, so hiding everything IS the outcome.
    CHECK(w.applyWorkspacePreset(QStringLiteral("Export")));
    CHECK(dock->isHidden());
}

// Owner directive 4: the viewport shades with PBR as well as with the matcap.
// The widget outlives its SceneResources -- initialize() destroys and rebuilds
// it whenever the device or render pass changes -- so the choice has to be
// remembered by the WIDGET rather than only by the scene.
//
// WHAT THIS DOES NOT COVER, stated plainly because I nearly claimed otherwise:
// re-applying the model to a REBUILT SceneResources
// (`ViewportWidget.cpp`'s initialize()) needs a real device and a live render
// target, which these offscreen tests do not have. Deleting that line leaves
// this test green -- measured, not assumed. What is covered is the widget-side
// memory, which is the half that exists without a GPU.
TEST_CASE("the viewport remembers its shading model", "[viewport][pbr]") {
    mh::ui::ViewportWidget v(MH_SHADER_DIR);
    CHECK(v.shadingModel() == mh::render::ShadingModel::Litsphere);  // the parity default

    v.setShadingModel(mh::render::ShadingModel::Pbr);
    CHECK(v.shadingModel() == mh::render::ShadingModel::Pbr);

    v.resize(320, 240);
    CHECK(v.shadingModel() == mh::render::ShadingModel::Pbr);

    v.setShadingModel(mh::render::ShadingModel::Litsphere);
    CHECK(v.shadingModel() == mh::render::ShadingModel::Litsphere);
}

// ---------------------------------------------------------------------------
// Owner directive 8: "ensure you have all icons since we are using lucide
// icons". `theme::icon()` returns a NULL QIcon for a name it cannot find --
// deliberately, so a missing file shows nothing rather than a black square.
// The cost of that choice is that a typo, or an action added with an icon name
// nobody vendored, ships a blank button and no error anywhere.
//
// This walks the real window and refuses both. It is the audit the directive
// asks for, run on every build rather than done once by hand.
// ---------------------------------------------------------------------------

TEST_CASE("every action in the window has a real icon", "[ui][icons]") {
    theme::setIconDir(std::filesystem::path(MH_RESOURCE_DIR) / "icons" / "lucide");
    mh::ui::MainWindow w(MH_SHADER_DIR, mh::ui::TaskRegistry{});

    // Separators carry no icon by design, and neither do the workspace presets,
    // which are named layouts rather than commands. Everything a user can click
    // that ISN'T one of those is a toolbar or menu command and needs a glyph.
    QStringList blank;
    for (const QAction* a : w.findChildren<QAction*>()) {
        if (a->isSeparator()) continue;
        const QString name = a->objectName();
        if (name.isEmpty()) continue;  // Qt-internal actions we did not name
        // Only the named LAYOUTS are exempt -- a workspace preset is a saved
        // arrangement, not a command, and the reference gives those no glyph.
        // `workspace.reset` and `workspace.saveAs` are commands and are NOT
        // exempt: excluding the whole `workspace.` prefix is what let Reset
        // Workspace ship as bare text in the toolbar while this test was green.
        if (name.startsWith(QLatin1String("workspace.saved"))) continue;
        if (name.startsWith(QLatin1String("workspace.")) &&
            name != QLatin1String("workspace.reset") && name != QLatin1String("workspace.saveAs")) {
            continue;
        }
        if (a->icon().isNull()) blank << name;
    }

    blank.sort();
    INFO("actions with no icon: " << blank.join(QStringLiteral(", ")).toStdString());
    CHECK(blank.empty());
}

// The other half of the same audit, from the opposite direction: an icon that
// renders as an empty pixmap is just as blank as a missing file, and
// `QIcon::isNull()` does not catch it -- a QIcon built from an SVG that failed
// to parse is non-null and paints nothing.
TEST_CASE("a vendored icon actually renders pixels", "[ui][icons]") {
    theme::setIconDir(std::filesystem::path(MH_RESOURCE_DIR) / "icons" / "lucide");

    // Every vendored SVG, not a sample: one that fails to parse yields a
    // non-null QIcon that paints nothing, so `isNull()` cannot see it and the
    // action audit above would call it fine.
    const std::filesystem::path dir = std::filesystem::path(MH_RESOURCE_DIR) / "icons" / "lucide";
    QStringList empty;
    size_t checked = 0;
    for (const auto& entry : std::filesystem::directory_iterator(dir)) {
        if (entry.path().extension() != ".svg") continue;
        const std::string stem = entry.path().stem().string();
        ++checked;
        const QIcon i = theme::icon(stem, QColor(255, 255, 255), 32);
        size_t opaque = 0;
        if (!i.isNull()) {
            const QImage img = i.pixmap(32, 32).toImage();
            for (int y = 0; y < img.height(); ++y)
                for (int x = 0; x < img.width(); ++x)
                    if (qAlpha(img.pixel(x, y)) > 8) ++opaque;
        }
        // 32 of 1024 pixels is a low bar on purpose -- `minus` is a single
        // stroke. It separates "drew something" from "drew nothing".
        if (opaque <= 32) empty << QString::fromStdString(stem);
    }
    INFO("vendored icons checked: " << checked);
    INFO("icons that rendered blank: " << empty.join(QStringLiteral(", ")).toStdString());
    CHECK(checked >= 56);  // the vendored set as of directive 8
    CHECK(empty.empty());

    // A name nobody vendored must stay null rather than throw or fall back to
    // some other glyph -- the audit above depends on exactly that.
    CHECK(theme::icon("no-such-icon-anywhere", QColor(255, 255, 255), 32).isNull());
}

// ---------------------------------------------------------------------------
// Owner directive 8, first structural piece: the reference has a top toolbar
// and we had none. What it does NOT have yet is the mesh-display, symmetry and
// body-part-camera groups from the screenshot -- `src/ui/` has no wireframe,
// smooth, subdivide or symmetry anything, so those buttons would be painted
// no-ops. A button that looks live and does nothing is worse than an absent
// one, so they wait for the behaviour they need. Recorded in todo.md.
// ---------------------------------------------------------------------------

TEST_CASE("the window has a top toolbar", "[ui][toolbar]") {
    theme::setIconDir(std::filesystem::path(MH_RESOURCE_DIR) / "icons" / "lucide");
    mh::ui::MainWindow w(MH_SHADER_DIR, mh::ui::TaskRegistry{});

    QToolBar* bar = w.findChild<QToolBar*>(QStringLiteral("toolbar.main"));
    REQUIRE(bar != nullptr);
    CHECK(w.toolBarArea(bar) == Qt::TopToolBarArea);

    QStringList names;
    for (const QAction* a : bar->actions()) {
        if (!a->isSeparator()) names << a->objectName();
    }
    INFO("toolbar: " << names.join(QStringLiteral(", ")).toStdString());
    CHECK(names.contains(QStringLiteral("file.open")));
    CHECK(names.contains(QStringLiteral("file.save")));
    CHECK(names.contains(QStringLiteral("edit.undo")));
    CHECK(names.contains(QStringLiteral("edit.redo")));
    CHECK(names.contains(QStringLiteral("view.screenshot")));
}

// The toolbar must SHARE its actions with the menus rather than construct
// parallel ones. Two QActions for one command is how a toolbar button drifts
// out of step with its menu item: it keeps its own enabled state, its own
// shortcut and its own translation registration, and undo/redo in particular
// would stop greying out with the stack.
TEST_CASE("toolbar and menu are the same actions, not copies", "[ui][toolbar]") {
    theme::setIconDir(std::filesystem::path(MH_RESOURCE_DIR) / "icons" / "lucide");
    mh::ui::MainWindow w(MH_SHADER_DIR, mh::ui::TaskRegistry{});

    QToolBar* bar = w.findChild<QToolBar*>(QStringLiteral("toolbar.main"));
    REQUIRE(bar != nullptr);

    // Every named action in the window, by objectName. A duplicate name means
    // two objects for one command, which is exactly what this forbids.
    for (const QString& shared : {QStringLiteral("file.open"), QStringLiteral("file.save"),
                                  QStringLiteral("edit.undo"), QStringLiteral("edit.redo")}) {
        CAPTURE(shared.toStdString());
        const auto all = w.findChildren<QAction*>(shared);
        CHECK(all.size() == 1);
    }

    // Undo starts disabled because the stack is empty. If the toolbar held its
    // own copy this would be true of the menu action and false of the button.
    const auto undo = w.findChildren<QAction*>(QStringLiteral("edit.undo"));
    REQUIRE(undo.size() == 1);
    CHECK_FALSE(undo.front()->isEnabled());
    CHECK(bar->actions().contains(undo.front()));
}

// A lucide glyph is drawn in a centred 24x24 viewBox, so a correctly rendered
// icon has its ink centred in the pixmap. This catches the class of bug that
// "did it draw any pixels?" cannot: an icon rasterised at the wrong scale is
// CLIPPED rather than blank, and a clipped icon is still mostly opaque.
//
// It bites only at devicePixelRatio > 1, which is why the ctest suite runs the
// icon tests a second time under QT_SCALE_FACTOR=2 -- at DPR 1 the wrong and
// the right code produce identical output, and every test here was green while
// every icon in the shipped window was a corner fragment.
TEST_CASE("a rendered icon is centred, not clipped to a corner", "[ui][icons]") {
    theme::setIconDir(std::filesystem::path(MH_RESOURCE_DIR) / "icons" / "lucide");

    // `plus` is a symmetric cross: its centre of mass is the middle of the box
    // under any correct rasterisation, and lands up and left of it when the
    // glyph is drawn too large and cropped.
    for (const char* name : {"plus", "x", "circle-help"}) {
        CAPTURE(name);
        const QIcon i = theme::icon(name, QColor(255, 255, 255), 64);
        REQUIRE_FALSE(i.isNull());
        const QImage img = i.pixmap(64, 64).toImage();
        REQUIRE(img.width() > 0);

        double sx = 0.0;
        double sy = 0.0;
        double n  = 0.0;
        for (int y = 0; y < img.height(); ++y) {
            for (int x = 0; x < img.width(); ++x) {
                const double a = qAlpha(img.pixel(x, y)) / 255.0;
                sx += a * x;
                sy += a * y;
                n += a;
            }
        }
        REQUIRE(n > 0.0);
        const double cx = sx / n / img.width();
        const double cy = sy / n / img.height();
        INFO("centre of mass: " << cx << ", " << cy);
        CHECK(cx > 0.38);
        CHECK(cx < 0.62);
        CHECK(cy > 0.38);
        CHECK(cy < 0.62);
    }
}

// ---------------------------------------------------------------------------
// Owner directive 8: the reference's persistent status line
//   "Gender: male  Age: 25  Muscle: 72.10 %  Weight: 86.10 %  Height: 174.39 cm"
// Every number was already computable; the line simply did not exist.
//
// Two of its rules are NOT what a reasonable guess produces, which is why they
// are pinned here rather than eyeballed:
//   * weight displays 50 + 100*w, so a default character reads 100 %, not 50 %
//     (legacy/python/apps/gui/guimodifier.py:168);
//   * the gender endpoints are compared EXACTLY, so 0.999 is a split and not
//     "male" (guimodifier.py:155-163).
// ---------------------------------------------------------------------------

TEST_CASE("the gender label has all four of the reference's branches", "[ui][status]") {
    // Exact endpoints. A tolerance here would swallow the two cases below them.
    CHECK(mh::ui::genderLabel(0.0F) == QStringLiteral("female"));
    CHECK(mh::ui::genderLabel(1.0F) == QStringLiteral("male"));

    // Within 0.01 of centre, inclusive of the default.
    CHECK(mh::ui::genderLabel(0.5F) == QStringLiteral("neutral"));
    CHECK(mh::ui::genderLabel(0.505F) == QStringLiteral("neutral"));

    // Just outside the neutral band, and just inside the endpoints. Both are
    // splits: 0.999 is NOT "male", which is the trap.
    CHECK(mh::ui::genderLabel(0.52F) == QStringLiteral("48.00 % female, 52.00 % male"));
    CHECK(mh::ui::genderLabel(0.999F) == QStringLiteral("0.10 % female, 99.90 % male"));
}

TEST_CASE("the status line matches the reference's format", "[ui][status]") {
    mh::ui::MacroStats s;
    s.gender   = 1.0F;
    s.ageYears = 25.0F;
    s.muscle   = 0.7210F;
    s.weight   = 0.3610F;  // 50 + 36.10 = 86.10 %
    s.heightCm = 174.39F;

    CHECK(mh::ui::macroStatusLine(s) == QStringLiteral("Gender: male  Age: 25  Muscle: 72.10 %  "
                                                       "Weight: 86.10 %  Height: 174.39 cm"));

    // The default character. 100 %, not 50 % -- the whole reason the offset is
    // pinned rather than assumed.
    const mh::ui::MacroStats d;
    INFO(mh::ui::macroStatusLine(d).toStdString());
    CHECK(mh::ui::macroStatusLine(d).contains(QStringLiteral("Weight: 100.00 %")));
    CHECK(mh::ui::macroStatusLine(d).contains(QStringLiteral("Gender: neutral")));

    // Age truncates toward zero rather than rounding: the reference formats it
    // with %d.
    mh::ui::MacroStats old = d;
    old.ageYears           = 25.9F;
    CHECK(mh::ui::macroStatusLine(old).contains(QStringLiteral("Age: 25")));
}

// The formatter is tested above; this is the WIRING. Building the label and
// forgetting `addPermanentWidget` leaves an orphan that holds the right text
// and is never shown, which no formatter test can see.
TEST_CASE("the stats line is a permanent widget in the status bar", "[ui][status]") {
    theme::setIconDir(std::filesystem::path(MH_RESOURCE_DIR) / "icons" / "lucide");
    mh::ui::MainWindow w(MH_SHADER_DIR, mh::ui::TaskRegistry{});

    w.setMacroStatus(QStringLiteral("Gender: neutral  Age: 25"));
    CHECK(w.macroStatus() == QStringLiteral("Gender: neutral  Age: 25"));

    // Parented to the status bar, so it is actually on screen.
    QLabel* label = w.findChild<QLabel*>(QStringLiteral("status.macro"));
    REQUIRE(label != nullptr);
    CHECK(label->text() == QStringLiteral("Gender: neutral  Age: 25"));
    CHECK(w.statusBar()->isAncestorOf(label));

    // A transient message must NOT replace it -- that is the whole reason it is
    // a permanent widget and not showMessage.
    w.statusBar()->showMessage(QStringLiteral("Saved workspace"), 1000);
    CHECK(w.macroStatus() == QStringLiteral("Gender: neutral  Age: 25"));
}

// The writers have existed since M7 -- io/ObjWriter.h, io/GltfWriter.h,
// io/UsdWriter.h and the assimp path -- and until now NOTHING in the window
// reached them. Export was reachable only from the command line, and
// `memory/taskviews.md` had it filed as "covered" on the strength of the File
// menu, which was simply wrong: the menu has Open, Save and Save As and had no
// Export at all. That is why the audit below checks the action exists rather
// than trusting the roadmap.
TEST_CASE("the File menu can export", "[ui][export]") {
    theme::setIconDir(std::filesystem::path(MH_RESOURCE_DIR) / "icons" / "lucide");
    mh::ui::MainWindow w(MH_SHADER_DIR, mh::ui::TaskRegistry{});

    const auto found = w.findChildren<QAction*>(QStringLiteral("file.export"));
    REQUIRE(found.size() == 1);
    QAction* act = found.front();
    CHECK_FALSE(act->icon().isNull());

    // In the File menu, not merely parented to the window. An action nobody
    // added to a menu or a toolbar is unreachable however well it is wired.
    bool inFileMenu = false;
    for (const QMenu* m : w.menuBar()->findChildren<QMenu*>()) {
        if (m->actions().contains(act)) inFileMenu = true;
    }
    CHECK(inFileMenu);

    // It asks rather than acts: this module must not decide what a .glb is.
    int emitted = 0;
    QObject::connect(&w, &mh::ui::MainWindow::exportRequested, [&emitted] { ++emitted; });
    act->trigger();
    CHECK(emitted == 1);
}
