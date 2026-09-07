// SPDX-License-Identifier: Apache-2.0
#include "makehuman/ui/Shortcuts.h"

#include <QAction>
#include <QKeySequence>
#include <QMainWindow>
#include <QRegularExpression>
#include <QSettings>
#include <QVariant>

namespace mh::ui::shortcuts {
namespace {

/// Where an action's shipped sequence is remembered.
///
/// On the action itself rather than in a map owned by this module: the actions
/// belong to the window, and a side table would have to be kept in step with
/// their lifetime for no gain.
constexpr auto kDefaultProperty = "mh.defaultShortcut";

/// Every action that can be referred to by name, the window's own and its
/// menus'.
///
/// `findChildren` rather than `QWidget::actions()`: menu actions are children
/// of the QMenu, not of the window, so `actions()` sees only the ones added
/// directly and would quietly leave every File and Edit command unbindable.
QList<QAction*> namedActions(const QMainWindow& window) {
    QList<QAction*> out;
    for (QAction* a : window.findChildren<QAction*>()) {
        if (!a->objectName().isEmpty()) out.push_back(a);
    }
    return out;
}

/// Whether @p seq contains a key Qt could not make sense of.
///
/// `QKeySequence::isEmpty()` is NOT enough, which a test caught before this
/// shipped: Qt parses `"NotAKey+++"` into a one-element sequence holding
/// `Qt::Key_unknown` rather than rejecting it, so a typo in the settings file
/// would have been accepted as a real -- and unpressable -- shortcut.
bool hasUnknownKey(const QKeySequence& seq) {
    for (uint i = 0; i < static_cast<uint>(seq.count()); ++i) {
        if (seq[i].key() == Qt::Key_unknown) return true;
    }
    return false;
}

QKeySequence defaultOf(const QAction* a) {
    const QVariant v = a->property(kDefaultProperty);
    return v.isValid() ? QKeySequence(v.toString(), QKeySequence::PortableText) : a->shortcut();
}

}  // namespace

QStringList apply(QMainWindow& window, QSettings& settings) {
    const QList<QAction*> actions = namedActions(window);

    // Remembered before anything is overridden, and only once: a second apply
    // must not record an override as though it were the shipped default.
    for (QAction* a : actions) {
        if (!a->property(kDefaultProperty).isValid()) {
            a->setProperty(kDefaultProperty, a->shortcut().toString(QKeySequence::PortableText));
        }
    }

    QStringList problems;
    settings.beginGroup(QString::fromLatin1(kGroup));
    const QStringList keys = settings.childKeys();

    // What each action WILL have, seeded with the defaults. Collisions are
    // judged against this rather than against the live actions, so the outcome
    // does not depend on which override is read first.
    QHash<QString, QKeySequence> wanted;
    QHash<QString, QAction*> byName;
    for (QAction* a : actions) {
        byName.insert(a->objectName(), a);
        wanted.insert(a->objectName(), defaultOf(a));
    }

    for (const QString& name : keys) {
        QAction* action = byName.value(name, nullptr);
        if (action == nullptr) {
            problems += QStringLiteral("%1: no action has that name, ignoring").arg(name);
            continue;
        }

        const QVariant raw = settings.value(name);
        const QString text = raw.toString();

        // A bare integer is the reference's format (`mhmain.py:1022`), and it
        // has to be refused: some of those numbers parse as perfectly valid
        // sequences, so accepting them would rebind a command to something
        // nobody chose. `5` is the reference's code for nothing in particular
        // and Qt's portable text for the "5" key.
        //
        // TWO tests, because there are two ways the value arrives:
        //
        //  * a genuine int QVariant -- `setValue(name, 5)`. Portable text is a
        //    string by definition, so a non-string type is never one.
        //  * an all-digit STRING, which is what a hand-edited or migrated file
        //    gives: QSettings only knows a value's type if it wrote it itself
        //    in this process. Measured -- the type check alone let
        //    `edit.randomise=5` through when the app read a real INI, while the
        //    unit test passed because QSettings had cached its own int.
        //
        // Length is what separates them: portable text for a digit key is
        // exactly one character, so `"5"` is a legitimate shortcut and any
        // longer run of digits cannot be portable text for anything.
        const bool nonStringType =
            raw.typeId() == QMetaType::Int || raw.typeId() == QMetaType::LongLong;
        static const QRegularExpression manyDigits(QStringLiteral("^[0-9]{2,}$"));
        if (nonStringType || manyDigits.match(text).hasMatch()) {
            problems += QStringLiteral(
                            "%1: %2 is a raw Qt key code, not a shortcut -- "
                            "write it as text, e.g. \"Ctrl+Z\"")
                            .arg(name, text);
            continue;
        }

        // Empty is a deliberate "no shortcut for this", which the reference
        // cannot express -- (0, 0) is a valid key pair there.
        if (text.isEmpty()) {
            wanted.insert(name, QKeySequence());
            continue;
        }

        const QKeySequence seq(text, QKeySequence::PortableText);
        if (seq.isEmpty() || hasUnknownKey(seq)) {
            problems += QStringLiteral("%1: \"%2\" is not a key sequence, keeping the default")
                            .arg(name, text);
            continue;
        }
        wanted.insert(name, seq);
    }
    settings.endGroup();

    // Collisions last, once every wish is known. Qt gives an ambiguous
    // shortcut to NEITHER action, so the user loses both commands with nothing
    // to say why -- the reference refuses them too (`mhmain.py:1387`).
    // Walked over a NAME-SORTED list, not the hash: the message names two
    // actions and must read the same way on every run, and QHash's order is
    // deliberately not stable.
    QStringList names = wanted.keys();
    names.sort();

    QStringList clashing;
    for (qsizetype i = 0; i < names.size(); ++i) {
        const QKeySequence& seq = wanted[names.at(i)];
        if (seq.isEmpty()) continue;
        for (qsizetype j = i + 1; j < names.size(); ++j) {
            if (wanted[names.at(j)] != seq) continue;
            problems +=
                QStringLiteral("%1 and %2 both want \"%3\"; keeping both defaults")
                    .arg(names.at(i), names.at(j), seq.toString(QKeySequence::PortableText));
            clashing += names.at(i);
            clashing += names.at(j);
        }
    }

    for (const QString& name : names) {
        if (clashing.contains(name)) continue;
        byName.value(name)->setShortcut(wanted[name]);
    }
    return problems;
}

void save(const QMainWindow& window, QSettings& settings) {
    settings.beginGroup(QString::fromLatin1(kGroup));
    for (const QAction* a : namedActions(window)) {
        const QString now  = a->shortcut().toString(QKeySequence::PortableText);
        const QVariant def = a->property(kDefaultProperty);
        if (def.isValid() && def.toString() == now) {
            // Back on the default: remove the override rather than record it,
            // so a later release changing that default still reaches the user.
            settings.remove(a->objectName());
            continue;
        }
        if (!def.isValid()) continue;  // apply() never ran; nothing to compare
        settings.setValue(a->objectName(), now);
    }
    settings.endGroup();
}

void reset(QMainWindow& window, QSettings& settings) {
    for (QAction* a : namedActions(window)) {
        const QVariant def = a->property(kDefaultProperty);
        if (def.isValid()) {
            a->setShortcut(QKeySequence(def.toString(), QKeySequence::PortableText));
        }
    }
    settings.remove(QString::fromLatin1(kGroup));
}

}  // namespace mh::ui::shortcuts
