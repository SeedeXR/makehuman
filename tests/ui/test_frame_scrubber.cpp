// SPDX-License-Identifier: Apache-2.0
//
// The frame scrubber: the port of the reference's `AnimationLibrary`
// (`legacy/python/plugins/3_libraries_animation.py:48`).
//
// Every assertion here drives the WIDGET -- clicking its buttons, moving its
// slider -- rather than calling a helper that happens to compute the same
// thing. The four state bugs that sank this port's first Animation chooser
// were all of the form "the control says one thing and the model holds
// another", and only a test that reads the control can see that.
#include "makehuman/ui/FrameScrubber.h"

#include <catch2/catch_test_macros.hpp>

#include <QAbstractButton>
#include <QLabel>
#include <QList>
#include <QSlider>

using mh::ui::FrameScrubber;

namespace {

QAbstractButton* button(FrameScrubber& s, const char* name) {
    auto* b = s.findChild<QAbstractButton*>(QLatin1String(name));
    REQUIRE(b != nullptr);
    return b;
}

/// Counts `frameChosen` and remembers every frame asked for.
///
/// A lambda rather than QSignalSpy, for the reason `test_material_panel.cpp`
/// gives at :181 -- that class lives in Qt6::Test, which `mh_ui_tests` does
/// not link.
struct Asked {
    QList<int> frames;

    explicit Asked(FrameScrubber& s) {
        QObject::connect(&s, &FrameScrubber::frameChosen, &s, [this](int f) { frames.append(f); });
    }

    [[nodiscard]] int count() const { return static_cast<int>(frames.size()); }
};

QSlider* slider(FrameScrubber& s) {
    auto* w = s.findChild<QSlider*>(QStringLiteral("scrub.slider"));
    REQUIRE(w != nullptr);
    return w;
}

}  // namespace

TEST_CASE("the scrubber is dead until an animation has more than one frame", "[scrubber]") {
    FrameScrubber s;

    // The state the window opens in: a rest pose, no animation.
    CHECK_FALSE(s.isScrubbable());
    CHECK(s.status() == QStringLiteral("No animation loaded"));

    // dance1 -- MEASURED at 1 frame. A slider over one frame is a control with
    // nothing to do, and the reference disables it for exactly this case
    // (`3_libraries_animation.py:164`).
    s.setAnimation(1);
    CHECK_FALSE(s.isScrubbable());
    CHECK(s.status() == QStringLiteral("Only one frame available"));

    // walk1 -- MEASURED at 14 frames.
    s.setAnimation(14);
    CHECK(s.isScrubbable());
    CHECK(s.status() == QStringLiteral("14 frames available"));
    // 14 frames are 0..13. An off-by-one here would put the last frame out of
    // reach, or offer a frame the file does not have.
    CHECK(slider(s)->maximum() == 13);
}

TEST_CASE("the transport buttons clamp at both ends", "[scrubber]") {
    FrameScrubber s;
    s.setAnimation(14);
    Asked asked(s);

    button(s, "scrub.last")->click();
    CHECK(s.frame() == 13);
    button(s, "scrub.forward")->click();
    CHECK(s.frame() == 13);  // not 14: the file has no frame 14

    button(s, "scrub.back")->click();
    CHECK(s.frame() == 12);
    button(s, "scrub.first")->click();
    CHECK(s.frame() == 0);
    button(s, "scrub.back")->click();
    CHECK(s.frame() == 0);  // not -1

    // FIVE clicks, THREE reload requests: the two clamped ones did not move
    // the slider and so asked for nothing. A button that re-requested the
    // frame already shown would re-read the .bvh and refit the skeleton for no
    // change on screen.
    CHECK(asked.count() == 3);
    CHECK(asked.frames == QList<int>{13, 12, 0});
}

TEST_CASE("a shorter animation resets to frame 0 rather than clamping", "[scrubber]") {
    FrameScrubber s;
    // zombieWalk1 -- MEASURED at 31 frames.
    s.setAnimation(31);
    button(s, "scrub.last")->click();
    REQUIRE(s.frame() == 30);

    Asked asked(s);
    s.setAnimation(14);
    // 13 would be in range and would be WRONG: the user never chose the last
    // frame of the new file. The reference resets the same way (`:154`).
    CHECK(s.frame() == 0);
    CHECK(slider(s)->maximum() == 13);
    // And it asks for nothing: `setAnimation` reports a load that has already
    // happened. Emitting here would ask the app to reload what it just loaded.
    CHECK(asked.count() == 0);
}

TEST_CASE("setFrame puts the slider back without asking for a reload", "[scrubber]") {
    FrameScrubber s;
    s.setAnimation(14);
    Asked asked(s);

    s.setFrame(7);
    CHECK(s.frame() == 7);
    CHECK(asked.count() == 0);

    // Out of range leaves the previous value alone, rather than clamping to
    // something nobody asked for.
    s.setFrame(99);
    CHECK(s.frame() == 7);
    s.setFrame(-1);
    CHECK(s.frame() == 7);
    CHECK(asked.count() == 0);
}

TEST_CASE("the frame label follows every path that moves the slider", "[scrubber]") {
    FrameScrubber s;
    auto* label = s.findChild<QLabel*>(QStringLiteral("scrub.frame"));
    REQUIRE(label != nullptr);

    s.setAnimation(14);
    CHECK(label->text() == QStringLiteral("Frame: 0"));

    button(s, "scrub.last")->click();
    CHECK(label->text() == QStringLiteral("Frame: 13"));

    // The silent path. `setFrame` suppresses the SIGNAL, not the label -- a
    // blocked slider left `AssetPanel`'s checkbox showing the previous answer,
    // and this is the same trap one widget over.
    s.setFrame(4);
    CHECK(label->text() == QStringLiteral("Frame: 4"));

    // And the reset path.
    s.setAnimation(0);
    CHECK(label->text() == QStringLiteral("Frame: 0"));
}
