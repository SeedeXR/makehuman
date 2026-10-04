// SPDX-License-Identifier: Apache-2.0
//
// What MoGe actually does, pinned against what it was believed to do.
//
// THE BELIEF, WHICH WAS WRONG: that MoGe's `mask` output separates a subject
// from its background, and so rescues `ui::silhouetteOf` from the limitation
// its own header admits -- that a corner-colour rule "does not hold for a snap
// taken in a kitchen". It came from ONE observation, on our own render against
// a flat black clear colour, where the mask traces the body exactly.
//
// It was excluding the VOID, which has no geometry to predict. On a scene with
// real content the mask covers nearly all of it, because nearly all of a real
// scene HAS valid geometry. Measured here against a ground-truth subject, MoGe
// scores no better than the corner rule it was meant to replace.
//
// So these tests pin the REFUTATION, not a capability. The fixture is a
// collage of rectangles rather than a photograph, so it does not prove MoGe
// fails on real photographs either -- which is precisely why the bars below
// are loose and are about SHAPE and RESOLUTION, with the one hard assertion
// being that the mask is not a segmentation. Anything that wants to use MoGe
// for segmentation has to come back with real photographs and move these
// numbers first.
//
// SKIPS, never fails, when the model is absent. A 134 MB download is not a
// build dependency (LICENSING.md 5.2c).
#include "makehuman/foundation/FocalShift.h"
#include "makehuman/moge/Session.h"
#include "makehuman/ui/Silhouette.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <QImage>
#include <QPainter>
#include <QRandomGenerator>

using namespace mh;

namespace {

/// A figure: a body-ish blob that a depth model can plausibly read as a
/// subject standing in front of something.
QImage figureOn(const QImage& background) {
    QImage out = background.copy();
    QPainter p(&out);
    p.setRenderHint(QPainter::Antialiasing);
    p.setBrush(QColor(214, 170, 150));
    p.setPen(Qt::NoPen);
    const int cx = out.width() / 2;
    p.drawEllipse(QPoint(cx, out.height() / 5), out.width() / 12, out.height() / 12);  // head
    p.drawRoundedRect(
        QRect(cx - out.width() / 10, out.height() / 3, out.width() / 5, out.height() / 2), 18,
        18);  // torso
    return out;
}

QImage flatDark(int w, int h) {
    QImage img(w, h, QImage::Format_RGB888);
    img.fill(QColor(25, 25, 27));
    return img;
}

/// A background the corner rule cannot survive: every pixel differs from the
/// top-left one, so colour distance calls the entire frame "subject".
QImage clutter(int w, int h) {
    QImage img(w, h, QImage::Format_RGB888);
    QPainter p(&img);
    img.fill(QColor(30, 40, 60));
    QRandomGenerator rng(1234);  // fixed seed: a flaky fixture is not a test
    for (int i = 0; i < 160; ++i) {
        p.setBrush(QColor(static_cast<int>(rng.bounded(40, 230)),
                          static_cast<int>(rng.bounded(40, 230)),
                          static_cast<int>(rng.bounded(40, 230))));
        p.setPen(Qt::NoPen);
        const int x = static_cast<int>(rng.bounded(0, w));
        const int y = static_cast<int>(rng.bounded(0, h));
        p.drawRect(x, y, static_cast<int>(rng.bounded(10, 90)),
                   static_cast<int>(rng.bounded(10, 90)));
    }
    return img;
}

}  // namespace

TEST_CASE("the corner rule struggles on a cluttered background", "[moge]") {
    // The premise that motivated looking at MoGe at all, asserted rather than
    // assumed. 0.88 of the frame claimed as subject for a figure that is 12% of
    // it: the rule does fail here, which was never the part in doubt.
    const ui::Silhouette s = ui::silhouetteOf(figureOn(clutter(384, 384)));
    CHECK(s.coverage > 0.7);
}

TEST_CASE("the corner rule still works on our own renders", "[moge]") {
    // ...and it must keep working, because that is why it is not being
    // replaced: on a flat clear colour it is exact and costs nothing.
    const ui::Silhouette s = ui::silhouetteOf(figureOn(flatDark(384, 384)));
    CHECK(s.coverage > 0.02);
    CHECK(s.coverage < 0.5);
}

TEST_CASE("MoGe loads and returns a usable prediction", "[moge][model]") {
    if (!moge::modelAvailable()) {
        SKIP("no MoGe model: run tools/fetch_moge.sh");
    }
    auto session = moge::Session::open(moge::defaultModelPath());
    REQUIRE(session.has_value());

    const QImage photo = figureOn(clutter(384, 384));
    auto prediction    = session->run(photo);
    REQUIRE(prediction.has_value());

    // The contract that IS real: a mask at the caller's resolution, a finite
    // scale, and a coverage that is reported rather than hidden.
    CHECK(prediction->mask.size() == photo.size());
    CHECK(prediction->mask.format() == QImage::Format_Grayscale8);
    CHECK(prediction->scale > 0.0F);
    CHECK(prediction->coverage >= 0.0);
    CHECK(prediction->coverage <= 1.0);

    const ui::Silhouette s = ui::silhouetteFromMask(prediction->mask);
    CHECK(s.mask.size() == photo.size());
}

TEST_CASE("MoGe's mask is NOT a subject segmentation", "[moge][model]") {
    // THE ASSERTION THAT MATTERS, and it is a negative one on purpose.
    //
    // If this test ever fails it does not mean the code broke -- it means the
    // belief this module was nearly built on became true, and the header,
    // LICENSING.md 5.2c and the next increment all need rewriting. That is
    // worth being told about.
    if (!moge::modelAvailable()) {
        SKIP("no MoGe model: run tools/fetch_moge.sh");
    }
    auto session = moge::Session::open(moge::defaultModelPath());
    REQUIRE(session.has_value());

    auto prediction = session->run(figureOn(clutter(384, 384)));
    REQUIRE(prediction.has_value());

    // The subject is about 12% of this frame. The mask claims most of it,
    // because most of it has valid geometry. Measured at 0.78 on the scene
    // this fixture reproduces.
    INFO("MoGe coverage " << prediction->coverage);
    CHECK(prediction->coverage > 0.5);
}

TEST_CASE("a missing model is reported, not crashed on", "[moge]") {
    const auto s = moge::Session::open("/nonexistent/moge.onnx");
    REQUIRE_FALSE(s.has_value());
    CHECK(s.error().find("fetch_moge") != std::string::npos);
}

TEST_CASE("silhouetteFromMask measures a mask it did not produce", "[moge]") {
    // No model, no ONNX: this is the whole reason separating the deciding from
    // the measuring was worth a function.
    QImage mask(100, 80, QImage::Format_Grayscale8);
    mask.fill(0);
    QPainter p(&mask);
    p.fillRect(QRect(20, 10, 30, 40), QColor(255, 255, 255));
    p.end();

    const ui::Silhouette s = ui::silhouetteFromMask(mask);
    CHECK(s.area == 30 * 40);
    CHECK(s.bounds == QRect(20, 10, 30, 40));
    CHECK(s.coverage == Catch::Approx(1200.0 / 8000.0));
}

TEST_CASE("metric depth is recovered, and reads the image", "[moge][model]") {
    // THE TEST THAT SEPARATES OUR BUG FROM THE MODEL'S LIMIT.
    //
    // `test_focal_shift.cpp` already proves the solve recovers cameras we
    // chose, with no model involved. What it cannot show is whether the whole
    // chain -- model, mask, solve -- responds to the actual picture.
    //
    // Cropping towards the centre narrows the true field of view by a known
    // factor. If the recovered field of view falls with it, the chain is
    // reading the image. If it sat at some constant, the model would be
    // returning a prior and every depth from it would be decoration.
    //
    // It does NOT assert accuracy, deliberately. Measured on this fixture,
    // rendered at a true 30 degrees, the chain reports about 45 -- biased high
    // because a figure on a flat background gives no scene cues. That bias is
    // the model's and is recorded in Session.h; pinning it here would make this
    // test fail the day MoGe improves, which is the wrong thing to be told.
    if (!moge::modelAvailable()) {
        SKIP("no MoGe model: run tools/fetch_moge.sh");
    }
    const QImage full(QStringLiteral(MH_TEST_DIR "/golden/moge/figure_front.png"));
    REQUIRE_FALSE(full.isNull());

    auto session = moge::Session::open(moge::defaultModelPath());
    REQUIRE(session.has_value());

    const auto fovOf = [&](double fraction) {
        const int w = static_cast<int>(full.width() * fraction);
        const int h = static_cast<int>(full.height() * fraction);
        const QImage cropped =
            full.copy((full.width() - w) / 2, (full.height() - h) / 2, w, h)
                .scaled(512, 512, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
        auto p = session->run(cropped);
        REQUIRE(p.has_value());
        REQUIRE(p->metric);
        CHECK(p->depthWidth > 0);
        CHECK(p->depth.size() ==
              static_cast<size_t>(p->depthWidth) * static_cast<size_t>(p->depthHeight));
        return p->fovDegrees;
    };

    const float wide   = fovOf(1.0);
    const float narrow = fovOf(0.5);
    INFO("recovered fov: full " << wide << ", half-crop " << narrow);
    CHECK(wide > narrow);
    // A real response, not a rounding wobble. Halving the crop halves the true
    // field of view, and the measured pair moved 45.6 -> 31.8.
    CHECK(wide - narrow > 5.0F);
}

TEST_CASE("depth is left EMPTY when it could not be recovered", "[moge]") {
    // The honesty that makes the flag worth having: a caller must never get a
    // plausible array it would believe. No model needed -- an impossible input
    // to the solve is enough.
    const std::vector<float> nothing;
    const auto r = mh::foundation::recoverFocalShift(nothing, 64, 64);
    CHECK_FALSE(r.recovered);
    CHECK(r.focal == 1.0F);
    CHECK(r.shift == 0.0F);
}
