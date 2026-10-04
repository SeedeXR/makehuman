// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <cstdint>
#include <span>

namespace mh::foundation {

/// The camera a monocular depth model's point map implies.
///
/// A model like MoGe predicts geometry only up to an **affine** ambiguity: it
/// gets the shape right but not where the camera was, so its `z` is off by an
/// unknown additive `shift` and its `xy` is scaled by an unknown `focal`. The
/// model cannot resolve them -- nothing in a single image does -- but the point
/// map is over-determined, so they can be recovered from it.
///
/// **THIS IS WHY METRIC DEPTH IS NOT JUST THE THIRD CHANNEL.** Reading `z`
/// straight out of the model gives a plausible, smoothly varying, completely
/// unscaled surface. It looks correct in every visualisation and is wrong by a
/// constant nobody can see.
struct FocalShiftResult {
    /// In DIAGONAL-NORMALISED units, matching the `uv` convention below --
    /// not pixels, and not a 35mm equivalent.
    float focal{1.0F};
    /// Added to the model's `z` to get true depth.
    float shift{0.0F};
    /// False when there was too little to solve from, in which case the
    /// identity above is returned rather than a guess.
    bool recovered{false};
};

/// Recovers focal and shift from an affine point map.
///
/// Solves `min_shift |focal * xy / (z + shift) - uv|`, with `focal` in closed
/// form at each step, exactly as MoGe's `recover_focal_shift` does. `uv` is
/// MoGe's convention: normalised by the image DIAGONAL, so the corners sit at
/// `(±w/d, ±h/d)`. Getting that convention wrong does not fail, it rescales --
/// which is the kind of error that reaches production.
///
/// **One deliberate difference from the reference.** MoGe runs
/// Levenberg-Marquardt from `shift = 0`; this runs a bracketed golden-section
/// search. The objective has a pole wherever `z + shift` crosses zero, and an
/// unbracketed solver started next to it can step across and converge on
/// nonsense. A bracket that keeps `z + shift` strictly positive cannot. The
/// minimum is the same; only the route to it is guarded.
///
/// **A fronto-parallel scene is genuinely degenerate**, not a bug: when every
/// point is at the same depth, focal and shift trade off exactly and no
/// algorithm can separate them. `recovered` is still true -- the fit is
/// perfect -- so a caller that needs to know asks whether the depth varies.
///
/// @param points `height * width * 3`, row-major, xyz interleaved.
/// @param mask   optional `height * width`; non-zero means use this pixel.
[[nodiscard]] FocalShiftResult recoverFocalShift(std::span<const float> points, int width,
                                                 int height, std::span<const uint8_t> mask = {});

/// The vertical field of view a diagonal-normalised focal implies, in degrees.
///
/// Separate from the struct because it needs the aspect ratio, which the focal
/// alone does not carry.
[[nodiscard]] float verticalFovDegrees(float focal, int width, int height);

}  // namespace mh::foundation
