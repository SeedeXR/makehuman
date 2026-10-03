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
// supposed to rescue**. The fixture was a collage of coloured rectangles
// rather than a photograph, so this does not prove MoGe fails on real photos
// either -- it proves only that the claim was never evidenced. Nothing here
// should be wired into a scoring path until it is measured on real
// photographs, and `test_moge.cpp` pins these numbers so the claim cannot
// quietly come back.
//
// WHAT IT IS ACTUALLY FOR, THEN. Geometry: a point map, normals, and -- after
// a focal/shift recovery that is NOT in the exported graph -- metric depth.
// Metric scale is the one thing `fit_to_references` documents it cannot have,
// because a photograph does not carry an absolute stature. That is the reason
// to finish this, and it is not finished.
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
