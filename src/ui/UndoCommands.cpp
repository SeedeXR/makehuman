// SPDX-License-Identifier: Apache-2.0
#include "makehuman/ui/UndoCommands.h"

#include <QObject>

#include <stdexcept>

namespace mh::ui {

MultiValueChangeCommand::MultiValueChangeCommand(
    QString text, std::vector<Change> changes,
    std::function<void(const std::vector<std::pair<QString, float>>&)> apply)
    : changes_(std::move(changes)), apply_(std::move(apply)) {
    setText(text);
}

void MultiValueChangeCommand::applyDirection(bool forward) {
    if (!apply_) return;
    std::vector<std::pair<QString, float>> values;
    values.reserve(changes_.size());
    for (const Change& c : changes_) {
        values.emplace_back(c.key, forward ? c.to : c.from);
    }
    // ONE call. See the header: the whole point of this command is that the
    // caller rebuilds once rather than once per change.
    apply_(values);
}

void MultiValueChangeCommand::undo() {
    applyDirection(false);
}

void MultiValueChangeCommand::redo() {
    applyDirection(true);
}

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

LayoutChangeCommand::LayoutChangeCommand(const QString& name, QByteArray from, QByteArray to,
                                         std::function<void(const QByteArray&)> apply)
    : QUndoCommand(QObject::tr("Workspace: %1").arg(name)),
      from_(std::move(from)),
      to_(std::move(to)),
      apply_(std::move(apply)) {
    if (!apply_) throw std::invalid_argument("LayoutChangeCommand needs an apply callback");
}

void LayoutChangeCommand::undo() {
    apply_(from_);
}

void LayoutChangeCommand::redo() {
    apply_(to_);
}

}  // namespace mh::ui
