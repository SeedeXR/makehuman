// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "makehuman/foundation/SliderSpec.h"

#include <QWidget>

#include <span>

namespace mh::ui {

/// A column of labelled pickers, one per asset group.
///
/// Deliberately not a thumbnail grid: with three litspheres and two poses a grid
/// would be mostly empty space. The shape can change when there are enough
/// assets to justify it; the signal will not.
class AssetPanel : public QWidget {
    Q_OBJECT

public:
    explicit AssetPanel(std::span<const foundation::AssetGroup> groups, QWidget* parent = nullptr);

    /// The selected id, or empty when the group is unknown or has no selection.
    [[nodiscard]] QString choice(const QString& group) const;

signals:
    void chosen(const QString& group, const QString& id);
};

}  // namespace mh::ui
