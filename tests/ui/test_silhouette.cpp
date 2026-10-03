// SPDX-License-Identifier: Apache-2.0
//
// The outline comparison, tested on shapes drawn here rather than on renders.
// A render would make a failure ambiguous between "the metric is wrong" and
// "the renderer changed", and the metric is the part that has to be right.
#include "makehuman/ui/Silhouette.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <QImage>
#include <QPainter>

using namespace mh::ui;
using Catch::Approx;

namespace {

/// A shape on a dark ground, like our renders. The corner is background, which
/// is the assumption `silhouetteOf` makes.
QImage canvas(int size = 200) {
    QImage img(size, size, QImage::Format_ARGB32);
    img.fill(QColor(25, 25, 27));
    return img;
}

QImage withEllipse(QRect box, int size = 200) {
    QImage img = canvas(size);
    QPainter p(&img);
    p.setBrush(QColor(220, 150, 120));
    p.setPen(Qt::NoPen);
    p.drawEllipse(box);
    return img;
}

QImage withRect(QRect box, int size = 200) {
    QImage img = canvas(size);
    QPainter p(&img);
    p.fillRect(box, QColor(220, 150, 120));
    return img;
}

}  // namespace

TEST_CASE("an outline is found and measured", "[silhouette]") {
    const Silhouette s = silhouetteOf(withRect(QRect(50, 40, 60, 100)));
    CHECK(s.area == 60 * 100);
    CHECK(s.bounds == QRect(50, 40, 60, 100));
    CHECK(s.coverage == Approx(6000.0 / 40000.0));
}

TEST_CASE("an empty frame has no outline", "[silhouette]") {
    // Not an error: a render that drew nothing is a real thing to report, and
    // the caller is the one who knows whether that means a broken GPU or an
    // empty scene.
    const Silhouette s = silhouetteOf(canvas());
    CHECK(s.area == 0);
    CHECK(s.bounds.isNull());
    CHECK(s.coverage == Approx(0.0));
}

TEST_CASE("a cluttered background shows up as coverage, not as a good score", "[silhouette]") {
    // THE FAILURE THIS HAS TO SURFACE. A photograph whose background does not
    // match its own top-left corner comes back almost entirely "subject". The
    // mask is then meaningless -- but it is a perfectly valid mask, and an IoU
    // computed from it looks like any other number. `coverage` near 1 is the
    // only thing that says the separation failed.
    QImage busy = canvas();
    QPainter p(&busy);
    p.fillRect(QRect(1, 0, 199, 200), QColor(200, 30, 30));  // everything but the corner
    p.end();
    const Silhouette s = silhouetteOf(busy);
    CHECK(s.coverage > 0.99);
}

TEST_CASE("identical outlines score 1", "[silhouette]") {
    const QImage img = withEllipse(QRect(60, 30, 80, 140));
    const auto m     = compareSilhouettes(silhouetteOf(img), silhouetteOf(img));
    CHECK(m.iou == Approx(1.0));
    CHECK(m.widthRatio == Approx(1.0));
    CHECK(m.heightRatio == Approx(1.0));
}

TEST_CASE("the same shape, moved and resized, still scores high", "[silhouette]") {
    // THE POINT OF THE METRIC. A reference photograph is taken at an unknown
    // distance and the subject is not centred to the pixel. A shape that is
    // RIGHT but framed differently must not read as a miss, or every fit would
    // be driven by the lens rather than the body.
    const Silhouette a = silhouetteOf(withEllipse(QRect(60, 30, 80, 140)));
    const Silhouette b = silhouetteOf(withEllipse(QRect(20, 10, 40, 70)));
    const auto m       = compareSilhouettes(a, b);
    CHECK(m.iou > 0.97);
    // ...and the size difference is still REPORTED, not lost. Half the width
    // and half the height, which `iou` deliberately ignores.
    CHECK(m.widthRatio == Approx(0.5).margin(0.05));
    CHECK(m.heightRatio == Approx(0.5).margin(0.05));
}

TEST_CASE("different shapes in the same box score well below 1", "[silhouette]") {
    // The other half of the previous test: scale-invariance must not be so
    // forgiving that it stops discriminating. An ellipse fills pi/4 of its box
    // and a rectangle fills all of it, so the IoU is about 0.785.
    const Silhouette ellipse = silhouetteOf(withEllipse(QRect(50, 50, 100, 100)));
    const Silhouette square  = silhouetteOf(withRect(QRect(50, 50, 100, 100)));
    const auto m             = compareSilhouettes(ellipse, square);
    CHECK(m.iou < 0.82);
    CHECK(m.iou > 0.74);
    // The box is the same, so the ratios say "same framing" while the IoU says
    // "different body". That separation is what a caller acts on.
    CHECK(m.widthRatio == Approx(1.0).margin(0.02));
}

TEST_CASE("comparing against nothing scores 0 rather than throwing", "[silhouette]") {
    const auto m =
        compareSilhouettes(silhouetteOf(withRect(QRect(10, 10, 20, 20))), silhouetteOf(canvas()));
    CHECK(m.iou == Approx(0.0));
    CHECK(m.intersection == 0);
}

TEST_CASE("a transparent background is background whatever its colour", "[silhouette]") {
    // `--transparent` leaves the RGB under alpha 0 undefined, so classifying by
    // colour alone would read the clear's leftovers as subject and call the
    // whole frame covered.
    QImage img(100, 100, QImage::Format_ARGB32);
    img.fill(QColor(220, 150, 120, 0));  // opaque-looking colour, zero alpha
    QPainter p(&img);
    p.setCompositionMode(QPainter::CompositionMode_Source);
    p.fillRect(QRect(40, 40, 20, 20), QColor(220, 150, 120, 255));
    p.end();
    const Silhouette s = silhouetteOf(img);
    CHECK(s.area == 400);
    CHECK(s.bounds == QRect(40, 40, 20, 20));
}
