// SPDX-License-Identifier: Apache-2.0
#include "makehuman/ui/UndoCommands.h"

namespace mh::ui {

ValueChangeCommand::ValueChangeCommand(QString key, float from, float to, int mergeId,
                                       std::function<void(const QString&, float)> apply)
    : key_(std::move(key)), from_(from), to_(to), mergeId_(mergeId), apply_(std::move(apply)) {
    setText(QObject::tr("Change %1").arg(key_));
}

void ValueChangeCommand::undo() {
    if (apply_) apply_(key_, from_);
}

void ValueChangeCommand::redo() {
    if (apply_) apply_(key_, to_);
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

}  // namespace mh::ui
