// SPDX-License-Identifier: Apache-2.0
#include "makehuman/ui/Backdrop.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <sstream>
#include <string_view>
#include <vector>

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

QRectF coverSource(QSize frame, QSize image, BackdropTransform transform) {
    if (frame.width() <= 0 || frame.height() <= 0 || image.width() <= 0 || image.height() <= 0) {
        return {};
    }
    const double fw = frame.width();
    const double fh = frame.height();
    const double iw = image.width();
    const double ih = image.height();

    // Compare aspects by cross-multiplying: `iw/ih > fw/fh` with no division,
    // so an image one pixel tall cannot produce an infinity here.
    // Refused, not clamped: an empty rect is SKIPPED by `SceneResources::draw`,
    // so a zero scale would delete the backdrop silently. Saying "nothing" here
    // is the same answer a degenerate size gets, two lines up.
    if (!(transform.scale > 0.0F)) return {};

    QRectF fit;
    if (iw * fh > fw * ih) {
        // Wider than the frame: keep the full height, crop the width.
        const double want = ih * fw / fh;
        fit               = {(iw - want) / 2.0, 0.0, want, ih};
    } else {
        // Taller than the frame, or the same aspect -- in which case `want`
        // comes out as `ih` exactly and this is a no-op, which is the point.
        const double want = iw * fh / fw;
        fit               = {0.0, (ih - want) / 2.0, iw, want};
    }

    // Zoom about the CENTRE of the fit, not its corner, so scaling does not
    // also slide the image sideways.
    const double zoom = static_cast<double>(transform.scale);
    const QPointF mid = fit.center();
    QRectF out(0.0, 0.0, fit.width() / zoom, fit.height() / zoom);
    out.moveCenter(mid);

    // Pan in units of the VISIBLE width, so the number means the same thing at
    // any resolution -- see the header.
    out.translate(out.width() * static_cast<double>(transform.x),
                  out.height() * static_cast<double>(transform.y));

    // Never show anything the image does not have. Shrink to fit FIRST -- a
    // window wider than the image cannot be satisfied by sliding it -- then
    // slide whatever is left back inside.
    if (out.width() > iw) out.setWidth(iw);
    if (out.height() > ih) out.setHeight(ih);
    if (out.left() < 0.0) out.moveLeft(0.0);
    if (out.top() < 0.0) out.moveTop(0.0);
    if (out.right() > iw) out.moveRight(iw);
    if (out.bottom() > ih) out.moveBottom(ih);
    return out;
}

namespace {

/// The six names this port owns, in enum order. `other` is absent on purpose.
constexpr std::array<std::string_view, 6> kSideNames{"front", "back", "left",
                                                     "right", "top",  "bottom"};

std::optional<BackdropSide> sideNamed(std::string_view name) {
    for (std::size_t i = 0; i < kSideNames.size(); ++i) {
        if (kSideNames[i] == name) return static_cast<BackdropSide>(i);
    }
    return std::nullopt;
}

/// Strict float: the WHOLE token must be a number.
///
/// `std::stof` would accept "1abc" and `atof` returns 0 for junk, which would
/// turn a malformed line into a placement at the origin rather than a line
/// this port leaves alone.
std::optional<float> wholeFloat(const std::string& token) {
    if (token.empty()) return std::nullopt;
    char* end      = nullptr;
    const double v = std::strtod(token.c_str(), &end);
    if (end != token.c_str() + token.size()) return std::nullopt;
    if (!std::isfinite(v)) return std::nullopt;
    return static_cast<float>(v);
}

}  // namespace

std::optional<BackdropPlacement> parseBackgroundLine(const std::string& line) {
    std::istringstream in(line);
    std::vector<std::string> tok;
    for (std::string t; in >> t;)
        tok.push_back(t);

    if (tok.size() < 2 || tok[0] != "background") return std::nullopt;

    // side + at least one filename token + the four numbers.
    if (tok.size() < 7) return std::nullopt;

    // Rejects `other` (the reference's three-quarter view) AND `enabled`, so
    // `background enabled True` needs no special case: it is simply not one of
    // the six names. The plan called for an explicit `enabled` branch ahead of
    // the arity check; MEASURED, that mutant survives -- no input reaches it --
    // so it is not here.
    const auto side = sideNamed(tok[1]);
    if (!side) return std::nullopt;

    const std::size_t tail = tok.size() - 4;
    const auto aspect      = wholeFloat(tok[tail]);
    const auto tx          = wholeFloat(tok[tail + 1]);
    const auto ty          = wholeFloat(tok[tail + 2]);
    const auto scale       = wholeFloat(tok[tail + 3]);
    if (!aspect || !tx || !ty || !scale) return std::nullopt;

    BackdropPlacement out;
    out.side = *side;
    // Join the middle back together with single spaces. The reference wrote it
    // with single spaces, so this is lossless for anything it produced.
    for (std::size_t i = 2; i < tail; ++i) {
        if (i > 2) out.file += ' ';
        out.file += tok[i];
    }
    out.aspect          = *aspect;
    out.transform.x     = *tx;
    out.transform.y     = *ty;
    out.transform.scale = std::max(*scale, kMinBackdropScale);
    return out;
}

std::string formatBackgroundLine(const BackdropPlacement& placement) {
    std::ostringstream out;
    out << "background " << kSideNames[static_cast<std::size_t>(placement.side)] << ' '
        << placement.file << ' ' << placement.aspect << ' ' << placement.transform.x << ' '
        << placement.transform.y << ' ' << placement.transform.scale;
    return out.str();
}

void recordBackgrounds(std::vector<std::string>& unhandled,
                       const std::vector<BackdropPlacement>& placements) {
    // Erase only what we own. `parseBackgroundLine` returns nothing for a line
    // that is not one of our six sides, and that nothing is the instruction to
    // leave it alone -- `background other ...` and `background enabled ...`
    // both survive here untouched.
    std::erase_if(unhandled,
                  [](const std::string& line) { return parseBackgroundLine(line).has_value(); });

    for (const BackdropPlacement& p : placements)
        unhandled.push_back(formatBackgroundLine(p));
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
