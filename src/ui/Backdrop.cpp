// SPDX-License-Identifier: Apache-2.0
#include "makehuman/ui/Backdrop.h"

#include <cmath>

namespace mh::ui {

namespace {

/// Yaw folded into (-180, 180], so an accumulated drag compares like a heading.
float wrapDegrees(float deg) {
    const float turned = std::fmod(deg + 180.0F, 360.0F);
    return (turned < 0.0F ? turned + 360.0F : turned) - 180.0F;
}

bool near(float a, float b) {
    return std::abs(wrapDegrees(a - b)) < kFacingToleranceDegrees;
}

}  // namespace

QRectF coverSource(QSize frame, QSize image) {
    if (frame.width() <= 0 || frame.height() <= 0 || image.width() <= 0 || image.height() <= 0) {
        return {};
    }
    const double fw = frame.width();
    const double fh = frame.height();
    const double iw = image.width();
    const double ih = image.height();

    // Compare aspects by cross-multiplying: `iw/ih > fw/fh` with no division,
    // so an image one pixel tall cannot produce an infinity here.
    if (iw * fh > fw * ih) {
        // Wider than the frame: keep the full height, crop the width.
        const double want = ih * fw / fh;
        return {(iw - want) / 2.0, 0.0, want, ih};
    }
    // Taller than the frame, or the same aspect -- in which case `want` comes
    // out as `ih` exactly and this is a no-op, which is the point.
    const double want = iw * fh / fw;
    return {0.0, (ih - want) / 2.0, iw, want};
}

bool facingSide(BackdropSide side, float yawDegrees, float pitchDegrees) {
    // Pitch decides FIRST. Top and bottom are the views reached by pitching,
    // and while the camera is looking down, no side is being faced at all --
    // so a front photo must not hang behind a top view just because the yaw
    // happens to still read zero.
    const bool lookingDown = pitchDegrees > kFacingToleranceDegrees;
    const bool lookingUp   = pitchDegrees < -kFacingToleranceDegrees;
    if (side == BackdropSide::Top) return lookingDown;
    if (side == BackdropSide::Bottom) return lookingUp;
    if (lookingDown || lookingUp) return false;

    switch (side) {
        case BackdropSide::Front: return near(yawDegrees, 0.0F);
        case BackdropSide::Back: return near(yawDegrees, 180.0F);
        case BackdropSide::Right: return near(yawDegrees, 90.0F);
        case BackdropSide::Left: return near(yawDegrees, -90.0F);
        case BackdropSide::Top:
        case BackdropSide::Bottom: break;  // handled above
    }
    return false;
}

std::optional<BackdropSide> sideFacing(float yawDegrees, float pitchDegrees) {
    // Asks `facingSide` rather than repeating its angles, so the two cannot
    // disagree about where a side begins.
    for (const BackdropSide side : {BackdropSide::Front, BackdropSide::Back, BackdropSide::Left,
                                    BackdropSide::Right, BackdropSide::Top, BackdropSide::Bottom}) {
        if (facingSide(side, yawDegrees, pitchDegrees)) return side;
    }
    return std::nullopt;
}

}  // namespace mh::ui
