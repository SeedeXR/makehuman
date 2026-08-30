// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <QString>
#include <QStringList>

namespace mh::ui {

/// Which task-view categories exist, and in what order.
///
/// This replaces the reference's filename ordering. There, a plugin's category
/// and its position both fall out of its file name -- `0_modeling_0_modifiers.py`
/// sorts before `0_modeling_1_bodyshapes.py` because `loadPlugins` does
/// `sorted(pluginsToLoad, key=lambda plugin: plugin[0])` (`core/mhmain.py:562`).
/// That works until someone needs to insert a view between two others or rename
/// a file, and it makes the order invisible from the code defining the view.
///
/// **Categories keep registration order, not alphabetical order.** That is the
/// point: "Modelling" precedes "Materials" because it is registered first.
///
/// Deliberately minimal. An earlier draft carried a per-task rank, an icon name
/// and a `tasks(category)` accessor -- none of which had a single production
/// reader, only tests. They come back with the first view that needs them.
class TaskRegistry {
public:
    /// @return false if @p category is already registered, ignoring case,
    ///         leaving the registry untouched. Case matters because the dock
    ///         object name is the lower-cased category and `saveState` keys on
    ///         it: two categories differing only in case would share one dock.
    [[nodiscard]] bool add(QString category);

    /// Categories in registration order.
    [[nodiscard]] QStringList categories() const;

private:
    QStringList categories_;
};

}  // namespace mh::ui
