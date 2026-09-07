// SPDX-License-Identifier: AGPL-3.0-or-later
//
// The rebinding dialog: Settings > Shortcuts...
//
// Owner directive 10 (2026-09-07) gave the design call here, judged on how
// intuitive the result is. The decisions it produced, and what each is worth
// testing for:
//
//  * ONE dialog for keyboard AND mouse. The reference splits them across
//    `5_settings_shortcuts.py` and `5_settings_mouse.py`; someone asking "how
//    do I drive this thing" should not have to know which half their question
//    is in.
//  * Click a row, then press. The reference's own interaction
//    (`5_settings_shortcuts.py:123`) and every DCC's.
//  * Conflicts shown ON the row, not in a modal alert that has to be dismissed
//    before the user can see what they collided with.
//  * Cancel really cancels -- nothing applied, nothing written.
#include "makehuman/ui/MouseBindings.h"
#include "makehuman/ui/ShortcutsDialog.h"

#include <catch2/catch_test_macros.hpp>

#include <QAction>
#include <QApplication>
#include <QFontMetrics>
#include <QHeaderView>
#include <QKeyEvent>
#include <QMainWindow>
#include <QMouseEvent>
#include <QSettings>
#include <QTableWidget>
#include <QTemporaryDir>

#include <cstdlib>

using namespace mh::ui;

namespace {

/// A window with two named actions and a viewport-less binding table, which is
/// all the dialog addresses.
struct Fixture {
    QMainWindow window;
    MouseBindings mouse;
    QAction* undo{};
    QAction* randomise{};

    Fixture() {
        undo = new QAction(QStringLiteral("Undo"), &window);
        undo->setObjectName(QStringLiteral("edit.undo"));
        undo->setShortcut(QKeySequence(QStringLiteral("Ctrl+Z")));
        window.addAction(undo);

        randomise = new QAction(QStringLiteral("Randomise"), &window);
        randomise->setObjectName(QStringLiteral("edit.randomise"));
        randomise->setShortcut(QKeySequence(QStringLiteral("Ctrl+R")));
        window.addAction(randomise);
    }
};

void pressKeys(ShortcutsDialog& dialog, int key, Qt::KeyboardModifiers mods) {
    QKeyEvent e(QEvent::KeyPress, key, mods);
    QApplication::sendEvent(&dialog, &e);
}

}  // namespace

TEST_CASE("the dialog lists every rebindable command, keys and mouse together", "[ui][rebind]") {
    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    QSettings s(dir.filePath(QStringLiteral("d.ini")), QSettings::IniFormat);

    Fixture f;
    ShortcutsDialog dialog(f.window, f.mouse, s);

    // Both named actions, and both camera verbs, in one list.
    CHECK(dialog.rowFor(QStringLiteral("edit.undo")) >= 0);
    CHECK(dialog.rowFor(QStringLiteral("edit.randomise")) >= 0);
    CHECK(dialog.rowFor(QStringLiteral("mouse.orbit")) >= 0);
    CHECK(dialog.rowFor(QStringLiteral("mouse.pan")) >= 0);
    CHECK(dialog.rowCount() == 4);

    // Showing what is bound today, not blanks.
    CHECK(dialog.textAt(dialog.rowFor(QStringLiteral("edit.undo"))) == QStringLiteral("Ctrl+Z"));
    CHECK(dialog.textAt(dialog.rowFor(QStringLiteral("mouse.pan"))) == QStringLiteral("Middle"));
}

TEST_CASE("clicking a row and pressing keys rebinds it", "[ui][rebind]") {
    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    QSettings s(dir.filePath(QStringLiteral("d.ini")), QSettings::IniFormat);

    Fixture f;
    ShortcutsDialog dialog(f.window, f.mouse, s);
    const int row = dialog.rowFor(QStringLiteral("edit.randomise"));
    REQUIRE(row >= 0);

    dialog.beginRecording(row);
    CHECK(dialog.isRecording());
    pressKeys(dialog, Qt::Key_K, Qt::ControlModifier | Qt::ShiftModifier);

    CHECK_FALSE(dialog.isRecording());
    CHECK(dialog.textAt(row) == QStringLiteral("Ctrl+Shift+K"));
    // Live: the action itself already carries it, before OK is pressed.
    CHECK(f.randomise->shortcut() == QKeySequence(QStringLiteral("Ctrl+Shift+K")));
}

TEST_CASE("a bare modifier does not end the recording", "[ui][rebind]") {
    // Holding Ctrl on the way to Ctrl+K must not commit "Ctrl" as the binding
    // -- which is what a naive key handler does, and it is instantly noticed
    // because every attempt yields the same useless result.
    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    QSettings s(dir.filePath(QStringLiteral("d.ini")), QSettings::IniFormat);

    Fixture f;
    ShortcutsDialog dialog(f.window, f.mouse, s);
    const int row = dialog.rowFor(QStringLiteral("edit.randomise"));
    dialog.beginRecording(row);

    pressKeys(dialog, Qt::Key_Control, Qt::ControlModifier);
    CHECK(dialog.isRecording());
    pressKeys(dialog, Qt::Key_Shift, Qt::ControlModifier | Qt::ShiftModifier);
    CHECK(dialog.isRecording());
    CHECK(dialog.textAt(row) == QStringLiteral("Ctrl+R"));  // unchanged so far

    pressKeys(dialog, Qt::Key_K, Qt::ControlModifier | Qt::ShiftModifier);
    CHECK_FALSE(dialog.isRecording());
    CHECK(dialog.textAt(row) == QStringLiteral("Ctrl+Shift+K"));
}

TEST_CASE("Escape abandons a recording and keeps the old binding", "[ui][rebind]") {
    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    QSettings s(dir.filePath(QStringLiteral("d.ini")), QSettings::IniFormat);

    Fixture f;
    ShortcutsDialog dialog(f.window, f.mouse, s);
    const int row = dialog.rowFor(QStringLiteral("edit.randomise"));
    dialog.beginRecording(row);
    pressKeys(dialog, Qt::Key_Escape, Qt::NoModifier);

    CHECK_FALSE(dialog.isRecording());
    CHECK(dialog.textAt(row) == QStringLiteral("Ctrl+R"));
    CHECK(f.randomise->shortcut() == QKeySequence(QStringLiteral("Ctrl+R")));
}

TEST_CASE("a conflict is reported on the row, not in an alert", "[ui][rebind]") {
    // A modal alert would hide the very thing the user needs to see -- which
    // command they collided with. The row says so and the binding is refused.
    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    QSettings s(dir.filePath(QStringLiteral("d.ini")), QSettings::IniFormat);

    Fixture f;
    ShortcutsDialog dialog(f.window, f.mouse, s);
    const int row = dialog.rowFor(QStringLiteral("edit.randomise"));
    dialog.beginRecording(row);
    pressKeys(dialog, Qt::Key_Z, Qt::ControlModifier);  // already Undo's

    CHECK(dialog.textAt(row) == QStringLiteral("Ctrl+R"));  // refused
    INFO(dialog.problemAt(row).toStdString());
    CHECK(dialog.problemAt(row).contains(QStringLiteral("Undo")));
    CHECK(f.randomise->shortcut() == QKeySequence(QStringLiteral("Ctrl+R")));
    // ...and the other side is untouched.
    CHECK(f.undo->shortcut() == QKeySequence(QStringLiteral("Ctrl+Z")));
}

TEST_CASE("a mouse row records a button, not a key", "[ui][rebind]") {
    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    QSettings s(dir.filePath(QStringLiteral("d.ini")), QSettings::IniFormat);

    Fixture f;
    ShortcutsDialog dialog(f.window, f.mouse, s);
    const int row = dialog.rowFor(QStringLiteral("mouse.pan"));
    REQUIRE(row >= 0);

    dialog.beginRecording(row);
    QMouseEvent press(QEvent::MouseButtonPress, QPointF(1, 1), QPointF(1, 1), Qt::RightButton,
                      Qt::RightButton, Qt::AltModifier);
    QApplication::sendEvent(&dialog, &press);

    CHECK_FALSE(dialog.isRecording());
    CHECK(dialog.textAt(row) == QStringLiteral("Alt+Right"));
    CHECK(f.mouse.verbFor(Qt::RightButton, Qt::AltModifier) == NavVerb::Pan);
}

TEST_CASE("reset puts one row back, and reset-all puts every row back", "[ui][rebind]") {
    // Two different wishes. The Workspace menu already proves the pattern.
    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    QSettings s(dir.filePath(QStringLiteral("d.ini")), QSettings::IniFormat);

    Fixture f;
    ShortcutsDialog dialog(f.window, f.mouse, s);
    const int keyRow   = dialog.rowFor(QStringLiteral("edit.randomise"));
    const int mouseRow = dialog.rowFor(QStringLiteral("mouse.pan"));

    dialog.beginRecording(keyRow);
    pressKeys(dialog, Qt::Key_J, Qt::ControlModifier | Qt::AltModifier);
    REQUIRE(dialog.textAt(keyRow) == QStringLiteral("Ctrl+Alt+J"));

    dialog.resetRow(keyRow);
    CHECK(dialog.textAt(keyRow) == QStringLiteral("Ctrl+R"));
    CHECK(f.randomise->shortcut() == QKeySequence(QStringLiteral("Ctrl+R")));

    // Now change both kinds and reset everything at once.
    dialog.beginRecording(keyRow);
    pressKeys(dialog, Qt::Key_J, Qt::ControlModifier | Qt::AltModifier);
    dialog.beginRecording(mouseRow);
    QMouseEvent press(QEvent::MouseButtonPress, QPointF(1, 1), QPointF(1, 1), Qt::RightButton,
                      Qt::RightButton, Qt::NoModifier);
    QApplication::sendEvent(&dialog, &press);
    REQUIRE(dialog.textAt(mouseRow) == QStringLiteral("Right"));

    dialog.resetAll();
    CHECK(dialog.textAt(keyRow) == QStringLiteral("Ctrl+R"));
    CHECK(dialog.textAt(mouseRow) == QStringLiteral("Middle"));
    CHECK(f.mouse.verbFor(Qt::MiddleButton, Qt::NoModifier) == NavVerb::Pan);
}

TEST_CASE("OK writes the overrides, Cancel writes nothing and puts them back", "[ui][rebind]") {
    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("d.ini"));

    SECTION("accepting writes symbolically") {
        QSettings s(path, QSettings::IniFormat);
        Fixture f;
        ShortcutsDialog dialog(f.window, f.mouse, s);
        dialog.beginRecording(dialog.rowFor(QStringLiteral("edit.randomise")));
        pressKeys(dialog, Qt::Key_J, Qt::ControlModifier | Qt::AltModifier);
        dialog.accept();
        s.sync();

        CHECK(s.value(QStringLiteral("shortcuts/edit.randomise")).toString() ==
              QStringLiteral("Ctrl+Alt+J"));
        // Untouched commands stay out of the file, so a changed default in a
        // later release still reaches the user.
        CHECK_FALSE(s.contains(QStringLiteral("shortcuts/edit.undo")));
    }

    SECTION("cancelling reverts the live bindings too") {
        QSettings s(path, QSettings::IniFormat);
        Fixture f;
        ShortcutsDialog dialog(f.window, f.mouse, s);
        dialog.beginRecording(dialog.rowFor(QStringLiteral("edit.randomise")));
        pressKeys(dialog, Qt::Key_J, Qt::ControlModifier | Qt::AltModifier);
        REQUIRE(f.randomise->shortcut() == QKeySequence(QStringLiteral("Ctrl+Alt+J")));

        dialog.reject();
        s.sync();
        // Applied live means Cancel has to undo, or "Cancel" is a lie.
        CHECK(f.randomise->shortcut() == QKeySequence(QStringLiteral("Ctrl+R")));
        CHECK_FALSE(s.contains(QStringLiteral("shortcuts/edit.randomise")));
    }
}

// Reset and Cancel are NOT the same restore point, and every test above would
// pass with one value for both -- because with no saved override the shipped
// default and the on-open binding are the same string.
//
// With an override in the file they diverge, and the button labels promise
// different things: "Reset" means give me the default back, "Cancel" means
// forget what I just did.
TEST_CASE("Reset goes to the shipped default, Cancel to the saved override", "[ui][rebind]") {
    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("d.ini"));
    {
        QSettings seed(path, QSettings::IniFormat);
        seed.setValue(QStringLiteral("shortcuts/edit.randomise"), QStringLiteral("Ctrl+Alt+G"));
    }

    SECTION("Reset ignores the override and restores Ctrl+R") {
        QSettings s(path, QSettings::IniFormat);
        Fixture f;
        ShortcutsDialog dialog(f.window, f.mouse, s);
        const int row = dialog.rowFor(QStringLiteral("edit.randomise"));
        // The dialog opened with the override in effect.
        REQUIRE(dialog.textAt(row) == QStringLiteral("Ctrl+Alt+G"));

        dialog.resetRow(row);
        CHECK(dialog.textAt(row) == QStringLiteral("Ctrl+R"));
        CHECK(f.randomise->shortcut() == QKeySequence(QStringLiteral("Ctrl+R")));
    }

    SECTION("Cancel restores the override, not the default") {
        QSettings s(path, QSettings::IniFormat);
        Fixture f;
        ShortcutsDialog dialog(f.window, f.mouse, s);
        const int row = dialog.rowFor(QStringLiteral("edit.randomise"));
        REQUIRE(dialog.textAt(row) == QStringLiteral("Ctrl+Alt+G"));

        dialog.beginRecording(row);
        pressKeys(dialog, Qt::Key_J, Qt::ControlModifier | Qt::AltModifier);
        REQUIRE(dialog.textAt(row) == QStringLiteral("Ctrl+Alt+J"));

        dialog.reject();
        CHECK(f.randomise->shortcut() == QKeySequence(QStringLiteral("Ctrl+Alt+G")));
    }
}

// Every assertion above passes on a dialog that stores the right strings and
// paints an empty table -- zero-height rows, or a table sized to nothing. This
// grabs it and looks.
//
// `MH_REBIND_SHOT` writes the grab out for a human to look at in the session
// that changes this; the assertions run either way.
TEST_CASE("the dialog actually paints its rows", "[ui][rebind]") {
    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    QSettings s(dir.filePath(QStringLiteral("d.ini")), QSettings::IniFormat);

    Fixture f;
    ShortcutsDialog dialog(f.window, f.mouse, s);
    dialog.resize(560, 360);
    dialog.show();

    const QImage shot = dialog.grab().toImage();
    if (const char* out = std::getenv("MH_REBIND_SHOT")) {
        REQUIRE(shot.save(QString::fromUtf8(out)));
    }
    REQUIRE_FALSE(shot.isNull());

    // More than one colour: an unpainted dialog is a single flat fill.
    QSet<QRgb> colours;
    for (int y = 0; y < shot.height(); y += 3) {
        for (int x = 0; x < shot.width(); x += 3) {
            colours.insert(shot.pixel(x, y));
            if (colours.size() > 8) break;
        }
    }
    INFO("distinct colours sampled: " << colours.size());
    CHECK(colours.size() > 8);

    // The command column must FIT its labels: the default split elided
    // "Orbit the camera" to "Orbit the ...", visible only in the grab and the
    // same defect as the modifier sub-tabs.
    //
    // Asserted as the RESIZE MODE, not as a width. Measured: without the fix
    // the column is 100 px and the widest label needs 99 under the offscreen
    // font -- it passes by one pixel here while eliding on macOS, so a width
    // check cannot catch the regression. `ResizeToContents` is not a Qt
    // default (that is `Interactive`), so asserting it is not satisfied for
    // free -- which is what made the sub-tab assertion decorative until it was
    // pinned to a style.
    auto* fitted = dialog.findChild<QTableWidget*>(QStringLiteral("shortcuts.table"));
    REQUIRE(fitted != nullptr);
    CHECK(fitted->horizontalHeader()->sectionResizeMode(0) == QHeaderView::ResizeToContents);
    CHECK(fitted->horizontalHeader()->stretchLastSection());

    const QFontMetrics fm(fitted->font());
    for (int row = 0; row < fitted->rowCount(); ++row) {
        REQUIRE(fitted->item(row, 0) != nullptr);
        const QString label = fitted->item(row, 0)->text();
        INFO("row " << row << " \"" << label.toStdString() << "\" column " << fitted->columnWidth(0)
                    << " needs " << fm.horizontalAdvance(label));
        CHECK(fitted->columnWidth(0) >= fm.horizontalAdvance(label));
    }

    // And the rows have real height, which is what a table with items but no
    // layout pass looks like from the outside.
    auto* table = dialog.findChild<QTableWidget*>(QStringLiteral("shortcuts.table"));
    REQUIRE(table != nullptr);
    REQUIRE(table->rowCount() == dialog.rowCount());
    for (int row = 0; row < table->rowCount(); ++row) {
        INFO("row " << row);
        CHECK(table->rowHeight(row) > 0);
        REQUIRE(table->item(row, 0) != nullptr);
        CHECK_FALSE(table->item(row, 0)->text().isEmpty());
        REQUIRE(table->item(row, 1) != nullptr);
        CHECK_FALSE(table->item(row, 1)->text().isEmpty());
    }
}
