// SPDX-License-Identifier: AGPL-3.0-or-later
//
// A backdrop behind a production render.
//
// The reference puts an image behind the model so a user can model TO a
// photograph (`plugins/0_modeling_background.py`); the same image is what turns
// a render into a composite. This is the second half: `--background <file>`,
// applied to `--render`'s output.
//
// It is compositing, not a shader change. The renderer already produces alpha
// when asked, so the backdrop goes UNDER the finished frame and no part of the
// draw path has to know about it.
//
// **The order is load-bearing, and it is the thing most worth testing.**
// `describeFrame` calls a frame blank when it is a flat fill, and takes pixel
// (0, 0) to be the background. Composite first and a render that drew NOTHING
// over a photograph is a frame full of photograph -- not flat, corner not the
// clear colour -- so the blank-render guard would pass on exactly the failure
// it exists to catch. The guard has to run on the render, and the backdrop goes
// on afterwards.

#include "makehuman/ui/Background.h"

#include "makehuman/ui/FrameStats.h"

#include <catch2/catch_test_macros.hpp>

#include <QColor>
#include <QImage>

#include <string>

using mh::ui::overBackground;

namespace {

/// A frame with an alpha channel: `alpha` everywhere, in `colour`.
QImage frame(int w, int h, QColor colour, int alpha) {
    QImage img(w, h, QImage::Format_ARGB32);
    colour.setAlpha(alpha);
    img.fill(colour);
    return img;
}

/// A backdrop whose left half and right half differ, so that horizontal
/// squashing or cropping is visible in a single pixel read.
QImage twoTone(int w, int h) {
    QImage img(w, h, QImage::Format_RGB32);
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x)
            img.setPixelColor(x, y, x < w / 2 ? QColor(255, 0, 0) : QColor(0, 0, 255));
    }
    return img;
}

/// The same, with a GREEN margin down the outer eighth of each side.
///
/// Covering a square frame from a 2:1 source shows only the middle half, so the
/// margin is cropped away and no green survives. Squashing the same image to
/// fit shows all of it, and the green lands on the edges. Without the margin
/// the two are indistinguishable -- left half red, right half blue, seam in the
/// centre, either way -- which is what the first version of this test asserted
/// and what a mutation to `IgnoreAspectRatio` walked straight through.
QImage twoToneWithMargin(int w, int h) {
    QImage img = twoTone(w, h);
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w / 8; ++x) {
            img.setPixelColor(x, y, QColor(0, 255, 0));
            img.setPixelColor(w - 1 - x, y, QColor(0, 255, 0));
        }
    }
    return img;
}

}  // namespace

TEST_CASE("the result keeps the FRAME's size, whatever the backdrop's", "[ui][background]") {
    const QImage f = frame(320, 240, QColor(10, 10, 10), 0);

    // A backdrop is a file a user chose; nothing says it matches the render.
    for (const auto& bg : {twoTone(64, 64), twoTone(4000, 100), twoTone(100, 4000)}) {
        const QImage out = overBackground(f, bg);
        CHECK(out.width() == 320);
        CHECK(out.height() == 240);
    }
}

TEST_CASE("an opaque frame hides the backdrop entirely", "[ui][background]") {
    const QImage f   = frame(64, 64, QColor(200, 30, 40), 255);
    const QImage out = overBackground(f, twoTone(64, 64));

    // Nothing of a red-and-blue backdrop survives under a fully opaque frame.
    for (int y = 0; y < 64; y += 8) {
        for (int x = 0; x < 64; x += 8) {
            INFO("at " << x << "," << y);
            CHECK(out.pixelColor(x, y) == QColor(200, 30, 40));
        }
    }
}

TEST_CASE("a fully transparent frame IS the backdrop", "[ui][background]") {
    const QImage f   = frame(64, 64, QColor(200, 30, 40), 0);
    const QImage out = overBackground(f, twoTone(64, 64));

    CHECK(out.pixelColor(8, 32) == QColor(255, 0, 0));
    CHECK(out.pixelColor(56, 32) == QColor(0, 0, 255));
}

TEST_CASE("the subject sits on the backdrop where it is opaque", "[ui][background]") {
    QImage f = frame(64, 64, QColor(0, 0, 0), 0);
    // An opaque block in the middle, the shape a rendered character takes.
    for (int y = 16; y < 48; ++y) {
        for (int x = 16; x < 48; ++x)
            f.setPixelColor(x, y, QColor(0, 255, 0, 255));
    }
    const QImage out = overBackground(f, twoTone(64, 64));

    CHECK(out.pixelColor(32, 32) == QColor(0, 255, 0));  // the subject
    CHECK(out.pixelColor(4, 4) == QColor(255, 0, 0));    // backdrop, left half
    CHECK(out.pixelColor(60, 60) == QColor(0, 0, 255));  // backdrop, right half
}

TEST_CASE("the backdrop is COVERED and centred, not squashed", "[ui][background]") {
    const QImage f = frame(200, 200, QColor(0, 0, 0), 0);
    // 2:1 over a square frame. Cover scales to 400x200 and crops 100 columns
    // from each side, so only the middle half is visible and the seam between
    // the halves lands at the centre of the result.
    const QImage out = overBackground(f, twoToneWithMargin(400, 200));

    CHECK(out.pixelColor(99, 100) == QColor(255, 0, 0));
    CHECK(out.pixelColor(100, 100) == QColor(0, 0, 255));

    // The assertion that separates cover from squash. The green margin is the
    // outer eighth of the SOURCE, which cover crops away entirely; a squashed
    // backdrop would show it along both edges of the result. A seam-at-centre
    // check alone passes either way, which is the trap this replaces.
    CHECK(out.pixelColor(0, 100) != QColor(0, 255, 0));
    CHECK(out.pixelColor(199, 100) != QColor(0, 255, 0));
    CHECK(out.pixelColor(2, 100) == QColor(255, 0, 0));
    CHECK(out.pixelColor(197, 100) == QColor(0, 0, 255));
}

TEST_CASE("a null backdrop leaves the frame exactly as it was", "[ui][background]") {
    // `--background` naming a file Qt cannot read must not silently blank the
    // render; the caller reports the bad file and the frame stands.
    //
    // The frame here is TRANSPARENT, which is the whole point. Checked with an
    // opaque frame this passes with the null guard removed: the destination
    // QImage is uninitialised, and an opaque frame drawn over uninitialised
    // memory hides it completely. A mutation that deleted the guard walked
    // through the first version of this test for exactly that reason.
    QImage f = frame(32, 32, QColor(1, 2, 3), 0);
    f.setPixelColor(4, 4, QColor(9, 8, 7, 255));

    const QImage out = overBackground(f, QImage{});
    REQUIRE(out.size() == f.size());
    CHECK(out.pixelColor(4, 4) == QColor(9, 8, 7, 255));
    // Still transparent where it was: not painted over anything, not garbage.
    CHECK(out.pixelColor(16, 16).alpha() == 0);
}

TEST_CASE("a null frame stays null", "[ui][background]") {
    CHECK(overBackground(QImage{}, twoTone(8, 8)).isNull());
}

TEST_CASE("compositing does NOT make a blank render look drawn", "[ui][background]") {
    // The regression this file exists for. A render that drew nothing is a flat
    // fill of the clear colour; `describeFrame` catches that and the
    // application refuses to save. Put a photograph under it first and the
    // frame is neither flat nor clear-coloured at (0, 0) -- so the guard would
    // pass and the failure would be written to disk as a valid PNG.
    QImage blank(64, 64, QImage::Format_ARGB32);
    blank.fill(QColor(25, 25, 27, 0));

    std::string text;
    REQUIRE_FALSE(mh::ui::describeFrame(blank, text));

    const QImage composited = overBackground(blank, twoTone(64, 64));
    // Stated as a fact about the composite, so that the reason the application
    // must check BEFORE compositing is written down where it can fail:
    CHECK(mh::ui::describeFrame(composited, text));
}
