// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "makehuman/foundation/SliderSpec.h"

#include <QHash>
#include <QList>
#include <QWidget>

#include <span>

class QLabel;
class QSlider;
class QTabWidget;

namespace mh::ui {

/// The modelling panel: one tab per task view, sections of sliders inside.
///
/// Takes `foundation::TaskViewSpec` rather than the modifier objects, so this
/// module never sees the AGPL core -- the app resolves the modifiers and passes
/// plain data down, the same bridge `RenderView` provides for geometry.
class ModifierPanel : public QWidget {
    Q_OBJECT

public:
    explicit ModifierPanel(std::span<const foundation::TaskViewSpec> views,
                           QWidget* parent = nullptr);

    /// Moves a slider without emitting valueChanged.
    ///
    /// Signals are blocked deliberately: this exists so a value loaded from a
    /// file can be shown, and re-emitting would feed it straight back to the
    /// thing that set it.
    void setValue(const QString& id, float value);

    [[nodiscard]] float value(const QString& id) const;

    /// Every slider back to its spec default, emitting one change each.
    void resetAll();

    /// Shows only sliders whose label or id contains @p text, and hides sections
    /// and tabs left with nothing -- a tab that scrolls an empty page is worse
    /// than no tab. Empty text shows everything.
    void filter(const QString& text);

    [[nodiscard]] int sliderCount() const;
    [[nodiscard]] int visibleSliderCount() const;
    [[nodiscard]] QTabWidget* tabs() const;

signals:
    void valueChanged(const QString& id, float value);

    /// A drag finished, or a keyboard/wheel/page step settled. Undo uses this
    /// to close the merge group: without it two deliberate nudges of the same
    /// slider would collapse into one undo step.
    ///
    /// **Emitted after the corresponding valueChanged**, not before.
    /// `QAbstractSlider::actionTriggered` fires *before* the value lands, so
    /// closing the group there closed it one edit too early and the next drag
    /// merged into the keyboard step.
    void editingFinished();

    /// Brackets resetAll(): true before the first change, false after the last.
    /// The app turns that into one undo step instead of one per slider -- a
    /// Reset used to cost up to 291 presses of Ctrl+Z to undo.
    void resetInProgress(bool active);

private:
    /// One labelled slider. Held by value in a list that is filled during
    /// construction and never touched again, which is what makes the plain
    /// index captured by each slider's handler safe.
    struct Row {
        QWidget* container{};
        QSlider* slider{};
        QLabel* readout{};
        foundation::SliderSpec spec;
    };

    QTabWidget* tabs_{};
    QList<Row> rows_;
    QHash<QString, int> byId_;
    /// Section bodies, so one left with nothing showing can be hidden.
    QList<QWidget*> sections_;
    /// Which tab each section belongs to, for hiding an emptied tab.
    QList<int> sectionTab_;
    /// Set by actionTriggered, consumed by the valueChanged handler. A drag in
    /// progress (SliderMove) does not close the group; anything else does.
    bool endsEdit_{false};
};

}  // namespace mh::ui
