// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <QByteArray>
#include <QString>
#include <QUndoCommand>

#include <functional>
#include <utility>
#include <vector>

namespace mh::ui {

/// One named value change, undoable.
///
/// Deliberately knows nothing about modifiers: it holds a key, two floats and a
/// callback. That is what lets undo live in this Apache-2.0 module while the
/// AGPL side supplies what the key means.
class ValueChangeCommand : public QUndoCommand {
public:
    /// @param key      opaque; handed back to @p apply unchanged.
    /// @param mergeId  consecutive commands sharing a key AND a mergeId collapse
    ///                 into one, so dragging a slider is a single undo step
    ///                 rather than several hundred. The caller bumps it when an
    ///                 edit finishes -- without that, two deliberate nudges of
    ///                 the same slider minutes apart would also merge.
    /// @param apply    called with (key, value) for both undo and redo. It must
    ///                 not push further commands; see MainWindow::undoStack().
    ValueChangeCommand(QString key, float from, float to, int mergeId,
                       std::function<void(const QString&, float)> apply);

    void undo() override;
    void redo() override;

    [[nodiscard]] int id() const override;
    bool mergeWith(const QUndoCommand* other) override;

    [[nodiscard]] const QString& key() const { return key_; }

    [[nodiscard]] float from() const { return from_; }

    [[nodiscard]] float to() const { return to_; }

private:
    QString key_;
    float from_{};
    float to_{};
    int mergeId_{};
    std::function<void(const QString&, float)> apply_;
};

/// MANY named value changes as ONE undo step, applied in a single batch.
///
/// Not a `QUndoStack` macro over N `ValueChangeCommand`s, and the reason is
/// performance rather than tidiness. Randomising a character changes **245**
/// modifiers; each `ValueChangeCommand` triggers a full mesh rebuild, so a
/// macro would rebuild the mesh 245 times per undo and per redo. This hands
/// the whole set to the caller at once so it rebuilds exactly once — a
/// property the tests assert by counting calls, because "it worked" and "it
/// worked 245 times too slowly" look identical from the outside.
class MultiValueChangeCommand : public QUndoCommand {
public:
    struct Change {
        QString key;
        float from{};
        float to{};
    };

    /// @param apply called ONCE per undo or redo with every key and the value
    ///        it should take. It must not push further commands.
    MultiValueChangeCommand(
        QString text, std::vector<Change> changes,
        std::function<void(const std::vector<std::pair<QString, float>>&)> apply);

    void undo() override;
    void redo() override;

    [[nodiscard]] const std::vector<Change>& changes() const { return changes_; }

private:
    void applyDirection(bool forward);

    std::vector<Change> changes_;
    std::function<void(const std::vector<std::pair<QString, float>>&)> apply_;
};

/// One named choice, undoable -- a skin, a pose, later a garment.
///
/// Separate from ValueChangeCommand rather than a template over it: the payload
/// is a string, and the shared surface is three short methods. A template would
/// force the merge logic into the header and need `if constexpr` for the one
/// real difference.
///
/// **Consecutive choices in the same group merge**, like a drag. This is a
/// deliberate behaviour choice, not an accident: arrow-keying a closed combo
/// emits one change *per keystroke* (measured: three Down presses give three
/// changes), so without merging a traversal that ends where it started costs
/// three undo steps and three full skeleton reloads. Trying several skins to
/// compare them is one decision -- "I changed the skin" -- and the caller ends
/// the group when a different kind of edit happens.
class ChoiceChangeCommand : public QUndoCommand {
public:
    /// @param mergeId consecutive commands sharing a key AND a mergeId collapse.
    /// @param apply   called with (key, id) for both undo and redo. Must be
    ///                callable -- a null one is a programming error and throws
    ///                rather than silently doing nothing. It must not push
    ///                further commands.
    ChoiceChangeCommand(QString key, QString from, QString to, int mergeId,
                        std::function<void(const QString&, const QString&)> apply);

    void undo() override;
    void redo() override;

    [[nodiscard]] int id() const override;
    bool mergeWith(const QUndoCommand* other) override;

private:
    QString key_;
    QString from_;
    QString to_;
    int mergeId_{};
    std::function<void(const QString&, const QString&)> apply_;
};

/// A whole window layout, undoable.
///
/// The payload is `QMainWindow::saveState()`: one opaque QByteArray that
/// carries dock positions, sizes AND visibility (asserted in the UI tests --
/// if it carried only geometry, an undo would restore positions and leave docks
/// hidden). So the command is two blobs and a restore, with no knowledge of what
/// a dock or a preset is.
///
/// **`saveWorkspaceAs` is deliberately NOT undoable.** It writes a file and
/// changes no window state; "undoing" it would mean deleting a file the user
/// asked to save, which is not what an undo stack is for.
class LayoutChangeCommand : public QUndoCommand {
public:
    /// @param apply called with a state blob for both undo and redo. It must not
    ///        push further commands; see MainWindow::undoStack().
    LayoutChangeCommand(const QString& name, QByteArray from, QByteArray to,
                        std::function<void(const QByteArray&)> apply);

    void undo() override;
    /// Also called once by `QUndoStack::push`, when the layout is already in
    /// `to_`. No guard for that: `restoreState` is idempotent, so the first
    /// call is a no-op. A flag to skip it was measured to change nothing and
    /// removed rather than kept as unverifiable cleverness.
    void redo() override;

private:
    QByteArray from_;
    QByteArray to_;
    std::function<void(const QByteArray&)> apply_;
};

}  // namespace mh::ui
