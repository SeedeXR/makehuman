// SPDX-License-Identifier: Apache-2.0
#include "makehuman/ui/AssetPanel.h"
#include <algorithm>
#include <cctype>
#include <string>

#include <QCheckBox>
#include <QComboBox>
#include <QLabel>
#include <QScrollArea>
#include <QTabWidget>
#include <QVBoxLayout>

#include <map>
#include <vector>

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

/// The tab order, taken from the reference rather than chosen.
///
/// `core/mhmain.py:523-527` creates its categories in exactly this sequence --
/// Modelling, Geometries, Materials, Pose/Animate, Rendering. Modelling is the
/// slider dock in this port, not an asset chooser, so the Assets panel carries
/// the remaining four in the reference's own order. Geometries leading is both
/// what upstream does and what the panel is most used for.
///
/// A category not listed here still gets a tab, appended after these. The
/// panel must never be the reason a chooser is unreachable.
const QStringList& categoryOrder() {
    static const QStringList order{QStringLiteral("Geometries"), QStringLiteral("Materials"),
                                   QStringLiteral("Pose/Animate"), QStringLiteral("Rendering")};
    return order;
}

/// The tab an unclassified group lands in.
///
/// Not "Other" as a silent dumping ground: a group reaching here means the
/// application forgot to categorise it, and the name is meant to be noticed in
/// a screenshot.
QString fallbackCategory() {
    return QStringLiteral("Uncategorised");
}

}  // namespace

AssetPanel::AssetPanel(std::span<const foundation::AssetGroup> groups, QWidget* parent)
    : QWidget(parent) {
    setObjectName(QStringLiteral("panel.assets"));

    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);

    auto* tabs = new QTabWidget(this);
    tabs->setObjectName(QStringLiteral("assets.tabs"));
    // Top, like every other tab bar in this application; Qt's default on macOS
    // is bottom and the mismatch reads as a different application.
    tabs->setTabPosition(QTabWidget::North);
    outer->addWidget(tabs);

    // One page per category, created on demand but ORDERED by the list above
    // rather than by first appearance -- otherwise the tab order would depend
    // on the order the application happens to build its groups in.
    std::map<QString, QVBoxLayout*> pages;
    const auto pageFor = [&](const QString& category) -> QVBoxLayout* {
        auto it = pages.find(category);
        if (it != pages.end()) return it->second;
        auto* page = new QWidget(tabs);
        page->setObjectName(QStringLiteral("assets.page:") + category);
        auto* inner = new QVBoxLayout(page);
        inner->setContentsMargins(8, 8, 8, 8);
        inner->setSpacing(6);
        // Scrolled, because a category is free to grow: Geometries already
        // holds seven groups and the dock is resizable down to a narrow strip.
        auto* scroll = new QScrollArea(tabs);
        scroll->setWidgetResizable(true);
        scroll->setFrameShape(QFrame::NoFrame);
        // A PANEL MUST NOT DICTATE THE WINDOW'S SIZE, and this pair is what
        // stops it.
        //
        // MEASURED on CI, which has a far smaller screen than this machine:
        // after these tabs landed the viewport was squeezed to 324 px wide and
        // its HEIGHT stopped being stable between runs -- 599, 600, 602 --
        // which failed every screenshot comparison that spans two app launches
        // (`sizes differ: 324x599 vs 324x602`). A scroll area reports the size
        // its CONTENT wants, so seven combo rows plus a tab bar pushed the dock
        // wider and taller than the screen could pay for, and a horizontal
        // scrollbar then appeared or did not depending on rounding, moving the
        // height by a pixel or three.
        //
        // So: never scroll sideways -- a chooser column has nothing to reveal
        // horizontally, it should simply narrow -- and let the area shrink
        // below its content, which is the whole point of putting it in a scroll
        // area. The content keeps its own size; only the window's obligation to
        // it is removed.
        scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        scroll->setSizeAdjustPolicy(QAbstractScrollArea::AdjustIgnored);
        scroll->setMinimumSize(0, 0);
        scroll->setWidget(page);
        tabs->addTab(scroll, category);
        pages.emplace(category, inner);
        return inner;
    };
    for (const QString& category : categoryOrder()) {
        const bool used = std::ranges::any_of(groups, [&](const foundation::AssetGroup& g) {
            return QString::fromStdString(g.category) == category;
        });
        // An EMPTY tab is worse than no tab: it reads as a broken panel. So a
        // category nothing uses is simply not created.
        if (used) pageFor(category);
    }

    for (const foundation::AssetGroup& group : groups) {
        const QString name = QString::fromStdString(group.name);
        QString category   = QString::fromStdString(group.category);
        if (category.isEmpty()) category = fallbackCategory();
        QVBoxLayout* column = pageFor(category);

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
    for (auto& [category, layout] : pages) {
        layout->addStretch(1);
    }
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
