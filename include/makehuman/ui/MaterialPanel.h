// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "makehuman/foundation/MaterialProperty.h"

#include <QWidget>

#include <span>

namespace mh::ui {

/// The material editor: one labelled row per editable property.
///
/// Takes `foundation::MaterialProperty` rather than the material itself, so
/// this module never sees the AGPL core -- the app reads the `.mhmat` and
/// passes plain data down, the same bridge `ModifierPanel` has for modifiers.
///
/// **Every edit leaves as `"id=value"`**, the exact string
/// `mh::core::setMaterialProperty` takes. The panel therefore cannot express an
/// edit `--set-material` cannot, and the two cannot disagree about what one
/// means: there is one parser and it is in the core.
class MaterialPanel : public QWidget {
    Q_OBJECT

public:
    explicit MaterialPanel(std::span<const foundation::MaterialProperty> properties,
                           QWidget* parent = nullptr);

    /// Shows @p value in @p id's row **without emitting**, so a material loaded
    /// from a file can be displayed without the panel reporting it as an edit.
    ///
    /// An unknown id is ignored: a row showing nothing is worse than one
    /// showing the previous value.
    void setValue(const QString& id, const QString& value);

    /// What the row currently shows, or empty when @p id is not a row.
    [[nodiscard]] QString value(const QString& id) const;

    [[nodiscard]] int rowCount() const;

signals:
    /// `"<id>=<value>"`, ready for `mh::core::setMaterialProperty`.
    void edited(const QString& spec);
};

}  // namespace mh::ui
