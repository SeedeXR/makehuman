// SPDX-License-Identifier: AGPL-3.0-or-later
//
// The in-app render viewer. The application could render since M6 and the
// window could ask for one since the render dialog landed, but the result went
// straight to a PNG and the user was told about it in the status bar -- so the
// one thing a render is for, LOOKING at it, meant leaving the application.
//
// Runs on the offscreen platform: geometry and scaling are arithmetic, and
// that is what these check. What the widget paints is judged by eye, from a
// grab, in the same session that changes it.
#include "makehuman/ui/ImageViewer.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <QColor>
#include <QImage>

#include <cstdlib>
#include <filesystem>
#include <system_error>

using Catch::Matchers::WithinAbs;

namespace {

QImage solid(int w, int h, QColor c = Qt::red) {
    QImage img(w, h, QImage::Format_ARGB32);
    img.fill(c);
    return img;
}

}  // namespace

TEST_CASE("fit scale is the smaller ratio, and never magnifies", "[ui][viewer]") {
    // A 1024-square render in a 600x400 panel is limited by the HEIGHT. Taking
    // the width ratio instead gives 0.586 and crops a third of the image off
    // the bottom, which looks like a rendering bug rather than a fit bug.
    CHECK_THAT(mh::ui::ImageViewer::fitScale({1024, 1024}, {600, 400}),
               WithinAbs(400.0 / 1024.0, 1e-9));
    CHECK_THAT(mh::ui::ImageViewer::fitScale({1024, 512}, {600, 400}),
               WithinAbs(600.0 / 1024.0, 1e-9));

    // Small image, big panel: 1:1, not blown up. "Fit" that magnifies a 64px
    // thumbnail to 900px shows a blurred mess and hides that the render came
    // out tiny.
    CHECK_THAT(mh::ui::ImageViewer::fitScale({64, 64}, {900, 900}), WithinAbs(1.0, 1e-9));

    // Degenerate inputs must not divide by zero. A viewer laid out but never
    // shown has a zero-sized viewport, and that is the first thing setImage
    // meets.
    CHECK(mh::ui::ImageViewer::fitScale({0, 0}, {600, 400}) == 1.0);
    CHECK(mh::ui::ImageViewer::fitScale({1024, 1024}, {0, 0}) == 1.0);
}

TEST_CASE("the viewer holds the image it was given", "[ui][viewer]") {
    mh::ui::ImageViewer v;
    CHECK(v.image().isNull());
    CHECK_FALSE(v.hasImage());

    v.setImage(solid(320, 240));
    REQUIRE(v.hasImage());
    CHECK(v.image().size() == QSize(320, 240));
    CHECK(v.image().pixelColor(10, 10) == QColor(Qt::red));

    // A second render replaces the first rather than stacking.
    v.setImage(solid(64, 64, Qt::blue));
    CHECK(v.image().size() == QSize(64, 64));
    CHECK(v.image().pixelColor(1, 1) == QColor(Qt::blue));
}

TEST_CASE("zoom is clamped to a usable range", "[ui][viewer]") {
    mh::ui::ImageViewer v;
    v.setImage(solid(100, 100));

    v.setZoom(4.0);
    CHECK_THAT(v.zoom(), WithinAbs(4.0, 1e-9));

    // Beyond the ends it stops rather than running away: a wheel held down
    // used to reach 1e9 and the widget then asked Qt for a scaled pixmap
    // several terabytes wide.
    v.setZoom(1000.0);
    CHECK_THAT(v.zoom(), WithinAbs(mh::ui::ImageViewer::kMaxZoom, 1e-9));
    v.setZoom(0.0);
    CHECK_THAT(v.zoom(), WithinAbs(mh::ui::ImageViewer::kMinZoom, 1e-9));
    v.setZoom(-3.0);
    CHECK_THAT(v.zoom(), WithinAbs(mh::ui::ImageViewer::kMinZoom, 1e-9));
}

TEST_CASE("a new image is fitted, not left at the previous zoom", "[ui][viewer]") {
    // Zooming into one render and then producing another must not leave the
    // second one showing a corner at 8x with no way to tell what happened.
    mh::ui::ImageViewer v;
    v.resize(600, 400);
    v.setImage(solid(1024, 1024));
    const double fitted = v.zoom();
    CHECK(fitted < 1.0);

    v.setZoom(8.0);
    CHECK_THAT(v.zoom(), WithinAbs(8.0, 1e-9));

    v.setImage(solid(1024, 1024, Qt::green));
    CHECK_THAT(v.zoom(), WithinAbs(fitted, 1e-9));
}

TEST_CASE("fitting a huge image into a tiny window stays in range", "[ui][viewer]") {
    // The render dialog allows 8192 pixels a side, and the viewer is a resizable
    // window: dragged below about 410 across, the geometric fit for an 8192
    // render is under kMinZoom. `zoom()` promises the range, so fitToWindow has
    // to clamp like every other path into it.
    mh::ui::ImageViewer v;
    v.resize(120, 90);
    v.setImage(solid(8192, 8192));
    CHECK(v.zoom() >= mh::ui::ImageViewer::kMinZoom);
    CHECK(v.zoom() <= mh::ui::ImageViewer::kMaxZoom);
}

TEST_CASE("the viewer writes the image it is showing", "[ui][viewer]") {
    // Save As is the whole reason the render no longer demands a path up front,
    // so it has to write the actual pixels rather than a re-render or a
    // screen-sized copy.
    mh::ui::ImageViewer v;
    v.setImage(solid(48, 32, Qt::magenta));

    const auto out = std::filesystem::temp_directory_path() / "mh_viewer_save.png";
    std::error_code ec;
    std::filesystem::remove(out, ec);
    REQUIRE(v.saveAs(QString::fromStdString(out.string())));

    const QImage back(QString::fromStdString(out.string()));
    REQUIRE_FALSE(back.isNull());
    CHECK(back.size() == QSize(48, 32));
    CHECK(back.pixelColor(4, 4) == QColor(Qt::magenta));
    std::filesystem::remove(out, ec);

    // Nothing to save is a refusal, not an empty file that looks like a render
    // that went wrong.
    mh::ui::ImageViewer empty;
    CHECK_FALSE(empty.saveAs(QString::fromStdString(out.string())));
    CHECK_FALSE(std::filesystem::exists(out));
}

// The one thing a viewer must do, and the one thing arithmetic cannot check.
//
// Every assertion above passes on a widget that stores the image, computes the
// right scale, and paints nothing -- an empty QLabel with the correct geometry.
// Six defects in this project shipped past a green suite and were only ever
// visible in a picture, so this grabs the widget and looks at the pixels.
//
// `MH_VIEWER_SHOT` writes the grab out, for a human to look at in the session
// that changes this. The assertions run either way.
TEST_CASE("the viewer actually paints the image", "[ui][viewer]") {
    mh::ui::ImageViewer v;
    v.resize(480, 360);

    QImage src(200, 100, QImage::Format_ARGB32);
    // Two halves, so a grab can be told apart from any single fill -- including
    // the widget's own background, which is what an unpainted canvas shows.
    for (int y = 0; y < src.height(); ++y) {
        for (int x = 0; x < src.width(); ++x) {
            src.setPixelColor(x, y,
                              x < src.width() / 2 ? QColor(220, 40, 40) : QColor(40, 90, 220));
        }
    }
    v.setImage(src);

    const QImage shot = v.grab().toImage();
    if (const char* out = std::getenv("MH_VIEWER_SHOT")) {
        REQUIRE(shot.save(QString::fromUtf8(out)));
    }
    REQUIRE_FALSE(shot.isNull());

    // Both halves of the source must be somewhere in the grab. A canvas that
    // paints nothing, or paints one flat colour, fails here while every
    // geometry assertion above still passes.
    bool sawRed  = false;
    bool sawBlue = false;
    for (int y = 0; y < shot.height() && !(sawRed && sawBlue); ++y) {
        for (int x = 0; x < shot.width(); ++x) {
            const QColor c = shot.pixelColor(x, y);
            if (c.red() > 180 && c.green() < 90 && c.blue() < 90) sawRed = true;
            if (c.blue() > 180 && c.red() < 90) sawBlue = true;
        }
    }
    CHECK(sawRed);
    CHECK(sawBlue);
}
