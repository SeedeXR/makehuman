// SPDX-License-Identifier: Apache-2.0
#include "makehuman/ui/UndoCommands.h"

namespace mh::ui {

ValueChangeCommand::ValueChangeCommand(QString key, float from, float to, int mergeId,
                                       std::function<void(const QString&, float)> apply)
    : key_(std::move(key)), from_(from), to_(to), mergeId_(mergeId), apply_(std::move(apply)) {
    setText(QObject::tr("Change %1").arg(key_));
}

void ValueChangeCommand::undo() {
    // Unguarded on purpose: a null callback is a programming error, and an undo
    // that silently does nothing is harder to diagnose than one that throws.
    apply_(key_, from_);
}

void ValueChangeCommand::redo() {
    apply_(key_, to_);
}

int ValueChangeCommand::id() const {
    // QUndoStack only attempts a merge when the ids match and are not -1, so
    // the merge window is exactly one edit group. qHash mixes the key in, which
    // keeps two different sliders in the same group from merging into each
    // other -- mergeWith checks the key as well, so a hash collision is a
    // refused merge rather than a wrong one.
    return static_cast<int>(qHash(key_, static_cast<size_t>(mergeId_)) & 0x7fffffffU);
}

bool ValueChangeCommand::mergeWith(const QUndoCommand* other) {
    const auto* value = dynamic_cast<const ValueChangeCommand*>(other);
    if (value == nullptr || value->key_ != key_ || value->mergeId_ != mergeId_) return false;
    // Keep this command's `from` -- it is where the drag started -- and take the
    // newer `to`. Undo then goes all the way back in one step.
    to_ = value->to_;
    return true;
}

ChoiceChangeCommand::ChoiceChangeCommand(QString key, QString from, QString to, int mergeId,
                                         std::function<void(const QString&, const QString&)> apply)
    : key_(std::move(key)),
      from_(std::move(from)),
      to_(std::move(to)),
      mergeId_(mergeId),
      apply_(std::move(apply)) {
    setText(QObject::tr("Change %1").arg(key_));
}

int ChoiceChangeCommand::id() const {
    return mergeId_;
}

bool ChoiceChangeCommand::mergeWith(const QUndoCommand* other) {
    const auto* choice = dynamic_cast<const ChoiceChangeCommand*>(other);
    if (choice == nullptr || choice->key_ != key_ || choice->mergeId_ != mergeId_) return false;
    // Keep where the run started and take the newest destination, so one undo
    // returns to the skin the user had before they started comparing.
    to_ = choice->to_;
    return true;
}

void ChoiceChangeCommand::undo() {
    apply_(key_, from_);
}

void ChoiceChangeCommand::redo() {
    apply_(key_, to_);
}

}  // namespace mh::ui
