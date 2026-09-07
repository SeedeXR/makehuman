// SPDX-License-Identifier: Apache-2.0
//
// Settings > Shortcuts... -- one place to change how the application is driven.
//
// Owner directive 10 (2026-09-07) put the design call here, judged on how
// intuitive the result is. Four decisions, each with a reason:
//
//   * **Keyboard and mouse in ONE dialog.** The reference splits them across
//     `5_settings_shortcuts.py` and `5_settings_mouse.py`, so a user has to
//     know whether "how do I pan" is a shortcut question or a mouse question
//     before they can find the answer. One list of commands, some bound to keys
//     and some to drags.
//   * **Click a row, then press.** The reference's own interaction
//     (`5_settings_shortcuts.py:123`) and what every DCC does. A row being
//     recorded says so; Escape abandons it.
//   * **Conflicts on the row.** A modal alert hides the one thing the user
//     needs to read -- which command they collided with -- behind an OK button.
//   * **Reset one row, or reset all.** "Put this back" and "start over" are
//     different wishes; the Workspace menu already ships that pair.
//
// Changes apply LIVE so the user can try a binding without committing, which
// makes Cancel obliged to undo them.
#pragma once

#include <QDialog>
#include <QString>

#include <memory>

class QMainWindow;
class QSettings;

namespace mh::ui {

class MouseBindings;

class ShortcutsDialog : public QDialog {
    Q_OBJECT

public:
    /// @param window whose named QActions are listed and rebound.
    /// @param mouse the camera gesture table, listed alongside them.
    /// @param settings where OK writes, and what Cancel leaves untouched.
    ShortcutsDialog(QMainWindow& window, MouseBindings& mouse, QSettings& settings,
                    QWidget* parent = nullptr);
    ~ShortcutsDialog() override;

    ShortcutsDialog(const ShortcutsDialog&)            = delete;
    ShortcutsDialog& operator=(const ShortcutsDialog&) = delete;

    /// Rows, in the order shown.
    [[nodiscard]] int rowCount() const;
    /// The row for an action `objectName` or a `mouse.<verb>` id, or -1.
    [[nodiscard]] int rowFor(const QString& id) const;
    /// The gesture currently shown on @p row, e.g. `"Ctrl+Z"` or `"Middle"`.
    [[nodiscard]] QString textAt(int row) const;
    /// Why the last attempt on @p row was refused, or empty.
    [[nodiscard]] QString problemAt(int row) const;

    /// Starts listening for the next key or button press.
    void beginRecording(int row);
    [[nodiscard]] bool isRecording() const;

    void resetRow(int row);
    void resetAll();

    /// Writes the overrides and closes.
    void accept() override;
    /// Puts every live binding back as it was and writes nothing. Applying
    /// changes live is what obliges Cancel to undo them.
    void reject() override;

protected:
    /// Recording intercepts input before any widget sees it, which is the only
    /// way to capture sequences the dialog itself would otherwise act on --
    /// Escape, Tab, Ctrl+Z.
    bool event(QEvent* e) override;

private:
    /// Puts @p row's binding back to @p text. The two restore points differ:
    /// Reset uses the shipped default, Cancel uses what was in effect on open.
    void restore(int row, const QString& text);

    struct Impl;
    std::unique_ptr<Impl> d_;
};

}  // namespace mh::ui
