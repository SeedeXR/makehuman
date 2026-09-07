// SPDX-License-Identifier: Apache-2.0
#include "makehuman/ui/ImageViewer.h"

#include "makehuman/ui/Theme.h"

#include <QFileDialog>
#include <QHBoxLayout>
#include <QImage>
#include <QLabel>
#include <QMouseEvent>
#include <QPixmap>
#include <QScrollArea>
#include <QScrollBar>
#include <QToolButton>
#include <QVBoxLayout>
#include <QWheelEvent>

#include <algorithm>
#include <cmath>

namespace mh::ui {
namespace {

/// One toolbar button, named so a test can find it and a stylesheet can reach
/// it. Icon-only, so the tooltip doubles as the accessible name -- otherwise a
/// screen reader announces nothing at all (design.md 9).
QToolButton* makeButton(const char* iconName, const QString& objectName, const QString& tip,
                        QWidget* parent) {
    auto* b = new QToolButton(parent);
    b->setObjectName(objectName);
    b->setIcon(theme::icon(iconName, theme::palette().textSecondary, 16));
    b->setAutoRaise(true);
    b->setToolTip(tip);
    b->setAccessibleName(tip);
    b->setFocusPolicy(Qt::TabFocus);
    return b;
}

/// One wheel notch. 1.25 rather than 2: doubling per notch crosses the whole
/// zoom range in five notches and overshoots whatever the user was aiming at.
constexpr double kZoomStep = 1.25;

}  // namespace

struct ImageViewer::Impl {
    QImage image;
    double zoom{1.0};
    /// Whether the current zoom came from fitToWindow. A resized window
    /// re-fits only while the user has not chosen a zoom of their own -- moving
    /// their view under them is worse than a stale fit.
    bool fitted{true};
    QScrollArea* area{nullptr};
    QLabel* canvas{nullptr};
    QLabel* readout{nullptr};
    QPoint dragFrom;
    bool dragging{false};
};

ImageViewer::ImageViewer(QWidget* parent) : QWidget(parent), d_(std::make_unique<Impl>()) {
    setObjectName(QStringLiteral("viewer.render"));
    setWindowTitle(tr("Render"));

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    auto* bar = new QHBoxLayout;
    bar->setContentsMargins(6, 4, 6, 4);
    bar->setSpacing(4);
    auto* out   = makeButton("zoom-out", QStringLiteral("viewer.zoomOut"), tr("Zoom out"), this);
    auto* in    = makeButton("zoom-in", QStringLiteral("viewer.zoomIn"), tr("Zoom in"), this);
    auto* fit   = makeButton("maximize-2", QStringLiteral("viewer.fit"), tr("Fit to window"), this);
    auto* save  = makeButton("save", QStringLiteral("viewer.save"), tr("Save image as…"), this);
    d_->readout = new QLabel(this);
    d_->readout->setObjectName(QStringLiteral("viewer.zoomLabel"));
    bar->addWidget(out);
    bar->addWidget(in);
    bar->addWidget(fit);
    bar->addWidget(d_->readout);
    bar->addStretch(1);
    bar->addWidget(save);
    root->addLayout(bar);

    d_->canvas = new QLabel;
    d_->canvas->setObjectName(QStringLiteral("viewer.canvas"));
    d_->canvas->setAlignment(Qt::AlignCenter);
    d_->area = new QScrollArea(this);
    d_->area->setObjectName(QStringLiteral("viewer.scroll"));
    d_->area->setWidget(d_->canvas);
    d_->area->setWidgetResizable(false);
    d_->area->setAlignment(Qt::AlignCenter);
    d_->area->viewport()->installEventFilter(this);
    root->addWidget(d_->area, 1);

    connect(out, &QToolButton::clicked, this, [this] { setZoom(d_->zoom / kZoomStep); });
    connect(in, &QToolButton::clicked, this, [this] { setZoom(d_->zoom * kZoomStep); });
    connect(fit, &QToolButton::clicked, this, [this] { fitToWindow(); });
    connect(save, &QToolButton::clicked, this, [this] {
        const QString path =
            QFileDialog::getSaveFileName(this, tr("Save render as"), {}, tr("PNG image (*.png)"));
        if (!path.isEmpty()) (void)saveAs(path);
    });
    resize(720, 560);
}

ImageViewer::~ImageViewer() = default;

double ImageViewer::fitScale(QSize image, QSize viewport) {
    if (image.width() <= 0 || image.height() <= 0) return 1.0;
    if (viewport.width() <= 0 || viewport.height() <= 0) return 1.0;
    const double byWidth  = static_cast<double>(viewport.width()) / image.width();
    const double byHeight = static_cast<double>(viewport.height()) / image.height();
    // The SMALLER ratio: the larger one fits one axis and crops the other.
    return std::min({byWidth, byHeight, 1.0});
}

void ImageViewer::setImage(QImage image) {
    d_->image = std::move(image);
    fitToWindow();
}

const QImage& ImageViewer::image() const {
    return d_->image;
}

bool ImageViewer::hasImage() const {
    return !d_->image.isNull();
}

bool ImageViewer::saveAs(const QString& path) const {
    // No guard for a null image: Qt already refuses one and writes nothing --
    // measured, by deleting the guard and watching the test still pass. The
    // test asserts the behaviour rather than the guard, so a Qt that started
    // writing an empty PNG would be caught.
    return d_->image.save(path);
}

double ImageViewer::zoom() const {
    return d_->zoom;
}

void ImageViewer::setZoom(double factor) {
    const double clamped = std::clamp(factor, kMinZoom, kMaxZoom);
    d_->zoom             = clamped;
    d_->fitted           = false;
    redraw();
}

void ImageViewer::fitToWindow() {
    // The layout has to have run, or the scroll area is still 0 wide and every
    // image "fits" at 1.0. A viewer that was resized but never shown is exactly
    // that case, and it is the one a test meets first.
    if (QLayout* l = layout()) l->activate();
    // Clamped like every other zoom: the render dialog allows 8192 pixels, so
    // a viewer dragged below ~410 across fits it at less than kMinZoom and
    // `zoom()` would then report a value outside the range this class promises.
    d_->zoom =
        std::clamp(fitScale(d_->image.size(), d_->area->viewport()->size()), kMinZoom, kMaxZoom);
    d_->fitted = true;
    redraw();
}

void ImageViewer::redraw() {
    if (d_->image.isNull()) {
        d_->canvas->clear();
        d_->canvas->resize(0, 0);
        d_->readout->clear();
        return;
    }
    const QSize scaled = (QSizeF(d_->image.size()) * d_->zoom).toSize().expandedTo({1, 1});
    d_->canvas->setPixmap(QPixmap::fromImage(d_->image).scaled(scaled, Qt::IgnoreAspectRatio,
                                                               Qt::SmoothTransformation));
    d_->canvas->resize(scaled);
    d_->readout->setText(tr("%1%  ·  %2 × %3")
                             .arg(std::lround(d_->zoom * 100.0))
                             .arg(d_->image.width())
                             .arg(d_->image.height()));
}

void ImageViewer::resizeEvent(QResizeEvent* e) {
    QWidget::resizeEvent(e);
    if (d_->fitted && !d_->image.isNull()) fitToWindow();
}

bool ImageViewer::eventFilter(QObject* watched, QEvent* e) {
    if (watched != d_->area->viewport()) return QWidget::eventFilter(watched, e);
    switch (e->type()) {
        case QEvent::Wheel: {
            // Plain wheel zooms, as the reference's ZoomableImageView does. A
            // render viewer is looked at, not scrolled through, and panning is
            // on the drag.
            const auto* w = static_cast<QWheelEvent*>(e);
            const int dy  = w->angleDelta().y();
            if (dy != 0) setZoom(d_->zoom * (dy > 0 ? kZoomStep : 1.0 / kZoomStep));
            return true;
        }
        case QEvent::MouseButtonPress: {
            const auto* m = static_cast<QMouseEvent*>(e);
            if (m->button() != Qt::LeftButton) break;
            d_->dragging = true;
            d_->dragFrom = m->pos();
            d_->area->viewport()->setCursor(Qt::ClosedHandCursor);
            return true;
        }
        case QEvent::MouseMove: {
            if (!d_->dragging) break;
            const auto* m      = static_cast<QMouseEvent*>(e);
            const QPoint moved = m->pos() - d_->dragFrom;
            d_->dragFrom       = m->pos();
            d_->area->horizontalScrollBar()->setValue(d_->area->horizontalScrollBar()->value() -
                                                      moved.x());
            d_->area->verticalScrollBar()->setValue(d_->area->verticalScrollBar()->value() -
                                                    moved.y());
            return true;
        }
        case QEvent::MouseButtonRelease: {
            d_->dragging = false;
            d_->area->viewport()->unsetCursor();
            return true;
        }
        default: break;
    }
    return QWidget::eventFilter(watched, e);
}

}  // namespace mh::ui
