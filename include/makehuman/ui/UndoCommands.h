// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <QString>
#include <QUndoCommand>

#include <functional>

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

}  // namespace mh::ui
