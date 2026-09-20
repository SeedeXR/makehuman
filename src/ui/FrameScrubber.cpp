// SPDX-License-Identifier: Apache-2.0
#include "makehuman/ui/FrameScrubber.h"

#include <QAbstractButton>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QSlider>
#include <QStyle>
#include <QToolButton>
#include <QVBoxLayout>

#include <algorithm>

namespace mh::ui {
namespace {

/// One transport button. The icons are the platform style's own media glyphs,
/// which is what the reference uses too (`QApplication.style().standardIcon`,
/// `3_libraries_animation.py:56`) -- nothing to draw, and it follows the
/// desktop theme.
QToolButton* transport(QWidget* parent, const QString& name, QStyle::StandardPixmap glyph,
                       const QString& tip) {
    auto* button = new QToolButton(parent);
    button->setObjectName(name);
    button->setIcon(parent->style()->standardIcon(glyph));
    button->setToolTip(tip);
    // The tooltip is a hover affordance; a screen reader needs the same words
    // without one, and an icon-only button otherwise announces nothing.
    button->setAccessibleName(tip);
    return button;
}

}  // namespace

FrameScrubber::FrameScrubber(QWidget* parent) : QWidget(parent) {
    setObjectName(QStringLiteral("panel.scrubber"));

    auto* column = new QVBoxLayout(this);
    column->setContentsMargins(8, 8, 8, 8);
    column->setSpacing(6);

    slider_ = new QSlider(Qt::Horizontal, this);
    slider_->setObjectName(QStringLiteral("scrub.slider"));
    slider_->setAccessibleName(tr("Frame"));
    slider_->setMinimum(0);
    slider_->setMaximum(0);
    column->addWidget(slider_);

    frameLabel_ = new QLabel(this);
    frameLabel_->setObjectName(QStringLiteral("scrub.frame"));
    column->addWidget(frameLabel_);

    auto* row = new QHBoxLayout;
    row->setSpacing(4);
    row->addWidget(transport(this, QStringLiteral("scrub.first"), QStyle::SP_MediaSkipBackward,
                             tr("Set to first frame")));
    row->addWidget(transport(this, QStringLiteral("scrub.back"), QStyle::SP_MediaSeekBackward,
                             tr("One frame backward")));
    row->addWidget(transport(this, QStringLiteral("scrub.forward"), QStyle::SP_MediaSeekForward,
                             tr("One frame forward")));
    row->addWidget(transport(this, QStringLiteral("scrub.last"), QStyle::SP_MediaSkipForward,
                             tr("Set to last frame")));
    row->addStretch(1);
    column->addLayout(row);

    status_ = new QLabel(this);
    status_->setObjectName(QStringLiteral("scrub.status"));
    status_->setWordWrap(true);
    column->addWidget(status_);
    column->addStretch(1);

    // Every control moves the SLIDER and nothing else, so the slider's own
    // handler below is the single place that updates the label and decides
    // whether to ask for a reload. Buttons that set a frame directly would be
    // four more sites to keep in step -- which is the drift this file's
    // siblings keep finding.
    const auto step = [this](int delta) {
        return [this, delta] { slider_->setValue(slider_->value() + delta); };
    };
    connect(findChild<QToolButton*>(QStringLiteral("scrub.first")), &QAbstractButton::clicked, this,
            [this] { slider_->setValue(0); });
    connect(findChild<QToolButton*>(QStringLiteral("scrub.back")), &QAbstractButton::clicked, this,
            step(-1));
    connect(findChild<QToolButton*>(QStringLiteral("scrub.forward")), &QAbstractButton::clicked,
            this, step(1));
    connect(findChild<QToolButton*>(QStringLiteral("scrub.last")), &QAbstractButton::clicked, this,
            [this] { slider_->setValue(slider_->maximum()); });

    connect(slider_, &QSlider::valueChanged, this, [this](int value) {
        frameLabel_->setText(tr("Frame: %1").arg(value));
        // Mid-drag the label follows the thumb and the body does not: one
        // reload per pixel would re-read the .bvh and refit the skeleton.
        // `sliderReleased` below asks for the one that matters.
        if (quiet_ || slider_->isSliderDown()) return;
        emit frameChosen(value);
    });
    connect(slider_, &QSlider::sliderReleased, this, [this] {
        // A click on the thumb that moves nothing releases too. Without this
        // the model would reload to the frame it is already showing.
        if (quiet_ || slider_->value() == pressedFrame_) return;
        emit frameChosen(slider_->value());
    });
    connect(slider_, &QSlider::sliderPressed, this, [this] { pressedFrame_ = slider_->value(); });

    setAnimation(0);
}

void FrameScrubber::setAnimation(int frameCount) {
    const int frames = std::max(0, frameCount);
    const bool live  = frames > 1;
    // Read BEFORE setMaximum, which clamps the value itself -- and clamping is
    // exactly what this must not do. A 31-frame animation replaced by a
    // 14-frame one goes to frame 0, not to frame 13.
    const int want = (live && slider_->value() <= frames - 1) ? slider_->value() : 0;

    quiet_ = true;
    slider_->setMaximum(live ? frames - 1 : 0);
    slider_->setValue(want);
    // setValue emits nothing when the value did not change, so the label is
    // written here rather than left to the handler.
    frameLabel_->setText(tr("Frame: %1").arg(want));
    quiet_ = false;

    slider_->setEnabled(live);
    for (QAbstractButton* button : findChildren<QAbstractButton*>())
        button->setEnabled(live);

    status_->setText(live          ? tr("%1 frames available").arg(frames)
                     : frames == 1 ? tr("Only one frame available")
                                   : tr("No animation loaded"));
}

int FrameScrubber::frame() const {
    return slider_->value();
}

void FrameScrubber::setFrame(int frame) {
    if (frame < slider_->minimum() || frame > slider_->maximum()) return;
    quiet_ = true;
    slider_->setValue(frame);
    frameLabel_->setText(tr("Frame: %1").arg(frame));
    quiet_ = false;
}

QString FrameScrubber::status() const {
    return status_->text();
}

bool FrameScrubber::isScrubbable() const {
    return slider_->isEnabled();
}

}  // namespace mh::ui
