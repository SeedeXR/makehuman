// SPDX-License-Identifier: Apache-2.0
#include "makehuman/ui/ShortcutsDialog.h"

#include "makehuman/ui/MouseBindings.h"
#include "makehuman/ui/Shortcuts.h"

#include <QAction>
#include <QDialogButtonBox>
#include <QHeaderView>
#include <QKeyEvent>
#include <QLabel>
#include <QMainWindow>
#include <QMenu>
#include <QMouseEvent>
#include <QPushButton>
#include <QSettings>
#include <QTableWidget>
#include <QVBoxLayout>

#include <algorithm>

namespace mh::ui {
namespace {

/// A row is either a keyboard action or one of the camera verbs.
struct Row {
    QString id;         ///< `edit.undo`, or `mouse.orbit`
    QString label;      ///< what the user reads
    QAction* action{};  ///< null for a mouse row
    NavVerb verb{NavVerb::None};
    /// The SHIPPED default -- what "Reset" returns to. For a key that is the
    /// sequence `shortcuts::apply` recorded on the action; for a mouse verb it
    /// is a freshly-constructed `MouseBindings`.
    QString shipped;
    /// What was in effect when the dialog OPENED -- what "Cancel" returns to.
    /// Not the same thing: a user with a saved override expects Reset to give
    /// them the default back and Cancel to give them their override back.
    QString onOpen;
    QString problem;  ///< why the last attempt was refused, or empty
};

/// Modifier keys pressed on the way to a real one.
///
/// Without this the first Ctrl of "Ctrl+K" ends the recording and binds the
/// command to nothing usable -- and every retry does the same, which is how a
/// rebinding dialog earns a reputation for being broken.
bool isBareModifier(int key) {
    return key == Qt::Key_Control || key == Qt::Key_Shift || key == Qt::Key_Alt ||
           key == Qt::Key_Meta || key == Qt::Key_AltGr || key == Qt::Key_CapsLock ||
           key == Qt::Key_NumLock || key == Qt::Key_ScrollLock;
}

}  // namespace

struct ShortcutsDialog::Impl {
    QMainWindow* window{};
    MouseBindings* mouse{};
    QSettings* settings{};
    QTableWidget* table{};
    std::vector<Row> rows;
    int recording{-1};

    /// The gesture @p verb holds in @p bindings, or empty.
    ///
    /// A search rather than a lookup: `MouseBindings` deliberately exposes no
    /// reverse map, because the forward question -- "what does this drag do" --
    /// is the one the viewport asks sixty times a second, and five buttons by
    /// five modifiers is nothing to scan once for a dialog row.
    [[nodiscard]] static QString gestureIn(const MouseBindings& bindings, NavVerb verb) {
        for (const Qt::MouseButton button : {Qt::LeftButton, Qt::MiddleButton, Qt::RightButton,
                                             Qt::BackButton, Qt::ForwardButton}) {
            for (const Qt::KeyboardModifiers mods :
                 {Qt::KeyboardModifiers(Qt::NoModifier), Qt::KeyboardModifiers(Qt::ControlModifier),
                  Qt::KeyboardModifiers(Qt::AltModifier), Qt::KeyboardModifiers(Qt::ShiftModifier),
                  Qt::KeyboardModifiers(Qt::MetaModifier)}) {
                if (bindings.verbFor(button, mods) == verb) {
                    return MouseBindings::toText(button, mods);
                }
            }
        }
        return {};
    }

    [[nodiscard]] QString gestureOf(const Row& r) const {
        if (r.action != nullptr) return r.action->shortcut().toString(QKeySequence::PortableText);
        return gestureIn(*mouse, r.verb);
    }

    void refresh() {
        for (size_t i = 0; i < rows.size(); ++i) {
            const Row& r    = rows[i];
            const int row   = static_cast<int>(i);
            const bool live = recording == row;
            auto* name      = new QTableWidgetItem(r.label);
            // The problem rides on the COMMAND cell as a tooltip and as text
            // after the gesture, so it is visible without a modal alert
            // stealing the very information the user needs.
            if (!r.problem.isEmpty()) name->setToolTip(r.problem);
            table->setItem(row, 0, name);

            QString shown = live ? tr("Press keys or a mouse button…") : gestureOf(r);
            if (shown.isEmpty() && !live) shown = tr("None");
            if (!r.problem.isEmpty()) shown += QStringLiteral("  —  ") + r.problem;
            auto* gesture = new QTableWidgetItem(shown);
            if (!r.problem.isEmpty()) gesture->setToolTip(r.problem);
            table->setItem(row, 1, gesture);
        }
    }

    /// Which OTHER row already holds @p text, or -1.
    [[nodiscard]] int heldBy(int exceptRow, const QString& text) const {
        if (text.isEmpty()) return -1;
        for (size_t i = 0; i < rows.size(); ++i) {
            if (static_cast<int>(i) == exceptRow) continue;
            // Keys never collide with buttons: they cannot be pressed as the
            // same thing, and refusing "Ctrl+Z clashes with Middle" would be
            // nonsense.
            if ((rows[i].action != nullptr) !=
                (rows[static_cast<size_t>(exceptRow)].action != nullptr)) {
                continue;
            }
            if (gestureOf(rows[i]) == text) return static_cast<int>(i);
        }
        return -1;
    }
};

ShortcutsDialog::ShortcutsDialog(QMainWindow& window, MouseBindings& mouse, QSettings& settings,
                                 QWidget* parent)
    : QDialog(parent), d_(std::make_unique<Impl>()) {
    d_->window   = &window;
    d_->mouse    = &mouse;
    d_->settings = &settings;

    setObjectName(QStringLiteral("dialog.shortcuts"));
    setWindowTitle(tr("Shortcuts"));

    // Keyboard first, in the order the window created the actions, then the
    // camera verbs -- both kinds in ONE list, because "how do I pan" should not
    // require knowing it is a mouse question rather than a key one.
    for (QAction* a : window.findChildren<QAction*>()) {
        if (a->objectName().isEmpty()) continue;
        if (a->isSeparator() || a->menu() != nullptr) continue;
        QString label = a->text();
        label.remove(QLatin1Char('&'));
        d_->rows.push_back(Row{a->objectName(),
                               label.isEmpty() ? a->objectName() : label,
                               a,
                               NavVerb::None,
                               {},
                               {},
                               {}});
    }
    d_->rows.push_back(Row{QStringLiteral("mouse.orbit"),
                           tr("Orbit the camera"),
                           nullptr,
                           NavVerb::Orbit,
                           {},
                           {},
                           {}});
    d_->rows.push_back(
        Row{QStringLiteral("mouse.pan"), tr("Pan the camera"), nullptr, NavVerb::Pan, {}, {}, {}});

    // Records each action's shipped sequence on the action itself, which is
    // what `shortcuts::save` compares against to tell an override from a
    // default. Idempotent -- it never overwrites a default it already recorded
    // -- so calling it again here is safe even though the application already
    // did at start-up.
    (void)shortcuts::apply(window, settings);

    // TWO restore points, captured before anything changes. `shipped` is the
    // default that Reset returns to; `onOpen` is what Cancel returns to.
    const MouseBindings shippedMouse;
    for (Row& r : d_->rows) {
        r.onOpen = d_->gestureOf(r);
        if (r.action != nullptr) {
            const QVariant def = r.action->property("mh.defaultShortcut");
            r.shipped          = def.isValid() ? def.toString() : r.onOpen;
        } else {
            r.shipped = Impl::gestureIn(shippedMouse, r.verb);
        }
    }

    auto* column = new QVBoxLayout(this);
    auto* hint   = new QLabel(tr("Select a command, then press the keys or mouse button to use "
                                   "for it. Escape cancels."),
                              this);
    hint->setObjectName(QStringLiteral("shortcuts.hint"));
    hint->setWordWrap(true);
    column->addWidget(hint);

    d_->table = new QTableWidget(static_cast<int>(d_->rows.size()), 2, this);
    d_->table->setObjectName(QStringLiteral("shortcuts.table"));
    d_->table->setHorizontalHeaderLabels({tr("Command"), tr("Shortcut")});
    d_->table->verticalHeader()->setVisible(false);
    d_->table->setSelectionBehavior(QAbstractItemView::SelectRows);
    d_->table->setSelectionMode(QAbstractItemView::SingleSelection);
    d_->table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    // The command column sizes to its CONTENTS, the gesture column takes the
    // rest. Without this the default equal split elides the command names --
    // "Orbit the camera" became "Orbit the ...", which is the same defect that
    // made the modifier sub-tabs unreadable, in a dialog written an hour after
    // fixing that one. Seen in a screenshot, not by any assertion.
    d_->table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    d_->table->horizontalHeader()->setStretchLastSection(true);
    column->addWidget(d_->table, 1);

    auto* buttons  = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    auto* rowReset = buttons->addButton(tr("Reset"), QDialogButtonBox::ResetRole);
    rowReset->setObjectName(QStringLiteral("shortcuts.resetRow"));
    auto* allReset = buttons->addButton(tr("Reset All"), QDialogButtonBox::ResetRole);
    allReset->setObjectName(QStringLiteral("shortcuts.resetAll"));
    column->addWidget(buttons);

    connect(buttons, &QDialogButtonBox::accepted, this, &ShortcutsDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &ShortcutsDialog::reject);
    connect(rowReset, &QPushButton::clicked, this, [this] { resetRow(d_->table->currentRow()); });
    connect(allReset, &QPushButton::clicked, this, [this] { resetAll(); });
    // Single click starts recording: the row IS the control, so a second
    // "click here to edit" step would be ceremony.
    connect(d_->table, &QTableWidget::cellClicked, this,
            [this](int row, int) { beginRecording(row); });

    d_->refresh();
    resize(520, 420);
}

ShortcutsDialog::~ShortcutsDialog() = default;

int ShortcutsDialog::rowCount() const {
    return static_cast<int>(d_->rows.size());
}

int ShortcutsDialog::rowFor(const QString& id) const {
    const auto at = std::ranges::find_if(d_->rows, [&id](const Row& r) { return r.id == id; });
    return at == d_->rows.end() ? -1 : static_cast<int>(at - d_->rows.begin());
}

QString ShortcutsDialog::textAt(int row) const {
    if (row < 0 || row >= rowCount()) return {};
    return d_->gestureOf(d_->rows[static_cast<size_t>(row)]);
}

QString ShortcutsDialog::problemAt(int row) const {
    if (row < 0 || row >= rowCount()) return {};
    return d_->rows[static_cast<size_t>(row)].problem;
}

bool ShortcutsDialog::isRecording() const {
    return d_->recording >= 0;
}

void ShortcutsDialog::beginRecording(int row) {
    if (row < 0 || row >= rowCount()) return;
    d_->recording                              = row;
    d_->rows[static_cast<size_t>(row)].problem = QString{};
    d_->refresh();
}

void ShortcutsDialog::resetRow(int row) {
    if (row < 0 || row >= rowCount()) return;
    restore(row, d_->rows[static_cast<size_t>(row)].shipped);
}

void ShortcutsDialog::resetAll() {
    for (int row = 0; row < rowCount(); ++row)
        resetRow(row);
}

void ShortcutsDialog::restore(int row, const QString& text) {
    if (row < 0 || row >= rowCount()) return;
    Row& r    = d_->rows[static_cast<size_t>(row)];
    r.problem = QString{};
    if (r.action != nullptr) {
        r.action->setShortcut(QKeySequence(text, QKeySequence::PortableText));
    } else {
        Qt::MouseButton button{};
        Qt::KeyboardModifiers mods{};
        if (MouseBindings::fromText(text, button, mods)) {
            (void)d_->mouse->bind(r.verb, button, mods);
        }
    }
    d_->recording = -1;
    d_->refresh();
}

void ShortcutsDialog::accept() {
    shortcuts::save(*d_->window, *d_->settings);
    d_->mouse->save(*d_->settings);
    QDialog::accept();
}

void ShortcutsDialog::reject() {
    // Bindings are applied live so a user can try one before committing; that
    // is exactly what obliges Cancel to put them all back -- to what was in
    // effect when the dialog OPENED, not to the shipped default, which is what
    // Reset is for.
    for (int row = 0; row < rowCount(); ++row) {
        restore(row, d_->rows[static_cast<size_t>(row)].onOpen);
    }
    QDialog::reject();
}

bool ShortcutsDialog::event(QEvent* e) {
    if (!isRecording()) return QDialog::event(e);

    const int row = d_->recording;
    Row& r        = d_->rows[static_cast<size_t>(row)];

    if (e->type() == QEvent::KeyPress) {
        auto* key = static_cast<QKeyEvent*>(e);
        if (isBareModifier(key->key())) return true;  // still waiting for a real key
        d_->recording = -1;
        if (key->key() == Qt::Key_Escape) {
            d_->refresh();
            return true;
        }
        if (r.action == nullptr) {
            r.problem = tr("This command wants a mouse button.");
            d_->refresh();
            return true;
        }
        const QKeySequence seq(QKeyCombination(key->modifiers(), Qt::Key(key->key())));
        const QString text = seq.toString(QKeySequence::PortableText);
        if (const int taken = d_->heldBy(row, text); taken >= 0) {
            r.problem = tr("Already used by %1").arg(d_->rows[static_cast<size_t>(taken)].label);
        } else {
            r.action->setShortcut(seq);
        }
        d_->refresh();
        return true;
    }

    if (e->type() == QEvent::MouseButtonPress) {
        auto* mouse   = static_cast<QMouseEvent*>(e);
        d_->recording = -1;
        if (r.action != nullptr) {
            r.problem = tr("This command wants a key.");
            d_->refresh();
            return true;
        }
        const QString text = MouseBindings::toText(mouse->button(), mouse->modifiers());
        if (text.isEmpty()) {
            r.problem = tr("That button cannot be bound.");
        } else if (const int taken = d_->heldBy(row, text); taken >= 0) {
            r.problem = tr("Already used by %1").arg(d_->rows[static_cast<size_t>(taken)].label);
        } else if (!d_->mouse->bind(r.verb, mouse->button(), mouse->modifiers())) {
            r.problem = tr("That gesture is already taken.");
        }
        d_->refresh();
        return true;
    }

    return QDialog::event(e);
}

}  // namespace mh::ui
