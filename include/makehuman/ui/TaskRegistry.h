// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <QHash>
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
    /// Registers a category.
    ///
    /// @param category the **stable id**. It is what `dockObjectName` lowercases
    ///        and what `QMainWindow::saveState` therefore keys on, so it is a
    ///        PERSISTED key: changing it silently drops that panel out of every
    ///        workspace a user has already saved. Never rename one.
    /// @param title what the user reads, empty meaning "the id". Separate for
    ///        exactly that reason -- the second dock was labelled "Materials"
    ///        while holding Skin, Pose, Eyes, Skin material and Skeleton, and
    ///        relabelling it to "Assets" cost nothing once the two stopped
    ///        being one string.
    ///
    /// @return false if @p category is already registered, ignoring case,
    ///         leaving the registry untouched. Case matters for the same
    ///         reason: two ids differing only in case would share one dock and
    ///         one saved-state key.
    [[nodiscard]] bool add(QString category, QString title = {});

    /// Category **ids** in registration order.
    [[nodiscard]] QStringList categories() const;

    /// The display title for @p category, or the id itself when none was given
    /// or the id is unknown. A blank dock title is worse than a slightly wrong
    /// one, so this never returns empty for a non-empty id.
    [[nodiscard]] QString title(const QString& category) const;

private:
    QStringList categories_;
    QHash<QString, QString> titles_;
};

}  // namespace mh::ui
