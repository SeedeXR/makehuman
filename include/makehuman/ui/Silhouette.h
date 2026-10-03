// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <QImage>
#include <QRect>

#include <cstdint>

namespace mh::ui {

/// The subject's outline, separated from its background.
///
/// **Why a silhouette and not a pixel difference.** Comparing a render against
/// a REFERENCE PHOTOGRAPH, which is the job, a per-pixel metric answers the
/// wrong question: the photograph has different lighting, different skin, a
/// different background and a different framing, so PSNR between the two is
/// near zero whether the body is right or wrong. It would score a perfect fit
/// and a hopeless one about the same. The outline is the part a parametric body
/// can actually be fitted to.
struct Silhouette {
    /// Grayscale, 255 where the subject is and 0 where the background is.
    QImage mask;
    /// Tight box around the subject. Null when nothing was found.
    QRect bounds;
    /// Subject pixels.
    int64_t area{0};
    /// Subject pixels as a fraction of the image.
    ///
    /// **Report this, do not hide it.** It is how a caller sees that the
    /// separation FAILED rather than that the character is wrong: a cluttered
    /// photograph whose background does not match its own corner comes back
    /// almost entirely "subject", and a fit against that mask would chase
    /// furniture. A value near 1 means the mask is meaningless.
    double coverage{0.0};
};

/// Separates subject from background by colour distance from pixel (0, 0).
///
/// The same assumption `describeFrame` makes, and the same limit: a frame whose
/// top-left corner is part of the subject measures the subject as background.
/// It holds for our renders, whose background is a known flat clear colour, and
/// for a photograph shot against a plain backdrop. It does not hold for a snap
/// taken in a kitchen, and nothing here pretends otherwise -- `coverage` is
/// what says so.
///
/// @param tolerance per-channel distance, summed over RGB, still counted as
///        background. 0 demands an exact match, which is right for a render and
///        wrong for a photograph's sensor noise and JPEG ringing.
[[nodiscard]] Silhouette silhouetteOf(const QImage& image, int tolerance = 30);

/// How closely two outlines agree.
struct SilhouetteMatch {
    /// Intersection over union, after each outline is scaled to a common box.
    ///
    /// 1.0 is identical, 0.0 is no overlap at all. SCALE-INVARIANT on purpose:
    /// a reference photograph is taken at an unknown distance, so a shape that
    /// is right but framed differently must not score as a miss. What that
    /// discards is absolute size -- which is why the ratios below are reported
    /// separately rather than folded in.
    double iou{0.0};
    /// Proportions BEFORE alignment: b's box over a's. Both 1.0 means the two
    /// were framed alike. These carry the information `iou` deliberately threw
    /// away, so a caller can tell "wrong shape" from "same shape, closer lens".
    double widthRatio{0.0};
    double heightRatio{0.0};
    /// Aspect ratio of each box, which survives scaling and so is the one size
    /// cue that IS comparable between a render and a photograph.
    double aspectA{0.0};
    double aspectB{0.0};
    int64_t intersection{0};
    int64_t onlyA{0};
    int64_t onlyB{0};
};

/// Compares two outlines, scale- and position-independently.
///
/// Returns a zeroed match when either silhouette is empty; the caller decides
/// whether that is a failed render or a failed separation, because only the
/// caller knows which image came from where.
[[nodiscard]] SilhouetteMatch compareSilhouettes(const Silhouette& a, const Silhouette& b);

}  // namespace mh::ui
