// SPDX-License-Identifier: Apache-2.0
//
// The viewport backdrop: an image behind the model so a user can model TO a
// photograph, which is what the reference's `BackgroundChooser` exists for
// (`plugins/0_modeling_background.py:104`).
//
// This port already put a backdrop under `--render` (`mh::ui::overBackground`),
// but that composites AFTER the frame is drawn and only for a production
// render. The viewport -- the thing you actually model in -- had none, so the
// capability the reference's tab provides was missing.
//
// Two pure rules are gated here, and they are the whole of the logic:
//
//   1. COVER-FIT, shared. `overBackground` scales the image to cover the frame
//      and crops the overflow evenly. The viewport's shader has to place the
//      same image the same way, from UV scale and offset. Two implementations
//      of one rule is exactly how they drift apart, so both now read
//      `coverSource`, and the CPU path's existing tests hold it honest.
//
//   2. WHICH SIDE. The reference binds a background to an AXIS VIEW and shows
//      it only when the camera looks along that axis -- the point is modelling
//      to an orthographic reference photo, so a front photo behind a
//      three-quarter view would be worse than none. This port has the same six
//      views (`MainWindow.cpp:510-537`), so the binding maps directly.

#include "makehuman/ui/Backdrop.h"

#include <catch2/catch_test_macros.hpp>

#include <QSize>

using mh::ui::BackdropSide;
using mh::ui::coverSource;
using mh::ui::facingSide;
using mh::ui::sideFacing;

TEST_CASE("cover-fit crops the overflowing axis and centres what is left", "[ui][backdrop]") {
    // A 400x200 image into a 100x100 frame: too wide, so the full height is
    // kept and the width is cropped to 200, centred -- 100 off each side.
    const QRectF wide = coverSource(QSize(100, 100), QSize(400, 200));
    CHECK(wide.height() == 200.0);
    CHECK(wide.width() == 200.0);
    CHECK(wide.y() == 0.0);
    CHECK(wide.x() == 100.0);

    // ...and the mirror case: a 200x400 image is too tall.
    const QRectF tall = coverSource(QSize(100, 100), QSize(200, 400));
    CHECK(tall.width() == 200.0);
    CHECK(tall.height() == 200.0);
    CHECK(tall.x() == 0.0);
    CHECK(tall.y() == 100.0);
}

TEST_CASE("an image already matching the frame's aspect is not cropped at all", "[ui][backdrop]") {
    // Cover must be a no-op here. A rule that always crops "a little" would
    // pass a differs-from-plain screenshot check while quietly losing edges.
    const QRectF same = coverSource(QSize(800, 400), QSize(400, 200));
    CHECK(same.x() == 0.0);
    CHECK(same.y() == 0.0);
    CHECK(same.width() == 400.0);
    CHECK(same.height() == 200.0);
}

TEST_CASE("a degenerate size asks for nothing rather than dividing by zero", "[ui][backdrop]") {
    CHECK(coverSource(QSize(0, 100), QSize(400, 200)).isEmpty());
    CHECK(coverSource(QSize(100, 100), QSize(0, 0)).isEmpty());
}

TEST_CASE("a backdrop shows only from the view it was bound to", "[ui][backdrop]") {
    // The reference's angles, which this port's axis views already use:
    // front [0,0], back [180,0], right [90,0], left [-90,0] (MainWindow.cpp).
    CHECK(facingSide(BackdropSide::Front, 0.0F, 0.0F));
    CHECK(facingSide(BackdropSide::Back, 180.0F, 0.0F));
    CHECK(facingSide(BackdropSide::Right, 90.0F, 0.0F));
    CHECK(facingSide(BackdropSide::Left, -90.0F, 0.0F));

    // The whole point: a FRONT photo must not hang behind a three-quarter or a
    // side view. This is the assertion that separates "bound to a side" from
    // "always on", and an always-true predicate still passes a
    // differs-from-plain screenshot.
    CHECK_FALSE(facingSide(BackdropSide::Front, 90.0F, 0.0F));
    CHECK_FALSE(facingSide(BackdropSide::Front, 45.0F, 0.0F));
    CHECK_FALSE(facingSide(BackdropSide::Front, 180.0F, 0.0F));
}

TEST_CASE("yaw wraps, so the same view reached the long way still shows it", "[ui][backdrop]") {
    // The camera's yaw accumulates as the user drags; nothing normalises it, so
    // 360 and -360 are the front view just as much as 0 is.
    CHECK(facingSide(BackdropSide::Front, 360.0F, 0.0F));
    CHECK(facingSide(BackdropSide::Front, -360.0F, 0.0F));
    CHECK(facingSide(BackdropSide::Right, 450.0F, 0.0F));
    CHECK(facingSide(BackdropSide::Back, -180.0F, 0.0F));
}

TEST_CASE("looking down hides a side backdrop and shows the top one", "[ui][backdrop]") {
    // Top and bottom are PITCH views, and the port clamps them to the same
    // limit the mouse obeys, so the pitch that selects them is not +-90.
    CHECK_FALSE(facingSide(BackdropSide::Front, 0.0F, 85.0F));
    CHECK(facingSide(BackdropSide::Top, 0.0F, 85.0F));
    CHECK(facingSide(BackdropSide::Bottom, 0.0F, -85.0F));
    // ...and the top photo must not appear while looking straight ahead.
    CHECK_FALSE(facingSide(BackdropSide::Top, 0.0F, 0.0F));
}

TEST_CASE("the side the camera faces is the one a backdrop would show from", "[ui][backdrop]") {
    // The inverse of `facingSide`, and it must agree with it -- the UI binds a
    // photograph to whatever this returns, so a disagreement would load an
    // image that is then immediately hidden.
    REQUIRE(sideFacing(0.0F, 0.0F) == BackdropSide::Front);
    REQUIRE(sideFacing(90.0F, 0.0F) == BackdropSide::Right);
    REQUIRE(sideFacing(-90.0F, 0.0F) == BackdropSide::Left);
    REQUIRE(sideFacing(180.0F, 0.0F) == BackdropSide::Back);
    REQUIRE(sideFacing(0.0F, 85.0F) == BackdropSide::Top);
    REQUIRE(sideFacing(0.0F, -85.0F) == BackdropSide::Bottom);

    // A three-quarter view faces NOTHING. Returning Front here would be a
    // guess, and the photograph would vanish the moment it was loaded.
    CHECK_FALSE(sideFacing(45.0F, 0.0F).has_value());

    // Whatever it returns, `facingSide` must agree -- checked across a full
    // turn rather than at the six angles that were chosen to work.
    for (int yaw = -360; yaw <= 360; yaw += 5) {
        const auto side = sideFacing(static_cast<float>(yaw), 0.0F);
        if (side.has_value()) {
            INFO("yaw " << yaw << " reported a side that facingSide denies");
            CHECK(facingSide(*side, static_cast<float>(yaw), 0.0F));
        }
    }
}
