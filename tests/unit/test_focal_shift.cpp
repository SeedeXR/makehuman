// SPDX-License-Identifier: Apache-2.0
//
// The affine-ambiguity solve, tested against a camera whose focal and shift we
// CHOSE. No model, no ONNX, no 134 MB download -- the point map is generated
// from a known pinhole camera, so the right answer is known exactly rather
// than compared against another implementation.
//
// That matters more than it sounds. A depth map recovered with the wrong focal
// is smooth, plausible, and wrong by a constant; every visualisation of it
// looks fine. Matching MoGe's Python output would only prove we agree with
// MoGe. Recovering numbers we picked proves the maths.
#include "makehuman/foundation/FocalShift.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <numbers>
#include <vector>

using namespace mh::foundation;
using Catch::Approx;

namespace {

constexpr int kW = 96;
constexpr int kH = 128;

/// Builds the point map a model would emit for a scene at known depth.
///
/// The pinhole relation is `uv = focal * xy / depth`, so a point at pixel
/// (x, y) with true depth Z has `xy = uv * Z / focal`. A model that is off by
/// an additive `shift` reports `z = Z - shift`, which is exactly what is handed
/// to the solver -- so recovering `shift` and `focal` means recovering what
/// went in.
std::vector<float> pointMap(float focalF, float shiftF, auto depthAt, int w = kW, int h = kH) {
    const auto focal    = static_cast<double>(focalF);
    const auto shift    = static_cast<double>(shiftF);
    const double aspect = static_cast<double>(w) / h;
    const double norm   = std::sqrt(1.0 + aspect * aspect);
    const double spanX  = aspect / norm;
    const double spanY  = 1.0 / norm;

    std::vector<float> points(static_cast<size_t>(w) * static_cast<size_t>(h) * 3);
    const auto axis = [](int index, int count, double span) {
        const double extent = span * (count - 1) / count;
        return -extent + 2.0 * extent * index / (count - 1);
    };
    for (int y = 0; y < h; ++y) {
        const double v = axis(y, h, spanY);
        for (int x = 0; x < w; ++x) {
            const double u = axis(x, w, spanX);
            const double z = depthAt(u, v);
            const size_t at =
                (static_cast<size_t>(y) * static_cast<size_t>(w) + static_cast<size_t>(x)) * 3;
            points[at + 0] = static_cast<float>(u * z / focal);
            points[at + 1] = static_cast<float>(v * z / focal);
            points[at + 2] = static_cast<float>(z - shift);
        }
    }
    return points;
}

/// Depth that actually varies, which the solve needs -- see the degenerate case.
auto tilted = [](double u, double v) { return 3.0 + 1.4 * v + 0.6 * u; };

}  // namespace

TEST_CASE("focal and shift are recovered from a known camera", "[focalshift]") {
    const float focal = 1.1F;
    const float shift = 0.45F;
    const auto points = pointMap(focal, shift, tilted);

    const FocalShiftResult r = recoverFocalShift(points, kW, kH);
    REQUIRE(r.recovered);
    CHECK(r.focal == Approx(focal).epsilon(0.01));
    CHECK(r.shift == Approx(shift).epsilon(0.02));
}

TEST_CASE("it recovers a different camera too", "[focalshift]") {
    // One lucky pair proves nothing; a wide lens and a negative shift exercise
    // the opposite end of both.
    const float focal = 0.55F;
    const float shift = -0.30F;
    const auto points = pointMap(focal, shift, tilted);

    const FocalShiftResult r = recoverFocalShift(points, kW, kH);
    REQUIRE(r.recovered);
    CHECK(r.focal == Approx(focal).epsilon(0.02));
    CHECK(r.shift == Approx(shift).epsilon(0.05));
}

TEST_CASE("a curved surface is recovered as well as a flat one", "[focalshift]") {
    const float focal = 0.9F;
    const float shift = 0.2F;
    const auto points = pointMap(focal, shift, [](double u, double v) {
        return 2.5 + 0.8 * std::sqrt(std::max(0.0, 1.0 - u * u - v * v));
    });

    const FocalShiftResult r = recoverFocalShift(points, kW, kH);
    REQUIRE(r.recovered);
    CHECK(r.focal == Approx(focal).epsilon(0.03));
    CHECK(r.shift == Approx(shift).epsilon(0.08));
}

TEST_CASE("the recovered focal gives back the field of view", "[focalshift]") {
    // What a caller actually wants. A 30-degree vertical field of view is what
    // this project's own renderer uses, so this is the number the end-to-end
    // check is compared against.
    const double spanY = 1.0 / std::sqrt(1.0 + std::pow(static_cast<double>(kW) / kH, 2.0));
    const auto focal   = static_cast<float>(spanY / std::tan(15.0 * std::numbers::pi / 180.0));
    CHECK(verticalFovDegrees(focal, kW, kH) == Approx(30.0).epsilon(0.001));

    const auto points        = pointMap(focal, 0.35F, tilted);
    const FocalShiftResult r = recoverFocalShift(points, kW, kH);
    REQUIRE(r.recovered);
    CHECK(verticalFovDegrees(r.focal, kW, kH) == Approx(30.0).epsilon(0.02));
}

TEST_CASE("a mask restricts the solve to the pixels it marks", "[focalshift]") {
    // The subject is what should drive the fit; a background at a wildly wrong
    // depth must not. Here the right half is poisoned and masked out, and the
    // answer must still be the camera the left half came from.
    const float focal = 1.0F;
    const float shift = 0.25F;
    auto points       = pointMap(focal, shift, tilted);
    std::vector<uint8_t> mask(static_cast<size_t>(kW) * kH, 1);
    for (int y = 0; y < kH; ++y) {
        for (int x = kW / 2; x < kW; ++x) {
            const size_t at =
                static_cast<size_t>(y) * static_cast<size_t>(kW) + static_cast<size_t>(x);
            mask[at] = 0;
            points[at * 3 + 2] += 17.0F;  // nonsense depth behind the mask
        }
    }

    const FocalShiftResult r = recoverFocalShift(points, kW, kH, mask);
    REQUIRE(r.recovered);
    CHECK(r.focal == Approx(focal).epsilon(0.03));
    CHECK(r.shift == Approx(shift).epsilon(0.05));

    // ...and without the mask it is wrong, which is what makes the mask worth
    // passing. If this ever stops being true the test above proves nothing.
    const FocalShiftResult unmasked = recoverFocalShift(points, kW, kH);
    CHECK(std::abs(unmasked.shift - shift) > 0.05F);
}

TEST_CASE("a fronto-parallel scene is degenerate, and that is not a bug", "[focalshift]") {
    // Every point at the same depth: focal and shift trade off exactly, so no
    // algorithm can separate them. The fit is still perfect, which is why
    // `recovered` stays true -- the caller that needs to know asks whether the
    // depth varied, and this test is where that is written down.
    const auto points        = pointMap(1.0F, 0.3F, [](double, double) { return 4.0; });
    const FocalShiftResult r = recoverFocalShift(points, kW, kH);
    CHECK(r.recovered);
}

TEST_CASE("too little input is reported, not guessed at", "[focalshift]") {
    const std::vector<float> tiny(12, 0.0F);
    CHECK_FALSE(recoverFocalShift(tiny, 2, 2).recovered);
    CHECK_FALSE(recoverFocalShift({}, 64, 64).recovered);

    // A mask that excludes nearly everything is the realistic version of the
    // same thing: a photograph where the subject is a handful of pixels.
    const auto points = pointMap(1.0F, 0.2F, tilted);
    std::vector<uint8_t> empty(static_cast<size_t>(kW) * kH, 0);
    CHECK_FALSE(recoverFocalShift(points, kW, kH, empty).recovered);
}
