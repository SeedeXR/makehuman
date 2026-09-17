// SPDX-License-Identifier: Apache-2.0
//
// `backdropUvTransform`, which is the one piece of the viewport backdrop with a
// GRAPHICS BACKEND in it -- and the piece that shipped wrong.
//
// The backdrop is a fullscreen triangle whose first corner sits at NDC y = -1.
// Where the API puts +1 at the top (Metal), that corner is the BOTTOM of the
// frame, while v = 0 is always the image's FIRST row. The first version ignored
// that and drew every photograph upside down.
//
// It survived being LOOKED AT, because the fixture was a chequerboard and a
// chequerboard is symmetric; it survived every "differs from plain" pixel gate,
// because an upside-down image differs from plain just as well. A review caught
// it, a red-over-blue image confirmed it, and the rule moved here so a unit
// test can hold it -- the pixel checks that could also catch it need Pillow,
// which the macOS CI jobs do not install.

#include "makehuman/render/SceneResources.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

using Catch::Matchers::WithinAbs;
using mh::render::backdropUvTransform;

namespace {
constexpr double kEps = 1e-5;
}  // namespace

TEST_CASE("a y-down backend maps the first corner to the image's first row", "[render][backdrop]") {
    // The whole 200x100 image, no crop. v must run 0 -> 1 with the corner.
    const auto uv = backdropUvTransform(QRectF(0, 0, 200, 100), QSize(200, 100), false);
    CHECK_THAT(static_cast<double>(uv[0]), WithinAbs(1.0, kEps));  // scaleU
    CHECK_THAT(static_cast<double>(uv[1]), WithinAbs(1.0, kEps));  // scaleV, NOT negated
    CHECK_THAT(static_cast<double>(uv[2]), WithinAbs(0.0, kEps));  // offsetU
    CHECK_THAT(static_cast<double>(uv[3]), WithinAbs(0.0, kEps));  // offsetV
}

TEST_CASE("a y-up backend flips v so the photograph is not upside down", "[render][backdrop]") {
    const auto uv = backdropUvTransform(QRectF(0, 0, 200, 100), QSize(200, 100), true);
    CHECK_THAT(static_cast<double>(uv[0]), WithinAbs(1.0, kEps));
    // corner.y = 0 is the BOTTOM of the frame, so it must sample v = 1.
    CHECK_THAT(static_cast<double>(uv[1]), WithinAbs(-1.0, kEps));
    CHECK_THAT(static_cast<double>(uv[3]), WithinAbs(1.0, kEps));

    // Said as the thing that actually matters, rather than as two constants:
    // evaluate the map at both ends of the visible range.
    const auto v = [&](float t) { return t * uv[1] + uv[3]; };
    INFO("bottom of frame samples v=" << v(0.0F) << ", top samples v=" << v(1.0F));
    CHECK_THAT(static_cast<double>(v(0.0F)), WithinAbs(1.0, kEps));  // frame bottom -> image bottom
    CHECK_THAT(static_cast<double>(v(1.0F)), WithinAbs(0.0, kEps));  // frame top    -> image top
}

TEST_CASE("a cropped source keeps its own top and bottom under the flip", "[render][backdrop]") {
    // A cover-fit crop of the middle 50 rows out of 100.
    const QRectF src(0, 25, 200, 50);
    const auto down = backdropUvTransform(src, QSize(200, 100), false);
    const auto up   = backdropUvTransform(src, QSize(200, 100), true);

    const auto at = [](const std::array<float, 4>& uv, float t) { return t * uv[1] + uv[3]; };
    // Both backends must show the SAME band of the image -- 0.25 to 0.75 --
    // only traversed in opposite directions. A flip that also moved the crop
    // would pass a "v is negated" check and show the wrong part of the photo.
    CHECK_THAT(static_cast<double>(at(down, 0.0F)), WithinAbs(0.25, kEps));
    CHECK_THAT(static_cast<double>(at(down, 1.0F)), WithinAbs(0.75, kEps));
    CHECK_THAT(static_cast<double>(at(up, 0.0F)), WithinAbs(0.75, kEps));
    CHECK_THAT(static_cast<double>(at(up, 1.0F)), WithinAbs(0.25, kEps));
}

TEST_CASE("a degenerate backdrop asks for nothing", "[render][backdrop]") {
    for (const bool yUp : {false, true}) {
        CHECK(backdropUvTransform(QRectF(), QSize(200, 100), yUp)[1] == 0.0F);
        CHECK(backdropUvTransform(QRectF(0, 0, 200, 100), QSize(0, 0), yUp)[1] == 0.0F);
    }
}
