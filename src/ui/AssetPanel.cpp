// SPDX-License-Identifier: Apache-2.0
#include "makehuman/ui/AssetPanel.h"
#include <algorithm>
#include <cctype>
#include <string>

#include <QCheckBox>
#include <QComboBox>
#include <QLabel>
#include <QVBoxLayout>

namespace mh::ui {

QString prettyAssetName(std::string_view stem, std::string_view prefix) {
    std::string out(stem);
    if (out.starts_with(prefix)) out = out.substr(prefix.size());
    std::ranges::replace(out, '_', ' ');
    if (!out.empty()) {
        out[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(out[0])));
    }
    return QString::fromStdString(out);
}

namespace {

/// Each picker is found by object name rather than kept in a side table --
/// QObject already indexes its children, and a second index is a second thing
/// that can disagree with the widget tree.
QString pickerName(const QString& group) {
    return QStringLiteral("assets:") + group;
}

}  // namespace

AssetPanel::AssetPanel(std::span<const foundation::AssetGroup> groups, QWidget* parent)
    : QWidget(parent) {
    setObjectName(QStringLiteral("panel.assets"));

    auto* column = new QVBoxLayout(this);
    column->setContentsMargins(8, 8, 8, 8);
    column->setSpacing(6);

    for (const foundation::AssetGroup& group : groups) {
        const QString name = QString::fromStdString(group.name);

        auto* heading = new QLabel(name, this);
        heading->setObjectName(QStringLiteral("assets.group"));
        column->addWidget(heading);

        auto* picker = new QComboBox(this);
        picker->setObjectName(pickerName(name));
        // The heading above it is a separate widget, so without this the combo
        // announces only its current value with no indication of what it sets.
        picker->setAccessibleName(name);
        for (const foundation::AssetChoice& c : group.choices) {
            // The id rides along as user data so the visible label stays free to
            // change -- and to be translated -- without breaking selection.
            picker->addItem(QString::fromStdString(c.label), QString::fromStdString(c.id));
        }
        if (group.selected >= 0 && group.selected < picker->count()) {
            picker->setCurrentIndex(group.selected);
        }
        connect(picker, &QComboBox::currentIndexChanged, this, [this, picker, name](int index) {
            if (index < 0) return;
            syncToggle(name);
            emit chosen(name, picker->itemData(index).toString());
        });
        column->addWidget(picker);

        if (!group.toggle) continue;

        // A checkbox for a slot whose real question is yes-or-no. It owns NO
        // state: `syncToggle` derives what it shows from the picker, and every
        // path that can move the picker calls it. That is the whole design --
        // a tick that remembered its own answer would be a second store, free
        // to disagree with the mesh actually worn.
        auto* box = new QCheckBox(tr("Show %1").arg(name), this);
        box->setObjectName(toggleName(name));
        box->setAccessibleName(tr("Show %1").arg(name));
        column->addWidget(box);
        connect(box, &QCheckBox::toggled, this, [picker](bool on) {
            // Index 1 is the first real choice; 0 is None by construction
            // above. A group that asked for a toggle but shipped no wearable
            // choice would leave the tick inert rather than crash.
            if (on && picker->count() < 2) return;
            // Setting the index makes the picker emit, which is what reaches
            // `syncToggle` and `chosen`. One path, not two.
            picker->setCurrentIndex(on ? 1 : 0);
        });
        syncToggle(name);
    }
    column->addStretch(1);
}

void AssetPanel::setChoice(const QString& group, const QString& id) {
    auto* picker = findChild<QComboBox*>(pickerName(group));
    if (picker == nullptr) return;
    const int index = picker->findData(id);
    if (index < 0) return;
    const QSignalBlocker block(picker);
    picker->setCurrentIndex(index);
    // The blocker above is exactly why this is here: with the picker's signals
    // suppressed, a tick wired only to `currentIndexChanged` would keep showing
    // the previous answer. This is the drift, closed at its source.
    syncToggle(group);
}

QString AssetPanel::toggleName(const QString& group) {
    return QStringLiteral("assets.toggle:") + group;
}

void AssetPanel::syncToggle(const QString& group) {
    auto* box = findChild<QCheckBox*>(toggleName(group));
    if (box == nullptr) return;
    const auto* picker = findChild<const QComboBox*>(pickerName(group));
    if (picker == nullptr) return;
    const QSignalBlocker block(box);
    box->setChecked(picker->currentIndex() > 0);
}

QString AssetPanel::choice(const QString& group) const {
    const auto* picker = findChild<const QComboBox*>(pickerName(group));
    if (picker == nullptr || picker->currentIndex() < 0) return {};
    return picker->currentData().toString();
}

}  // namespace mh::ui
