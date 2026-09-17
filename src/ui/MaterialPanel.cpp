// SPDX-License-Identifier: Apache-2.0
#include "makehuman/ui/MaterialPanel.h"

#include <QCheckBox>
#include <QColor>
#include <QColorDialog>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QToolButton>
#include <QVBoxLayout>
#include <QWidget>

namespace mh::ui {

namespace {

/// Widgets are found by object name rather than kept in a side table -- QObject
/// already indexes its children, and a second index is a second thing that can
/// disagree with the widget tree. The same reasoning as `AssetPanel`.
QString editorName(const QString& id) {
    return QStringLiteral("material:") + id;
}

/// The format's booleans, as `saveMaterial` writes them. `_readbool` accepts
/// these case-insensitively (`material.py:357`), and matching the writer means a
/// checkbox and a saved file say the same word.
QString boolText(bool on) {
    return on ? QStringLiteral("True") : QStringLiteral("False");
}

bool boolValue(const QString& text) {
    const QString v = text.trimmed().toLower();
    return v == QLatin1String("yes") || v == QLatin1String("enabled") || v == QLatin1String("true");
}

/// "0.8 0.1 0.1" -> a colour, for seeding the picker. A row whose text is not
/// three numbers simply opens the picker on black rather than refusing: the
/// text is the truth and the picker is a convenience over it.
QColor colourOf(const QString& text) {
    const QStringList parts = text.split(QLatin1Char(' '), Qt::SkipEmptyParts);
    if (parts.size() < 3) return QColor(Qt::black);
    QColor c;
    // Qt6's setRgbF/redF are float, so the conversion is done here and once
    // rather than left implicit -- -Wdouble-promotion rejects it either way.
    const auto channel = [](const QString& t) {
        return qBound(0.0F, static_cast<float>(t.toDouble()), 1.0F);
    };
    c.setRgbF(channel(parts[0]), channel(parts[1]), channel(parts[2]));
    return c;
}

/// Back to the format's spelling. Six significant digits, trailing zeros gone,
/// which is what the parser reads and close enough to the writer's shortest
/// round-trip form that a picked colour survives a save unchanged.
QString colourText(const QColor& c) {
    const auto n = [](float v) { return QString::number(static_cast<double>(v), 'g', 6); };
    return n(c.redF()) + QLatin1Char(' ') + n(c.greenF()) + QLatin1Char(' ') + n(c.blueF());
}

}  // namespace

MaterialPanel::MaterialPanel(std::span<const foundation::MaterialProperty> properties,
                             QWidget* parent)
    : QWidget(parent) {
    setObjectName(QStringLiteral("panel.material"));

    // Twenty-nine rows do not fit a panel, and a form that clips its last rows
    // is a material editor missing its name field.
    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);

    auto* scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    outer->addWidget(scroll);

    auto* page = new QWidget(scroll);
    auto* form = new QFormLayout(page);
    form->setContentsMargins(8, 8, 8, 8);
    form->setSpacing(6);
    // Long texture paths otherwise widen the panel without limit.
    form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);

    using Kind = foundation::MaterialProperty::Kind;

    for (const foundation::MaterialProperty& p : properties) {
        const QString id    = QString::fromStdString(p.id);
        const QString label = QString::fromStdString(p.label);
        const QString value = QString::fromStdString(p.value);

        if (p.kind == Kind::Flag) {
            auto* box = new QCheckBox(page);
            box->setObjectName(editorName(id));
            box->setAccessibleName(label);
            box->setChecked(boolValue(value));
            connect(box, &QCheckBox::toggled, this,
                    [this, id](bool on) { emit edited(id + QLatin1Char('=') + boolText(on)); });
            form->addRow(label, box);
            continue;
        }

        auto* field = new QLineEdit(value, page);
        field->setObjectName(editorName(id));
        field->setAccessibleName(label);
        if (p.kind == Kind::Texture) {
            field->setPlaceholderText(tr("none"));
            // Said in the field rather than a tooltip: clearing a texture is an
            // ordinary edit and the way to do it should not be a secret.
            field->setToolTip(tr("Path to the image. Leave empty to remove the texture."));
        }
        // editingFinished, not textChanged: a per-keystroke signal would apply
        // "0", "0.", "0.3" as three separate edits on the way to 0.31, and each
        // one reaches the viewport and every exporter.
        connect(field, &QLineEdit::editingFinished, this,
                [this, id, field] { emit edited(id + QLatin1Char('=') + field->text()); });

        if (p.kind != Kind::Colour) {
            form->addRow(label, field);
            continue;
        }

        // A colour gets a picker beside its text. The TEXT stays the value --
        // the picker writes into it and then goes through the same signal, so
        // there is one path out of this panel rather than two.
        auto* row    = new QWidget(page);
        auto* across = new QHBoxLayout(row);
        across->setContentsMargins(0, 0, 0, 0);
        across->setSpacing(4);
        across->addWidget(field, 1);

        auto* pick = new QToolButton(row);
        pick->setObjectName(QStringLiteral("material.pick:") + id);
        pick->setText(QStringLiteral("…"));
        pick->setAccessibleName(tr("Pick %1 colour").arg(label));
        connect(pick, &QToolButton::clicked, this, [this, id, field] {
            const QColor chosen = QColorDialog::getColor(colourOf(field->text()), this);
            if (!chosen.isValid()) return;  // cancelled
            field->setText(colourText(chosen));
            emit edited(id + QLatin1Char('=') + field->text());
        });
        across->addWidget(pick, 0);

        form->addRow(label, row);
    }

    scroll->setWidget(page);
}

void MaterialPanel::setValue(const QString& id, const QString& value) {
    const QString name = editorName(id);
    if (auto* box = findChild<QCheckBox*>(name); box != nullptr) {
        const QSignalBlocker block(box);
        box->setChecked(boolValue(value));
        return;
    }
    if (auto* field = findChild<QLineEdit*>(name); field != nullptr) {
        const QSignalBlocker block(field);
        field->setText(value);
    }
}

QString MaterialPanel::value(const QString& id) const {
    const QString name = editorName(id);
    if (const auto* box = findChild<const QCheckBox*>(name); box != nullptr) {
        return boolText(box->isChecked());
    }
    if (const auto* field = findChild<const QLineEdit*>(name); field != nullptr) {
        return field->text();
    }
    return {};
}

int MaterialPanel::rowCount() const {
    // Counted from the widget tree for the same reason lookups go through it:
    // a stored count is a second source of truth that can fall out of step.
    return static_cast<int>(findChildren<QLineEdit*>().size() + findChildren<QCheckBox*>().size());
}

}  // namespace mh::ui
