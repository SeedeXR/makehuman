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

#include "makehuman/render/SceneResources.h"
#include "makehuman/ui/Backdrop.h"

#include <catch2/catch_test_macros.hpp>

#include <QSize>

#include <algorithm>
#include <string>
#include <vector>

using mh::ui::BackdropPlacement;
using mh::ui::BackdropSide;
using mh::ui::BackdropTransform;
using mh::ui::coverSource;
using mh::ui::facingSide;
using mh::ui::formatBackgroundLine;
using mh::ui::parseBackgroundLine;
using mh::ui::recordBackgrounds;
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

TEST_CASE("the transform zooms about the centre and pans in source pixels", "[ui][backdrop]") {
    // Identity must reproduce the plain cover fit EXACTLY, or every existing
    // backdrop moves the day the transform lands.
    const QRectF plain = coverSource(QSize(100, 100), QSize(400, 200));
    const QRectF ident = coverSource(QSize(100, 100), QSize(400, 200), BackdropTransform{});
    CHECK(ident == plain);

    // scale 2 shows HALF as much of the image -- the picture gets bigger on
    // screen -- and stays centred on the same point.
    const QRectF in =
        coverSource(QSize(100, 100), QSize(400, 200), BackdropTransform{0.0F, 0.0F, 2.0F});
    CHECK(in.width() == plain.width() / 2.0);
    CHECK(in.height() == plain.height() / 2.0);
    CHECK(in.center().x() == plain.center().x());
    CHECK(in.center().y() == plain.center().y());

    // x is a fraction of the VISIBLE width, so a drag reads the same whatever
    // the image's pixel count. +0.5 moves the window half a window right.
    const QRectF panned =
        coverSource(QSize(100, 100), QSize(400, 200), BackdropTransform{0.5F, 0.0F, 1.0F});
    CHECK(panned.x() == plain.x() + plain.width() / 2.0);
    CHECK(panned.width() == plain.width());
}

TEST_CASE("the source rect is clamped inside the image, so viewport and render agree",
          "[ui][backdrop]") {
    // THE POINT OF THIS TEST: the viewport samples ClampToEdge and SMEARS past
    // the edge, while `QImage::copy` of an out-of-bounds rect fills TRANSPARENT
    // BLACK. Those disagree, so the rect never leaves the image in the first
    // place. Pan hard right and the window must stop at the image's edge.
    const QRectF far =
        coverSource(QSize(100, 100), QSize(400, 200), BackdropTransform{99.0F, 0.0F, 1.0F});
    CHECK(far.right() <= 400.0);
    CHECK(far.left() >= 0.0);
    CHECK(far.top() >= 0.0);
    CHECK(far.bottom() <= 200.0);
    // ...and it is still the same size, not squashed against the edge.
    CHECK(far.width() == coverSource(QSize(100, 100), QSize(400, 200)).width());

    // Zooming OUT past the image cannot show more than the image has.
    const QRectF out =
        coverSource(QSize(100, 100), QSize(400, 200), BackdropTransform{0.0F, 0.0F, 0.1F});
    CHECK(out.width() <= 400.0);
    CHECK(out.height() <= 200.0);
}

TEST_CASE("a scale of zero or less asks for nothing rather than an empty smear", "[ui][backdrop]") {
    // `SceneResources::draw` SKIPS an empty rect, so a zero scale would make
    // the backdrop vanish with no error. Refuse it at the one place that
    // computes the rect.
    CHECK(coverSource(QSize(100, 100), QSize(400, 200), BackdropTransform{0.0F, 0.0F, 0.0F})
              .isEmpty());
    CHECK(coverSource(QSize(100, 100), QSize(400, 200), BackdropTransform{0.0F, 0.0F, -1.0F})
              .isEmpty());
}

TEST_CASE("a degenerate size asks for nothing rather than dividing by zero", "[ui][backdrop]") {
    CHECK(coverSource(QSize(0, 100), QSize(400, 200)).isEmpty());
    CHECK(coverSource(QSize(100, 100), QSize(0, 0)).isEmpty());
}

TEST_CASE("a background line is parsed from the TAIL, so filenames may contain spaces",
          "[ui][backdrop]") {
    // The reference writes the filename unquoted in the middle of the line
    // (`0_modeling_background.py:439-445`), so splitting on whitespace and
    // taking token 2 as the path loses everything after the first space.
    // Token 1 is the side; the LAST FOUR are aspect, transX, transY, scale;
    // whatever is between them is the name.
    const auto spaced = parseBackgroundLine("background front my photo 2.png 1.5 0.25 -0.5 2");
    REQUIRE(spaced.has_value());
    CHECK(spaced->side == BackdropSide::Front);
    CHECK(spaced->file == "my photo 2.png");
    CHECK(spaced->aspect == 1.5F);
    CHECK(spaced->transform.x == 0.25F);
    CHECK(spaced->transform.y == -0.5F);
    CHECK(spaced->transform.scale == 2.0F);

    const auto plain = parseBackgroundLine("background left side.png 1 0 0 1");
    REQUIRE(plain.has_value());
    CHECK(plain->side == BackdropSide::Left);
    CHECK(plain->file == "side.png");
}

TEST_CASE("lines this port does not own are refused, so the caller can keep them verbatim",
          "[ui][backdrop]") {
    // `background enabled <bool>` is checked BEFORE any arity count -- it has
    // two tokens and would otherwise fall into the "too short" path and read
    // as a malformed placement rather than a different line entirely.
    CHECK_FALSE(parseBackgroundLine("background enabled True").has_value());

    // `other` is the reference's SEVENTH side, the three-quarter view, which
    // this port deliberately refuses. Returning nothing is what lets the
    // writer leave the line untouched instead of dropping it.
    CHECK_FALSE(parseBackgroundLine("background other photo.png 1 0 0 1").has_value());

    CHECK_FALSE(parseBackgroundLine("background front photo.png 1 0").has_value());
    CHECK_FALSE(parseBackgroundLine("modifier head/head-age 0.5").has_value());
    CHECK_FALSE(parseBackgroundLine("background").has_value());
    // A non-numeric tail is not a placement, however many tokens it has.
    CHECK_FALSE(parseBackgroundLine("background front photo.png a b c d").has_value());
}

TEST_CASE("a non-positive scale is clamped on parse, not trusted into invisibility",
          "[ui][backdrop]") {
    // A hand-edited or reference-written 0 would make the source rect empty,
    // and `SceneResources::draw` SKIPS an empty rect -- the backdrop would
    // simply not appear, with nothing to tell the user why.
    const auto zero = parseBackgroundLine("background front p.png 1 0 0 0");
    REQUIRE(zero.has_value());
    CHECK(zero->transform.scale > 0.0F);

    const auto neg = parseBackgroundLine("background front p.png 1 0 0 -3");
    REQUIRE(neg.has_value());
    CHECK(neg->transform.scale > 0.0F);
}

TEST_CASE("format and parse round-trip", "[ui][backdrop]") {
    BackdropPlacement p;
    p.side            = BackdropSide::Bottom;
    p.file            = "a file.png";
    p.aspect          = 1.25F;
    p.transform       = BackdropTransform{0.5F, -0.25F, 3.0F};
    const auto parsed = parseBackgroundLine(formatBackgroundLine(p));
    REQUIRE(parsed.has_value());
    CHECK(parsed->side == p.side);
    CHECK(parsed->file == p.file);
    CHECK(parsed->aspect == p.aspect);
    CHECK(parsed->transform.x == p.transform.x);
    CHECK(parsed->transform.y == p.transform.y);
    CHECK(parsed->transform.scale == p.transform.scale);
}

TEST_CASE("the writer keeps one line PER SIDE, which is what `recordLine` cannot do",
          "[ui][backdrop]") {
    // THE TRAP THIS EXISTS FOR: `recordLine` (main.cpp) erases EVERY line whose
    // first token matches the key and appends exactly one. Applied to
    // `background` it would collapse all six sides into a single line. The
    // writer owns lines by SIDE, not by key.
    std::vector<std::string> unhandled;
    std::vector<BackdropPlacement> six;
    for (int i = 0; i < 6; ++i) {
        BackdropPlacement p;
        p.side = static_cast<BackdropSide>(i);
        p.file = "s" + std::to_string(i) + ".png";
        six.push_back(p);
    }
    recordBackgrounds(unhandled, six);

    int backgrounds = 0;
    for (const std::string& l : unhandled) {
        if (l.rfind("background ", 0) == 0) ++backgrounds;
    }
    CHECK(backgrounds == 6);

    // ...and writing again REPLACES rather than appends, or a save-load-save
    // grows the file without bound.
    recordBackgrounds(unhandled, six);
    backgrounds = 0;
    for (const std::string& l : unhandled) {
        if (l.rfind("background ", 0) == 0) ++backgrounds;
    }
    CHECK(backgrounds == 6);
}

TEST_CASE("lines the writer does not own survive it verbatim", "[ui][backdrop]") {
    // A reference file's three-quarter `other` and its `enabled` flag are not
    // ours to rewrite, and dropping them would lose the user's data on a round
    // trip through this port.
    std::vector<std::string> unhandled{
        "skeleton default.mhskel",
        "background other some photo.png 1 0 0 1",
        "background enabled True",
        "material skin.mhmat",
    };
    const std::vector<std::string> before = unhandled;

    BackdropPlacement front;
    front.side = BackdropSide::Front;
    front.file = "f.png";
    recordBackgrounds(unhandled, {front});

    for (const std::string& kept : before) {
        CHECK(std::find(unhandled.begin(), unhandled.end(), kept) != unhandled.end());
    }
    // The non-background lines keep their relative order.
    const auto skel = std::find(unhandled.begin(), unhandled.end(), "skeleton default.mhskel");
    const auto mat  = std::find(unhandled.begin(), unhandled.end(), "material skin.mhmat");
    CHECK(skel < mat);
}

TEST_CASE("an owned line is replaced, not duplicated", "[ui][backdrop]") {
    std::vector<std::string> unhandled{"background front old.png 1 0 0 1"};
    BackdropPlacement p;
    p.side = BackdropSide::Front;
    p.file = "new.png";
    recordBackgrounds(unhandled, {p});

    int fronts = 0;
    for (const std::string& l : unhandled) {
        if (l.rfind("background front ", 0) == 0) ++fronts;
    }
    CHECK(fronts == 1);
    CHECK(unhandled.front().find("new.png") != std::string::npos);
}

TEST_CASE("writing no placements clears the lines this port owns and only those",
          "[ui][backdrop]") {
    std::vector<std::string> unhandled{
        "background front f.png 1 0 0 1",
        "background other keep.png 1 0 0 1",
    };
    recordBackgrounds(unhandled, {});
    CHECK(unhandled.size() == 1);
    CHECK(unhandled.front() == "background other keep.png 1 0 0 1");
}

TEST_CASE("the render's default camera faces FRONT, which is why --render composites at all",
          "[ui][backdrop]") {
    // A PINNING test: it passes today and is expected to. `--render` picks
    // which backdrop to composite from its OWN camera rather than from
    // `--background-side` (which it refuses), and that only works while the
    // default render camera actually faces a side.
    //
    // If someone gives `RenderSettings` a different default camera -- or
    // `renderSettingsFor` starts setting one -- the production render would
    // quietly stop compositing backdrops, because `sideFacing` would answer
    // "no side" for a three-quarter view. This says so out loud instead.
    const mh::render::Camera fresh;
    CHECK(fresh.yawDegrees == 0.0F);
    CHECK(fresh.pitchDegrees == 0.0F);
    const auto side = sideFacing(fresh.yawDegrees, fresh.pitchDegrees);
    REQUIRE(side.has_value());
    CHECK(*side == BackdropSide::Front);
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
