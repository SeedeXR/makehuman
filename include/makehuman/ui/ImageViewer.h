// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <QWidget>

#include <memory>

class QImage;

namespace mh::ui {

/// Shows a finished render, in the application.
///
/// `OffscreenRenderer` has been in the tree since M6 and the render dialog has
/// reached it since M8, but the result went straight to a PNG and the user was
/// told about it in the status bar -- so the one thing a render is for, LOOKING
/// at it, meant leaving the application and opening a file browser. The
/// reference hands its finished image to a viewer task
/// (`legacy/python/plugins/4_rendering_9_viewer.py:78-84`) and switches to it;
/// this is that, as a window rather than a task view, because our shell has a
/// permanent viewport in the middle rather than a tab stack.
///
/// Zoom and pan are the whole point of a viewer: a 2048-square render in a
/// 900-pixel window is unreadable at fit, and 1:1 with no pan shows a corner.
/// The reference uses a `ZoomableImageView` for exactly this reason.
class ImageViewer : public QWidget {
    Q_OBJECT

public:
    explicit ImageViewer(QWidget* parent = nullptr);
    ~ImageViewer() override;

    ImageViewer(const ImageViewer&)            = delete;
    ImageViewer& operator=(const ImageViewer&) = delete;

    /// Zoom limits. Named because the widget clamps to them and a test has to
    /// say which value it expects; a wheel held down otherwise reaches 1e9 and
    /// the scaled pixmap Qt is then asked for is terabytes wide.
    static constexpr double kMinZoom = 0.05;
    static constexpr double kMaxZoom = 16.0;

    /// Shows @p image, fitted to the widget.
    ///
    /// Fitted rather than left at the previous zoom: after zooming into one
    /// render, a second one would otherwise appear as a corner at 8x with
    /// nothing to say why.
    void setImage(QImage image);
    [[nodiscard]] const QImage& image() const;
    [[nodiscard]] bool hasImage() const;

    /// Writes the image as it is, full resolution.
    /// @return false when there is nothing to write or the format is refused.
    ///         Never an empty file, which would look like a render that went
    ///         wrong rather than one that was never made.
    [[nodiscard]] bool saveAs(const QString& path) const;

    [[nodiscard]] double zoom() const;
    /// Clamped to [kMinZoom, kMaxZoom].
    void setZoom(double factor);
    void fitToWindow();

    /// The scale that shows all of @p image inside @p viewport.
    ///
    /// Never magnifies: "fit" that blows a 64-pixel thumbnail up to 900 shows a
    /// blurred mess and hides that the render came out tiny. Degenerate sizes
    /// give 1.0 rather than a division by zero -- a viewer laid out but never
    /// shown has a zero-sized viewport, and that is the first thing `setImage`
    /// meets.
    [[nodiscard]] static double fitScale(QSize image, QSize viewport);

protected:
    void resizeEvent(QResizeEvent* e) override;
    bool eventFilter(QObject* watched, QEvent* e) override;

private:
    /// Rescales the pixmap and updates the readout. Every zoom change goes
    /// through here, so there is one place the two can agree.
    void redraw();

    struct Impl;
    std::unique_ptr<Impl> d_;
};

}  // namespace mh::ui
