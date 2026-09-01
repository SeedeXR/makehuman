// SPDX-License-Identifier: Apache-2.0
#include "makehuman/ui/ModifierPanel.h"

#include "makehuman/ui/Theme.h"

#include <QAbstractSlider>
#include <QAccessible>
#include <QAccessibleWidget>
#include <QHBoxLayout>
#include <QHash>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QSlider>
#include <QTabWidget>
#include <QVBoxLayout>

#include <cmath>

namespace mh::ui {
namespace {

/// QSlider is integral, so a float range is mapped onto fixed steps. 1000 gives
/// 0.002 resolution over the widest range any modifier has ([-1,1]), which is
/// finer than the slider is wide in pixels -- and it makes the midpoint exact,
/// so a default of 0 round-trips rather than landing on 0.001.
constexpr int kSteps = 1000;

int toTick(const foundation::SliderSpec& s, float value) {
    const float span = s.maxValue - s.minValue;
    if (span <= 0.0F) return 0;
    const float t = (value - s.minValue) / span;
    return static_cast<int>(std::lround(static_cast<double>(t) * kSteps));
}

float fromTick(const foundation::SliderSpec& s, int tick) {
    const float t = static_cast<float>(tick) / static_cast<float>(kSteps);
    return s.minValue + t * (s.maxValue - s.minValue);
}

/// The slider's own range, stashed on the widget so the accessibility factory
/// below can find it. `QAccessible`'s factory is a free function with no
/// context, and these two floats are the whole of what it needs.
constexpr auto kMinProperty = "mh.sliderMin";
constexpr auto kMaxProperty = "mh.sliderMax";

/// What a screen reader is told a slider's value IS.
///
/// Measured before this existed: the Neck-circumference slider announced
/// **"500"** -- Qt's default for a QSlider is `QString::number(value())`, and
/// our sliders run 0..1000 ticks whatever the modifier's own range is. So a
/// screen-reader user heard a number with no meaning: 500 for a modifier
/// sitting at 0.00.
///
/// There is no widget-level API for this; `QAccessible::installFactory` is the
/// documented way.
class SliderAccessible : public QAccessibleWidget {
public:
    explicit SliderAccessible(QSlider* slider) : QAccessibleWidget(slider, QAccessible::Slider) {}

    QString text(QAccessible::Text t) const override {
        const auto* slider = qobject_cast<const QSlider*>(object());
        if (t != QAccessible::Value || slider == nullptr) return QAccessibleWidget::text(t);
        const float lo = slider->property(kMinProperty).toFloat();
        const float hi = slider->property(kMaxProperty).toFloat();
        const float v =
            lo + (static_cast<float>(slider->value()) / static_cast<float>(kSteps)) * (hi - lo);
        // The same two decimals the readout label shows, so what is heard and
        // what is seen are the same number.
        return QString::number(static_cast<double>(v), 'f', 2);
    }
};

/// Claims only the sliders this panel built -- the ones carrying the range
/// properties. Anything else falls through to Qt's own interface.
QAccessibleInterface* sliderAccessibleFactory(const QString& className, QObject* object) {
    if (className != QLatin1String("QSlider")) return nullptr;
    if (!object->property(kMinProperty).isValid()) return nullptr;
    return new SliderAccessible(qobject_cast<QSlider*>(object));
}

}  // namespace

ModifierPanel::ModifierPanel(std::span<const foundation::TaskViewSpec> views, QWidget* parent)
    : QWidget(parent) {
    setObjectName(QStringLiteral("panel.modifiers"));

    // Once per process. QAccessible keeps a list and would call a duplicate
    // twice; the first non-null wins, so a second registration is harmless but
    // pointless.
    [[maybe_unused]] static const bool installed = [] {
        QAccessible::installFactory(&sliderAccessibleFactory);
        return true;
    }();

    auto* column = new QVBoxLayout(this);
    column->setContentsMargins(8, 8, 8, 8);
    column->setSpacing(8);

    auto* search = new QLineEdit(this);
    search->setObjectName(QStringLiteral("modifiers.search"));
    search->setPlaceholderText(tr("Search sliders…"));
    // design.md 9: set explicitly rather than left to inference. A QLineEdit
    // with only a placeholder reads as "text field" with no name at all.
    search->setAccessibleName(tr("Search sliders"));
    search->setClearButtonEnabled(true);
    connect(search, &QLineEdit::textChanged, this, &ModifierPanel::filter);

    auto* reset = new QPushButton(tr("Reset"), this);
    reset->setObjectName(QStringLiteral("modifiers.reset"));
    reset->setToolTip(tr("Every slider back to its default"));
    connect(reset, &QPushButton::clicked, this, &ModifierPanel::resetAll);

    auto* top = new QHBoxLayout;
    top->setContentsMargins(0, 0, 0, 0);
    top->addWidget(search, 1);
    top->addWidget(reset);
    column->addLayout(top);

    tabs_ = new QTabWidget(this);
    tabs_->setObjectName(QStringLiteral("modifiers.tabs"));
    column->addWidget(tabs_, 1);

    for (const foundation::TaskViewSpec& view : views) {
        auto* page       = new QWidget;
        auto* pageColumn = new QVBoxLayout(page);
        pageColumn->setContentsMargins(4, 4, 4, 4);
        pageColumn->setSpacing(10);

        for (const foundation::SliderSection& section : view.sections) {
            auto* box       = new QWidget(page);
            auto* boxColumn = new QVBoxLayout(box);
            boxColumn->setContentsMargins(0, 0, 0, 0);
            boxColumn->setSpacing(4);

            auto* heading = new QLabel(QString::fromStdString(section.name), box);
            heading->setObjectName(QStringLiteral("modifiers.section"));
            boxColumn->addWidget(heading);

            for (const foundation::SliderSpec& spec : section.sliders) {
                auto* rowWidget = new QWidget(box);
                auto* rowColumn = new QVBoxLayout(rowWidget);
                rowColumn->setContentsMargins(0, 0, 0, 0);
                rowColumn->setSpacing(1);

                auto* caption = new QLabel(rowWidget);
                caption->setObjectName(QStringLiteral("modifiers.caption"));
                caption->setText(QString::fromStdString(spec.label));
                // Measured at 200% text: the longest shipped caption wants
                // 263 px and the dock is 380, so this is not fixing a clipping
                // bug. It drops that label's MINIMUM from 263 px to 72, so a
                // user who drags the dock narrower keeps a usable panel instead
                // of one caption holding it open.
                caption->setWordWrap(true);

                auto* readout = new QLabel(rowWidget);
                readout->setObjectName(QStringLiteral("modifiers.readout"));
                readout->setAlignment(Qt::AlignRight);

                auto* head    = new QWidget(rowWidget);
                auto* headRow = new QHBoxLayout(head);
                headRow->setContentsMargins(0, 0, 0, 0);
                headRow->addWidget(caption);
                headRow->addStretch(1);
                headRow->addWidget(readout);
                rowColumn->addWidget(head);

                auto* slider = new QSlider(Qt::Horizontal, rowWidget);
                slider->setObjectName(QStringLiteral("slider:") + QString::fromStdString(spec.id));
                slider->setRange(0, kSteps);
                slider->setValue(toTick(spec, spec.defaultValue));
                slider->setToolTip(QString::fromStdString(spec.id));
                // Named by what it does and where it lives, so a screen reader
                // announces "Age, head shape" rather than "horizontal slider".
                // Section folded into the NAME rather than a separate
                // description that repeats it: a reader would otherwise say
                // "Oval ... Oval, in head shape".
                slider->setAccessibleName(tr("%1, %2").arg(QString::fromStdString(spec.label),
                                                           QString::fromStdString(section.name)));
                rowColumn->addWidget(slider);

                // What the accessibility factory reads to announce a value
                // rather than a raw tick.
                slider->setProperty(kMinProperty, spec.minValue);
                slider->setProperty(kMaxProperty, spec.maxValue);

                Row row;
                row.container   = rowWidget;
                row.slider      = slider;
                row.readout     = readout;
                row.spec        = spec;
                const int index = static_cast<int>(rows_.size());
                rows_.append(row);
                byId_.insert(QString::fromStdString(spec.id), index);

                readout->setText(QString::number(static_cast<double>(spec.defaultValue), 'f', 2));
                connect(slider, &QSlider::sliderReleased, this, &ModifierPanel::editingFinished);
                // Keyboard and wheel changes never emit sliderReleased, so they
                // would merge with whatever came next without this.
                connect(slider, &QSlider::actionTriggered, this, [this](int action) {
                    // actionTriggered fires BEFORE the value lands, so this only
                    // records the intent; the handler below emits afterwards.
                    endsEdit_ = action != QAbstractSlider::SliderMove;
                });
                connect(slider, &QSlider::valueChanged, this, [this, index](int tick) {
                    Row& r        = rows_[index];
                    const float v = fromTick(r.spec, tick);
                    r.readout->setText(QString::number(static_cast<double>(v), 'f', 2));
                    emit valueChanged(QString::fromStdString(r.spec.id), v);
                    if (endsEdit_) {
                        endsEdit_ = false;
                        emit editingFinished();
                    }
                });

                boxColumn->addWidget(rowWidget);
            }

            sections_.append(box);
            sectionTab_.append(tabs_->count());  // the tab this page will become
            pageColumn->addWidget(box);
        }
        pageColumn->addStretch(1);

        // 291 sliders do not fit on any screen; each tab scrolls.
        auto* scroll = new QScrollArea(tabs_);
        scroll->setWidgetResizable(true);
        scroll->setWidget(page);
        scroll->setFrameShape(QFrame::NoFrame);
        tabs_->addTab(scroll, QString::fromStdString(view.name));
    }
}

QTabWidget* ModifierPanel::tabs() const {
    return tabs_;
}

int ModifierPanel::sliderCount() const {
    return static_cast<int>(rows_.size());
}

int ModifierPanel::visibleSliderCount() const {
    int n = 0;
    for (const Row& r : rows_) {
        if (!r.container->isHidden()) ++n;
    }
    return n;
}

float ModifierPanel::value(const QString& id) const {
    const auto at = byId_.constFind(id);
    if (at == byId_.constEnd()) return 0.0F;
    const Row& r = rows_[*at];
    return fromTick(r.spec, r.slider->value());
}

void ModifierPanel::setValue(const QString& id, float value) {
    const auto at = byId_.constFind(id);
    if (at == byId_.constEnd()) return;
    Row& r = rows_[*at];

    const QSignalBlocker block(r.slider);
    r.slider->setValue(toTick(r.spec, value));
    // The readout is updated by hand because blocking the signal also skips the
    // handler that normally does it.
    r.readout->setText(
        QString::number(static_cast<double>(fromTick(r.spec, r.slider->value())), 'f', 2));
}

void ModifierPanel::resetAll() {
    // Bracketed so the app can make this one undo step. Without it every
    // non-default slider pushed its own command -- up to 291 of them -- and
    // the next drag merged into the last one.
    emit resetInProgress(true);
    for (Row& r : rows_)
        r.slider->setValue(toTick(r.spec, r.spec.defaultValue));
    emit resetInProgress(false);
}

void ModifierPanel::filter(const QString& text) {
    const QString needle = text.trimmed();

    // Counted in the same pass that decides each row, rather than rescanning
    // every row once per section.
    QHash<QWidget*, int> showingPerSection;
    for (Row& r : rows_) {
        const bool show =
            needle.isEmpty() ||
            QString::fromStdString(r.spec.label).contains(needle, Qt::CaseInsensitive) ||
            QString::fromStdString(r.spec.id).contains(needle, Qt::CaseInsensitive);
        r.container->setVisible(show);
        if (show) ++showingPerSection[r.container->parentWidget()];
    }

    // A section heading with no sliders under it is noise, not structure; a tab
    // with no sections left is an empty page the user has to click to discover.
    QHash<int, int> showingPerTab;
    for (int i = 0; i < sections_.size(); ++i) {
        const int n = showingPerSection.value(sections_[i], 0);
        sections_[i]->setVisible(n > 0);
        showingPerTab[sectionTab_[i]] += n;
    }
    for (int tab = 0; tab < tabs_->count(); ++tab) {
        tabs_->setTabVisible(tab, showingPerTab.value(tab, 0) > 0);
    }
}

}  // namespace mh::ui
