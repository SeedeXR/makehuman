// SPDX-License-Identifier: Apache-2.0
//
// Microsoft MoGe, run through ONNX Runtime, for the one thing it does that we
// cannot: separate a subject from the background of a REAL PHOTOGRAPH.
//
// WHAT `mask` MEANS, AND WHAT IT DOES NOT. It marks where the model's GEOMETRY
// PREDICTION IS VALID. It is **not** a subject-versus-background segmentation,
// and this module was very nearly built on the belief that it was.
//
// The belief came from one observation: on our own render -- a figure on a flat
// black clear colour -- the mask covers 8.3% of the frame and traces the body
// exactly. That looks like segmentation. It is not. The mask was excluding the
// VOID, which has no geometry to predict, not finding the person.
//
// MEASURED on a cluttered scene, against a known ground-truth subject:
//
//     MoGe mask > 0.5        coverage 0.78   IoU vs truth 0.154
//     best relative-depth threshold          IoU 0.177
//     ui::silhouetteOf's corner rule         IoU 0.159
//
// All three are poor, and MoGe is **no better than the corner rule it was
// supposed to rescue**. That fixture was a collage of rectangles rather than a
// photograph -- but the doubt it left has since been settled the other way: on
// real Commons photographs OF PEOPLE the mask reports a coverage of **1.000**,
// the whole frame, three times out of four. It is not a person segmenter on
// real input either. `test_moge.cpp` pins this so the claim cannot quietly
// come back.
//
// METRIC DEPTH IS NOW IMPLEMENTED, AND MEASURED, AND YOU SHOULD READ THIS
// BEFORE BELIEVING IT.
//
// The model's `z` is affine -- off by an unknown focal and an unknown additive
// shift -- so `foundation::recoverFocalShift` solves for both and `depth`
// below is the result. That solve is validated against cameras we CHOSE, in
// CI, with no model: it recovers them to within 1-3% (`test_focal_shift.cpp`).
//
// What is NOT reliable is the model's underlying prediction on the inputs this
// project produces. Rendered at a known 30-degree vertical field of view, the
// recovered answer is 41.5 degrees. Cropping the same render to narrow the
// true field of view shows what is happening:
//
//     true fov   30.0   21.3   15.3   10.7
//     recovered  45.6   38.8   31.8   23.9
//
// It TRACKS -- the estimate falls monotonically as the real field of view
// falls, which is how we know the solve is reading the image rather than
// inventing a number -- but it is biased high throughout, and worse the
// narrower the lens. (Those figures are a Lanczos crop series; the ctest does
// its own resampling and measures 41.0 falling to 33.8, which is the same
// story. The test asserts only the FALL, never the value, so it does not go
// red the day the model improves.) A subject floating on a flat background offers no scene
// cues, so the model falls back toward a normal-lens prior.
//
// IT WAS THEN MEASURED ON REAL PHOTOGRAPHS, AND THEY ARE WORSE, NOT BETTER.
//
// The obvious objection to the figures above is that a body in a void is the
// worst case for monocular camera estimation, and a real photograph has floors
// and walls to reason from. So: Wikimedia Commons photographs from full-frame
// bodies, where EXIF focal length gives the TRUE vertical field of view
// exactly. Scenes first --
//
//     true  4.6   17.5   22.6   61.9
//     MoGe 21.0   26.2   33.8   50.7
//
// -- and then photographs of PEOPLE, which is the actual use case:
//
//     true  5.2    5.2    5.2   21.6
//     MoGe 25.5   31.4   25.5   28.3
//
// Errors of +31% to +510%. The model regresses hard toward a normal lens: it
// overestimates every long lens and underestimates the wide one. Two of the
// real photographs are WORSE than the 30-degree render was.
//
// What +31% costs, at the most favourable real measurement: the recovered
// focal is low by a factor of 1.32, and inferred size scales with focal, so a
// person 1.75 m tall measures about 1.32 m.
//
// **So metric depth from this is NOT trustworthy as an absolute stature.** It
// is not wired into `fit_to_references` and must not be. `metric` below means
// RECOVERED, not ACCURATE. The hypothesis that real photographs would rescue
// it has been tested and refused, not left hanging.
//
// (Those photographs are CC BY-SA. They were measured locally and are NOT
// committed and NOT redistributed; the method is written down here so the
// measurement can be repeated rather than taken on trust.)
//
// LICENCE. MoGe's code and weights are MIT (LICENSING.md 5.2c), chosen by the
// owner with the training-data concern recorded there rather than waved away.
// ONNX Runtime is MIT. This module is Apache-2.0 so the Apache side may use it;
// it never includes a core header.
//
// THE MODEL IS NOT SHIPPED. 134 MB against a 152 MB `data/` tree would roughly
// double the distribution for something most characters never use. It is
// fetched by `tools/fetch_moge.sh` into a cache outside the repository, and
// everything here degrades to "unavailable" when it is absent rather than
// failing.
#pragma once

#include <QImage>

#include <expected>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace mh::moge {

/// What one forward pass yields.
struct Prediction {
    /// Grayscale8 at the SOURCE image's size, 255 where the model's geometry
    /// prediction is VALID -- which is not the same as where the subject is.
    /// See the header note above; it is measured, not supposed.
    QImage mask;

    /// The graph's fourth output.
    ///
    /// **Named `scale`, not `metric_scale`.** The documentation calls it the
    /// latter; the graph itself calls it the former, which is what a probe of
    /// the real model reported. Carried through unused for now: it only means
    /// something after the focal/shift recovery this module does not do.
    float scale{0.0F};

    /// Fraction of the frame the mask claims. On a scene with real content this
    /// runs close to 1.0 and that is CORRECT behaviour, not a failure: almost
    /// all of a real scene has valid geometry.
    double coverage{0.0};

    /// Metric depth, `depthWidth * depthHeight`, row-major, 0 where masked out.
    ///
    /// At the MODEL's working resolution, not the caller's. Depth is a float
    /// field and resampling it to flatter a caller would blur the exact edges
    /// the solve depends on; whoever wants it at another size can say how.
    std::vector<float> depth;
    int depthWidth{0};
    int depthHeight{0};

    /// What `recoverFocalShift` found, and whether it found it.
    ///
    /// **`metric` is the flag that matters.** The model's `z` is affine: on its
    /// own it is a plausible, smooth, unscaled surface that looks right in
    /// every visualisation and is wrong by a constant. `depth` above is only
    /// metric when this is true; when it is false the field is left empty
    /// rather than filled with something that would be believed.
    bool metric{false};
    float focal{0.0F};
    float shift{0.0F};
    /// Vertical field of view implied by the recovered focal, in degrees.
    float fovDegrees{0.0F};
};

/// A loaded model. Expensive to create, cheap to run; keep one.
class Session {
public:
    ~Session();
    Session(Session&&) noexcept;
    Session& operator=(Session&&) noexcept;
    Session(const Session&)            = delete;
    Session& operator=(const Session&) = delete;

    /// Loads @p model, or says why it could not.
    [[nodiscard]] static std::expected<Session, std::string> open(
        const std::filesystem::path& model);

    /// Runs one image.
    ///
    /// @param tokens ViT tokens. MoGe suggests 1200..2500; more is sharper and
    ///        slower. The default is the middle of that range.
    [[nodiscard]] std::expected<Prediction, std::string> run(const QImage& image,
                                                             int tokens = 1800);

private:
    Session();
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

/// Where the model is looked for: `$MH_MOGE_MODEL` if set, else the user's
/// cache directory. Returns an empty path when neither can be determined.
[[nodiscard]] std::filesystem::path defaultModelPath();

/// Whether a model file is actually present. Tests SKIP on false rather than
/// fail: a 134 MB download is not a build dependency.
[[nodiscard]] bool modelAvailable();

}  // namespace mh::moge
