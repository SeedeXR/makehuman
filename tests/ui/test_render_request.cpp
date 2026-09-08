// SPDX-License-Identifier: AGPL-3.0-or-later
//
// The two claims a production render makes about itself, at the only level a
// test can reach them.
//
// Both were stuck in `main.cpp` and both had already cost something:
//
//  * `rs.wireframe = req.wireframe` went MISSING from `renderImage` -- an edit
//    lost in tooling -- so the viewport drew edges while `--wireframe --render`
//    wrote a byte-identical solid PNG with every test green. Only looking at
//    the picture found it.
//  * "the grid is not in a production render" (the reference's
//    `excludeFromProduction`, `core/mhmain.py:671,687`) was gated only against
//    the FLAG leaking: a `renderImage` that switched the grid on
//    unconditionally would make both sides of that comparison equal.
#include "makehuman/ui/FrameStats.h"
#include "makehuman/ui/RenderDialog.h"

#include <catch2/catch_test_macros.hpp>

#include <QImage>

#include <string>

TEST_CASE("a render request becomes render settings, grid excluded", "[ui][render]") {
    mh::ui::RenderRequest req;
    req.width       = 640;
    req.height      = 480;
    req.transparent = true;
    req.shading     = mh::render::ShadingModel::Pbr;
    req.wireframe   = true;

    const mh::render::RenderSettings s = mh::ui::renderSettingsFor(req, "/tmp/skin.png");

    CHECK(s.width == 640);
    CHECK(s.height == 480);
    CHECK(s.transparentBackground);
    CHECK(s.shading == mh::render::ShadingModel::Pbr);
    // The line that went missing. Asserted on its own, because every other
    // field passing is exactly the state that shipped.
    CHECK(s.wireframe);
    CHECK(s.litsphere == std::filesystem::path("/tmp/skin.png"));

    // A production render is the character. The grid is scaffolding for judging
    // where the character stands, and the reference excludes it too.
    CHECK_FALSE(s.grid);
}

TEST_CASE("the defaults carry through unchanged", "[ui][render]") {
    const mh::render::RenderSettings s = mh::ui::renderSettingsFor(mh::ui::RenderRequest{}, {});
    CHECK(s.width == 1024);
    CHECK(s.height == 1024);
    CHECK_FALSE(s.transparentBackground);
    CHECK(s.shading == mh::render::ShadingModel::Litsphere);
    CHECK_FALSE(s.wireframe);
    CHECK_FALSE(s.grid);
}

// `--render` had NO blank-frame guard: `describe` was reachable only from
// `--screenshot`, so a production render of nothing at all saved a perfectly
// valid PNG and exited 0. This is the check that stops it, and it is a
// function rather than a lambda in `main.cpp` so that it can be tested with
// images a renderer would have to fail badly to produce.
TEST_CASE("a frame that drew nothing is reported as blank", "[ui][render]") {
    std::string text;

    CHECK_FALSE(mh::ui::describeFrame(QImage{}, text));
    CHECK(text.find("empty frame") != std::string::npos);

    // A flat fill is the shape a blank render actually takes: the clear colour
    // and nothing on top of it. Every arithmetic assertion about coverage
    // passes on this image, which is why the RETURN value is what callers use.
    QImage flat(64, 64, QImage::Format_RGB32);
    flat.fill(QColor(25, 25, 27));
    CHECK_FALSE(mh::ui::describeFrame(flat, text));
    CHECK(text.find("flat fill") != std::string::npos);
}

TEST_CASE("a frame that drew something reports what it drew", "[ui][render]") {
    std::string text;
    QImage img(100, 100, QImage::Format_RGB32);
    img.fill(QColor(0, 0, 0));
    // A quarter of the frame, at a known luminance, and deliberately AWAY from
    // the corner: the background is taken to be pixel (0, 0), so a shape that
    // covers it inverts the whole measurement. The first version of this test
    // filled from the origin and read 75% for a quarter-covered frame.
    for (int y = 25; y < 75; ++y) {
        for (int x = 25; x < 75; ++x)
            img.setPixelColor(x, y, QColor(255, 255, 255));
    }
    REQUIRE(mh::ui::describeFrame(img, text));

    // The numbers, not just "it said something": this text is what
    // `app_screenshot` and the render gates read, and a coverage figure that
    // drifted would quietly weaken all of them.
    CHECK(text.find("100x100") != std::string::npos);
    CHECK(text.find("covered 2500 of 10000 pixels (25.0%)") != std::string::npos);
    CHECK(text.find("min=255 max=255") != std::string::npos);
}
