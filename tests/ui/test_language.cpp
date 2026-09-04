// SPDX-License-Identifier: Apache-2.0
//
// Twenty language files ship under `data/languages/`, one of them RTL, and
// until now nothing in `src/` read any of them: there was no QTranslator, no
// layout direction and no way to switch.
#include "makehuman/ui/Language.h"
#include "makehuman/ui/MainWindow.h"
#include "makehuman/ui/TaskRegistry.h"

#include <catch2/catch_test_macros.hpp>

#include <QAction>
#include <QApplication>
#include <QCoreApplication>
#include <QDockWidget>
#include <QFile>
#include <QMenu>
#include <QMenuBar>
#include <QTemporaryDir>

#include <filesystem>

using namespace mh;

TEST_CASE("the shipped languages are found, and master is not offered", "[ui][i18n]") {
    const auto langs = ui::availableLanguages(MH_DATA_DIR);
    // 20 files ship; `master` is the English source list the rest are generated
    // against, so offering it would present the source strings as a
    // translation of themselves.
    CHECK(langs.size() == 19);
    CHECK(langs.contains(QStringLiteral("arabic_generic")));
    CHECK(langs.contains(QStringLiteral("german_generic")));
    CHECK_FALSE(langs.contains(QStringLiteral("master")));
    // Sorted, so a menu built from this does not reshuffle between machines --
    // QDir::entryInfoList order is not guaranteed.
    QStringList sorted = langs;
    sorted.sort();
    CHECK(langs == sorted);
}

TEST_CASE("a language file translates by source string", "[ui][i18n]") {
    ui::JsonTranslator t;
    REQUIRE(t.load(MH_DATA_DIR, QStringLiteral("german_generic")));
    CHECK_FALSE(t.isEmpty());
    CHECK(t.languageName() == QStringLiteral("german_generic"));

    // Context is ignored: the shipped files are one flat map keyed by source
    // alone, so honouring Qt's (class, source) key would make every entry miss.
    const QString a = t.translate("MainWindow", "Browse");
    const QString b = t.translate("SomeOtherClass", "Browse");
    CHECK_FALSE(a.isEmpty());
    CHECK(a == b);
    CHECK(a != QStringLiteral("Browse"));
}

TEST_CASE("a string with no entry falls through to the source", "[ui][i18n]") {
    ui::JsonTranslator t;
    REQUIRE(t.load(MH_DATA_DIR, QStringLiteral("german_generic")));
    // A null return is Qt's "no translation" signal, which is what makes tr()
    // use the source. Returning the source here instead would work in the UI
    // and hide every missing entry from anything that checks.
    CHECK(t.translate("", "a string no language file contains").isNull());
    CHECK(t.translate("", nullptr).isEmpty());
}

TEST_CASE("rtl comes from the file, and only Arabic has it", "[ui][i18n][rtl]") {
    ui::JsonTranslator arabic;
    REQUIRE(arabic.load(MH_DATA_DIR, QStringLiteral("arabic_generic")));
    CHECK(arabic.isRightToLeft());

    ui::JsonTranslator german;
    REQUIRE(german.load(MH_DATA_DIR, QStringLiteral("german_generic")));
    CHECK_FALSE(german.isRightToLeft());

    // `__options__` is metadata, not a translatable string. Leaking it into the
    // map would put a JSON object where a label belongs.
    CHECK(arabic.translate("", "__options__").isNull());
}

TEST_CASE("a missing or malformed file leaves the translator empty", "[ui][i18n]") {
    ui::JsonTranslator t;
    CHECK_FALSE(t.load(MH_DATA_DIR, QStringLiteral("no_such_language")));
    CHECK(t.isEmpty());

    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    const std::filesystem::path root(dir.path().toStdString());
    std::filesystem::create_directories(root / "languages");

    // Valid JSON, wrong shape: an array where an object belongs. Half-loading
    // this would install a translator that silently translates nothing.
    QFile bad(QString::fromStdString((root / "languages" / "broken.json").string()));
    REQUIRE(bad.open(QIODevice::WriteOnly));
    bad.write("[1, 2, 3]");
    bad.close();
    CHECK_FALSE(t.load(root, QStringLiteral("broken")));
    CHECK(t.isEmpty());

    // And a loaded language followed by a failed load must not leave the old
    // strings behind, or a failed switch keeps translating into the previous
    // language.
    REQUIRE(t.load(MH_DATA_DIR, QStringLiteral("german_generic")));
    REQUIRE_FALSE(t.isEmpty());
    CHECK_FALSE(t.load(MH_DATA_DIR, QStringLiteral("no_such_language")));
    CHECK(t.isEmpty());
    CHECK(t.languageName().isEmpty());
    CHECK_FALSE(t.isRightToLeft());
}

TEST_CASE("installing the translator makes tr() and data strings resolve", "[ui][i18n]") {
    auto t = std::make_unique<ui::JsonTranslator>();
    REQUIRE(t->load(MH_DATA_DIR, QStringLiteral("german_generic")));
    const QString expected = t->translate("", "Browse");
    REQUIRE_FALSE(expected.isEmpty());

    ui::JsonTranslator* raw = t.get();
    REQUIRE(QCoreApplication::installTranslator(raw));
    // A DATA string -- a slider caption never passes through tr() -- reaches
    // the same flat map this way, which is the whole reason context is ignored.
    CHECK(QCoreApplication::translate("", "Browse") == expected);
    REQUIRE(QCoreApplication::removeTranslator(raw));
    CHECK(QCoreApplication::translate("", "Browse") == QStringLiteral("Browse"));
}

// --- Live switching, end to end ---------------------------------------------
//
// The point of routing through QTranslator rather than a lookup helper: Qt
// posts `QEvent::LanguageChange` and the window re-applies every registered
// label. A test that only checked `setLanguage` returned true would pass on a
// window that never repainted a single menu.
TEST_CASE("switching language changes the menus that are already built", "[ui][i18n][live]") {
    mh::ui::MainWindow w(MH_SHADER_DIR, mh::ui::TaskRegistry{});

    // A DOCK title, not a menu: the shipped dictionaries hold the reference's
    // vocabulary, and only 5 of our 38 tr() strings appear in the German file
    // (`Close`, `Redo`, `Reset`, `Save`, `Undo`) while the task-view names it
    // was written for -- Materials, Modelling, Skeleton -- all do.
    mh::ui::TaskRegistry tasks;
    REQUIRE(tasks.add(QStringLiteral("Modelling")));
    mh::ui::MainWindow w2(MH_SHADER_DIR, std::move(tasks));

    QDockWidget* dock = w2.findChild<QDockWidget*>(QStringLiteral("dock.modelling"));
    REQUIRE(dock != nullptr);
    const QString english = dock->windowTitle();
    CHECK(english == QStringLiteral("Modelling"));

    REQUIRE(w2.setLanguage(MH_DATA_DIR, QStringLiteral("german_generic")));
    CHECK(w2.language() == QStringLiteral("german_generic"));

    // The dock was built before the language existed, so this only passes if it
    // was re-applied rather than translated once at construction.
    INFO("was " << english.toStdString() << ", now " << dock->windowTitle().toStdString());
    CHECK(dock->windowTitle() == QStringLiteral("Modellieren"));

    // ...and back to the source strings.
    REQUIRE(w2.setLanguage(MH_DATA_DIR, QString{}));
    CHECK(dock->windowTitle() == english);
    CHECK(w2.language().isEmpty());
}

TEST_CASE("Arabic flips the layout direction, and leaving it flips back", "[ui][i18n][rtl]") {
    const Qt::LayoutDirection before = QApplication::layoutDirection();
    mh::ui::MainWindow w(MH_SHADER_DIR, mh::ui::TaskRegistry{});

    REQUIRE(w.setLanguage(MH_DATA_DIR, QStringLiteral("arabic_generic")));
    CHECK(QApplication::layoutDirection() == Qt::RightToLeft);

    // A left-to-right language must put it back. Leaving the application in RTL
    // after switching away is the failure that makes "working RTL" the item
    // rather than just "RTL".
    REQUIRE(w.setLanguage(MH_DATA_DIR, QStringLiteral("german_generic")));
    CHECK(QApplication::layoutDirection() == Qt::LeftToRight);

    REQUIRE(w.setLanguage(MH_DATA_DIR, QString{}));
    CHECK(QApplication::layoutDirection() == Qt::LeftToRight);
    QApplication::setLayoutDirection(before);
}

TEST_CASE("a failed switch keeps the language it had", "[ui][i18n]") {
    mh::ui::MainWindow w(MH_SHADER_DIR, mh::ui::TaskRegistry{});
    REQUIRE(w.setLanguage(MH_DATA_DIR, QStringLiteral("german_generic")));

    QMenu* file = nullptr;
    for (QMenu* m : w.menuBar()->findChildren<QMenu*>()) {
        if (m->objectName().isEmpty() && !m->title().isEmpty() && file == nullptr) file = m;
    }
    REQUIRE(file != nullptr);
    const QString german = file->title();

    // Loading the new file BEFORE removing the old one is what makes this
    // survivable: a failed switch must not drop the user into raw English.
    CHECK_FALSE(w.setLanguage(MH_DATA_DIR, QStringLiteral("no_such_language")));
    CHECK(w.language() == QStringLiteral("german_generic"));
    CHECK(file->title() == german);
}

TEST_CASE("the language menu is hidden until it has choices", "[ui][i18n]") {
    mh::ui::MainWindow w(MH_SHADER_DIR, mh::ui::TaskRegistry{});

    QMenu* lang = nullptr;
    for (QMenu* m : w.menuBar()->findChildren<QMenu*>()) {
        if (m->title() == QStringLiteral("&Language")) lang = m;
    }
    REQUIRE(lang != nullptr);
    // `mh_ui` has no data path of its own, so an empty menu is the honest state
    // rather than one listing nothing.
    CHECK_FALSE(lang->menuAction()->isVisible());

    w.setLanguageChoices(MH_DATA_DIR, mh::ui::availableLanguages(MH_DATA_DIR));
    CHECK(lang->menuAction()->isVisible());
    // 19 languages plus English, which is the source rather than a file.
    CHECK(lang->actions().size() == 20);
    CHECK(lang->actions().front()->text() == QStringLiteral("English"));

    const auto* arabic = w.findChild<QAction*>(QStringLiteral("language.arabic_generic"));
    REQUIRE(arabic != nullptr);
    CHECK(arabic->isCheckable());
}
