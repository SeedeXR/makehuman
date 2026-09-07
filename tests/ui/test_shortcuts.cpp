// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Shortcut persistence, and why it is SYMBOLIC.
//
// The reference stores raw Qt enum ints -- `f.write('%d %d %s\n' % (
// shortcut[0], shortcut[1], action))` at `legacy/python/core/mhmain.py:1022`.
// Two consequences it then has to live with:
//
//   * the file is unreadable and unwritable by a human: `67108864 90 undo`
//   * the numbers are only meaningful for the Qt that wrote them, so it needs a
//     magic sentinel -- `'_versionSentinel': (0, 0x87654321)` at `:192` -- and
//     throws the whole file away when it does not match (`:988-989`).
//
// We store `"Ctrl+Z"`. It is readable, hand-editable, and portable across Qt
// versions, so no sentinel is needed and nothing is silently discarded. This is
// a deliberate divergence under CLAUDE.md hard rule 3.
#include "makehuman/ui/MainWindow.h"
#include "makehuman/ui/Shortcuts.h"
#include "makehuman/ui/TaskRegistry.h"

#include <catch2/catch_test_macros.hpp>

#include <QAction>
#include <QFile>
#include <QKeySequence>
#include <QMainWindow>
#include <QSettings>
#include <QStringList>
#include <QTemporaryDir>

#include <filesystem>

using namespace mh::ui;

namespace {

/// A window with two named actions, which is all the module addresses.
struct Fixture {
    QMainWindow window;
    QAction* undo{};
    QAction* randomise{};

    Fixture() {
        undo = new QAction(&window);
        undo->setObjectName(QStringLiteral("edit.undo"));
        undo->setShortcut(QKeySequence(QStringLiteral("Ctrl+Z")));
        window.addAction(undo);

        randomise = new QAction(&window);
        randomise->setObjectName(QStringLiteral("edit.randomise"));
        randomise->setShortcut(QKeySequence(QStringLiteral("Ctrl+R")));
        window.addAction(randomise);
    }
};

QString iniIn(const QTemporaryDir& dir) {
    return dir.filePath(QStringLiteral("s.ini"));
}

}  // namespace

TEST_CASE("an override is applied by action name", "[ui][shortcuts]") {
    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    {
        QSettings s(iniIn(dir), QSettings::IniFormat);
        s.setValue(QStringLiteral("shortcuts/edit.randomise"), QStringLiteral("Ctrl+Shift+K"));
    }

    Fixture f;
    QSettings s(iniIn(dir), QSettings::IniFormat);
    const QStringList problems = shortcuts::apply(f.window, s);
    CHECK(problems.isEmpty());
    CHECK(f.randomise->shortcut() == QKeySequence(QStringLiteral("Ctrl+Shift+K")));
    // Untouched actions keep the shipped default.
    CHECK(f.undo->shortcut() == QKeySequence(QStringLiteral("Ctrl+Z")));
}

TEST_CASE("the stored form is text a human can edit", "[ui][shortcuts]") {
    // The whole point. A test that only round-trips through QSettings would
    // pass just as well on `s.setValue(name, int(seq[0]))`, which is the
    // reference's format and the thing this replaces.
    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    Fixture f;
    {
        QSettings s(iniIn(dir), QSettings::IniFormat);
        // apply() FIRST, so Ctrl+R is what gets recorded as the shipped
        // default. Rebinding before it -- which this test did at first -- makes
        // the new sequence the default, and save() then correctly writes
        // nothing at all.
        (void)shortcuts::apply(f.window, s);
        f.randomise->setShortcut(QKeySequence(QStringLiteral("Ctrl+Alt+J")));
        shortcuts::save(f.window, s);
    }

    QFile file(iniIn(dir));
    REQUIRE(file.open(QIODevice::ReadOnly | QIODevice::Text));
    const QString text = QString::fromUtf8(file.readAll());
    INFO(text.toStdString());
    CHECK(text.contains(QStringLiteral("edit.randomise")));
    CHECK(text.contains(QStringLiteral("Ctrl+Alt+J")));
}

TEST_CASE("only real overrides are written, so defaults keep tracking the app", "[ui][shortcuts]") {
    // Writing every action's current sequence is what forces the reference to
    // carry a version sentinel: once the defaults are on disk, changing one in
    // a new release is silently overridden by the user's own file. Storing only
    // what the user actually changed removes that whole class of problem.
    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    Fixture f;
    QSettings s(iniIn(dir), QSettings::IniFormat);
    (void)shortcuts::apply(f.window, s);
    shortcuts::save(f.window, s);
    s.sync();

    s.beginGroup(QString::fromLatin1(shortcuts::kGroup));
    CHECK(s.childKeys().isEmpty());
    s.endGroup();

    // Change one, and exactly one appears.
    f.undo->setShortcut(QKeySequence(QStringLiteral("Ctrl+Shift+Z")));
    shortcuts::save(f.window, s);
    s.sync();
    s.beginGroup(QString::fromLatin1(shortcuts::kGroup));
    CHECK(s.childKeys() == QStringList{QStringLiteral("edit.undo")});
    s.endGroup();
}

TEST_CASE("a raw Qt enum int is refused, not applied", "[ui][shortcuts]") {
    // Exactly what a shortcuts.ini migrated from the reference contains.
    //
    // **The value matters.** 90 is Qt::Key_Z, and `QKeySequence("90")` is an
    // unknown key, so the unknown-key guard would reject it even with no type
    // check at all -- measured, by deleting the type check and watching this
    // test still pass. 5 is the case with teeth: `QKeySequence("5")` is a
    // perfectly valid sequence, so ONLY refusing integers outright stops a
    // migrated file rebinding undo to the "5" key.
    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    {
        QSettings s(iniIn(dir), QSettings::IniFormat);
        s.setValue(QStringLiteral("shortcuts/edit.undo"), 5);
        s.setValue(QStringLiteral("shortcuts/edit.randomise"), 90);
    }
    Fixture f;
    QSettings s(iniIn(dir), QSettings::IniFormat);
    const QStringList problems = shortcuts::apply(f.window, s);
    REQUIRE(problems.size() == 2);
    const QString all = problems.join(QStringLiteral(" | "));
    INFO(all.toStdString());
    CHECK(all.contains(QStringLiteral("raw Qt key code")));
    CHECK(all.contains(QStringLiteral("edit.undo")));
    CHECK(f.undo->shortcut() == QKeySequence(QStringLiteral("Ctrl+Z")));
    CHECK(f.randomise->shortcut() == QKeySequence(QStringLiteral("Ctrl+R")));
}

TEST_CASE("unparseable and unknown entries are reported, never silent", "[ui][shortcuts]") {
    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    {
        QSettings s(iniIn(dir), QSettings::IniFormat);
        s.setValue(QStringLiteral("shortcuts/edit.undo"), QStringLiteral("NotAKey+++"));
        s.setValue(QStringLiteral("shortcuts/no.such.action"), QStringLiteral("Ctrl+B"));
    }
    Fixture f;
    QSettings s(iniIn(dir), QSettings::IniFormat);
    const QStringList problems = shortcuts::apply(f.window, s);
    CHECK(problems.size() == 2);
    const QString all = problems.join(QStringLiteral(" | "));
    INFO(all.toStdString());
    CHECK(all.contains(QStringLiteral("no.such.action")));
    CHECK(all.contains(QStringLiteral("edit.undo")));
    // The default survives an unparseable override.
    CHECK(f.undo->shortcut() == QKeySequence(QStringLiteral("Ctrl+Z")));
}

TEST_CASE("two actions cannot share one sequence", "[ui][shortcuts]") {
    // The reference refuses this too (`mhmain.py:1387`), and it is worth
    // keeping: Qt gives an ambiguous shortcut to NEITHER action, so the user
    // loses both commands and nothing says why.
    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    {
        QSettings s(iniIn(dir), QSettings::IniFormat);
        s.setValue(QStringLiteral("shortcuts/edit.randomise"), QStringLiteral("Ctrl+Z"));
    }
    Fixture f;
    QSettings s(iniIn(dir), QSettings::IniFormat);
    const QStringList problems = shortcuts::apply(f.window, s);
    REQUIRE(problems.size() == 1);
    INFO(problems.at(0).toStdString());
    CHECK(problems.at(0).contains(QStringLiteral("edit.undo")));
    CHECK(problems.at(0).contains(QStringLiteral("edit.randomise")));
    // Both keep what they had: the collision is refused, not half-applied.
    CHECK(f.randomise->shortcut() == QKeySequence(QStringLiteral("Ctrl+R")));
    CHECK(f.undo->shortcut() == QKeySequence(QStringLiteral("Ctrl+Z")));
}

TEST_CASE("an empty value clears a shortcut deliberately", "[ui][shortcuts]") {
    // Distinct from a malformed one: "I want no shortcut for this" is a real
    // wish, and the reference cannot express it -- (0, 0) is a valid key pair.
    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    {
        QSettings s(iniIn(dir), QSettings::IniFormat);
        s.setValue(QStringLiteral("shortcuts/edit.randomise"), QString());
    }
    Fixture f;
    QSettings s(iniIn(dir), QSettings::IniFormat);
    CHECK(shortcuts::apply(f.window, s).isEmpty());
    CHECK(f.randomise->shortcut().isEmpty());
}

TEST_CASE("applying twice does not turn an override into the default", "[ui][shortcuts]") {
    // Reachable: the window applies at start-up, and a settings dialog that
    // re-applies afterwards is the obvious next caller. If the second pass
    // re-records what it finds, the user's own override becomes the "shipped
    // default" and `reset` can never get back to Ctrl+R.
    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    QSettings s(dir.filePath(QStringLiteral("twice.ini")), QSettings::IniFormat);
    s.setValue(QStringLiteral("shortcuts/edit.randomise"), QStringLiteral("Ctrl+Shift+K"));

    Fixture f;
    CHECK(shortcuts::apply(f.window, s).isEmpty());
    CHECK(shortcuts::apply(f.window, s).isEmpty());
    CHECK(f.randomise->shortcut() == QKeySequence(QStringLiteral("Ctrl+Shift+K")));

    shortcuts::reset(f.window, s);
    CHECK(f.randomise->shortcut() == QKeySequence(QStringLiteral("Ctrl+R")));
}

TEST_CASE("reset puts every shipped default back", "[ui][shortcuts]") {
    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    Fixture f;
    QSettings s(iniIn(dir), QSettings::IniFormat);
    (void)shortcuts::apply(f.window, s);  // records the defaults

    f.undo->setShortcut(QKeySequence(QStringLiteral("Ctrl+Shift+Z")));
    f.randomise->setShortcut(QKeySequence());
    shortcuts::reset(f.window, s);

    CHECK(f.undo->shortcut() == QKeySequence(QStringLiteral("Ctrl+Z")));
    CHECK(f.randomise->shortcut() == QKeySequence(QStringLiteral("Ctrl+R")));
    s.sync();
    s.beginGroup(QString::fromLatin1(shortcuts::kGroup));
    CHECK(s.childKeys().isEmpty());
    s.endGroup();
}

// The names in the settings file have to be the names the REAL window uses.
// Every test above builds its own two-action fixture, which proves the module
// and nothing about whether `edit.randomise` is what MainWindow actually calls
// that action -- a rename there would silently stop every override working.
TEST_CASE("the real window's actions are addressable by the names we document", "[ui][shortcuts]") {
    mh::ui::TaskRegistry tasks;
    mh::ui::MainWindow window(std::filesystem::path{}, tasks);

    QStringList named;
    for (const QAction* a : window.findChildren<QAction*>()) {
        if (!a->objectName().isEmpty()) named += a->objectName();
    }
    INFO(named.join(QStringLiteral(", ")).toStdString());

    // The three the menus bind by hand, and so the three most likely to be
    // renamed without anyone thinking about the settings file.
    for (const char* name : {"edit.undo", "edit.redo", "edit.randomise"}) {
        CHECK(named.contains(QString::fromLatin1(name)));
    }

    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    QSettings s(dir.filePath(QStringLiteral("real.ini")), QSettings::IniFormat);
    s.setValue(QStringLiteral("shortcuts/edit.randomise"), QStringLiteral("Ctrl+Shift+G"));
    CHECK(shortcuts::apply(window, s).isEmpty());

    const QAction* randomise = window.findChild<QAction*>(QStringLiteral("edit.randomise"));
    REQUIRE(randomise != nullptr);
    CHECK(randomise->shortcut() == QKeySequence(QStringLiteral("Ctrl+Shift+G")));
}

// The case the unit test above CANNOT reach, and the app did.
//
// QSettings caches parsed file data per process, so a value written as an int
// in this process reads back as an int -- which is how the type check alone
// looked sufficient. A file written by something else (a hand edit, or a
// migration from the reference's shortcuts.ini) arrives as STRINGS, and
// `edit.randomise=5` then sailed through as the "5" key. Found by running the
// application against a real INI, not by a test.
TEST_CASE("a migrated INI written by another process is still refused", "[ui][shortcuts]") {
    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("migrated.ini"));
    {
        // Written as TEXT, deliberately: no QSettings type hints, exactly like
        // a file a user edited or a converter produced.
        QFile f(path);
        REQUIRE(f.open(QIODevice::WriteOnly | QIODevice::Text));
        f.write(
            "[shortcuts]\n"
            "edit.undo=90\n"               // Qt::Key_Z
            "edit.randomise=67108864\n");  // Qt's CTRL modifier bit
    }

    Fixture f;
    QSettings s(path, QSettings::IniFormat);
    const QStringList problems = shortcuts::apply(f.window, s);
    const QString all          = problems.join(QStringLiteral(" | "));
    INFO(all.toStdString());
    CHECK(problems.size() == 2);
    // Asserted on the whole message set, not on problems.at(0): the order is
    // not part of the contract and pinning it makes the test fail for a reason
    // that does not matter.
    CHECK(all.count(QStringLiteral("raw Qt key code")) == 2);
    CHECK(f.undo->shortcut() == QKeySequence(QStringLiteral("Ctrl+Z")));
    CHECK(f.randomise->shortcut() == QKeySequence(QStringLiteral("Ctrl+R")));
}

// Where the rule stops, stated so nobody "fixes" it later.
//
// A single digit is ACCEPTED as that digit key, so a migrated file saying `5`
// binds the 5 key rather than being refused. That is not a gap: Qt's own codes
// for the digit keys are their ASCII values, 48 to 57, and every other code a
// reference file can hold is larger still -- so no realistic migrated value is
// ever a single digit, while `5` is exactly what a user writes to mean the 5
// key. There is no information in the file to separate the two, and reading it
// literally is the choice that never surprises the person who typed it.
TEST_CASE("a single digit is a legitimate shortcut, not a key code", "[ui][shortcuts]") {
    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("digit.ini"));
    {
        QFile f(path);
        REQUIRE(f.open(QIODevice::WriteOnly | QIODevice::Text));
        f.write("[shortcuts]\nedit.randomise=5\n");
    }
    Fixture f;
    QSettings s(path, QSettings::IniFormat);
    CHECK(shortcuts::apply(f.window, s).isEmpty());
    CHECK(f.randomise->shortcut() == QKeySequence(QStringLiteral("5")));
}
