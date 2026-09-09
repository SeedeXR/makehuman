// SPDX-License-Identifier: Apache-2.0
//
// Radial basis interpolation over a small set of example points.
//
// The second stage of the pose-space deformer owner directive 12.3 asks for:
// SwingTwist.h turns a joint rotation into a small signal vector, and this
// interpolates between the example poses an artist sculpted to say how much of
// each corrective to apply.
//
// Nothing here knows about poses, joints or meshes -- it is interpolation over
// R^n -- which is why it sits in foundation rather than rig. The pose-driver
// wiring (which joints, which component of each) is rig's, and comes with the
// authoring manifest.
//
// NOT ported from anything. The Python reference has no pose-space deformation
// at all.
//
// The split is directive 12.4's, and it is the whole shape of the design:
//
//   `rbfSolve`     OFFLINE, once per authored asset. Factorises an n-by-n
//                  matrix, where n is the number of example poses -- tens,
//                  not thousands.
//   `rbfEvaluate`  RUNTIME, every frame. Kernel evaluation and a matvec, no
//                  solve, no allocation.
//
// **The kernel is Gaussian and that is a load-bearing choice.** The Gaussian is
// a positive definite function, so for distinct centres the interpolation
// matrix is symmetric positive definite and Cholesky is both the correct
// factorisation and a stable one -- and, when the system is too degenerate to
// trust, Cholesky FAILS rather than returning something plausible. A
// thin-plate-spline kernel is only conditionally positive definite, needs a
// polynomial term and a pivoted factorisation, and is the case that would
// justify Eigen (LICENSING.md 5.1.1). Not needed yet, so not taken yet.
#pragma once

#include <cstddef>
#include <expected>
#include <span>
#include <vector>

namespace mh::foundation {

enum class RbfError {
    /// No example poses were given, so there is nothing to interpolate.
    NoCentres,
    /// The centre array, the value array, the dimension and the output count
    /// do not describe one consistent system.
    ShapeMismatch,
    /// A radius of zero or less names no kernel.
    BadRadius,
    /// The matrix is singular or too ill-conditioned to factorise. In practice
    /// this is two example poses at the same point -- the same pose keyed twice
    /// -- or a radius so much larger than the spacing that every kernel value
    /// rounds to 1 and the matrix collapses to rank one.
    NotSolvable,
};

/// The baked result of a solve: what a runtime needs and nothing else.
///
/// Laid out as flat arrays rather than a matrix type because this is what the
/// mmap-able blob of directive 12.4 will hold. **The order of `centres` is part
/// of the contract**: permuting it gives the same interpolant mathematically
/// but not the same last bit, so a compiler that reshuffles between builds
/// makes byte-comparison of exports fail for no discoverable reason.
struct RbfCoefficients {
    /// Dimension of the signal space -- how many numbers describe a pose.
    size_t dimension{};
    /// How many values the interpolant produces at once: the weight vector.
    size_t outputs{};
    /// Number of example poses.
    size_t count{};
    /// Gaussian width, in the units of the signal space.
    double radius{};
    /// `count * dimension`, row-major: example pose i starts at `i * dimension`.
    std::vector<double> centres;
    /// `count * outputs`, row-major: the solved coefficients for example pose i
    /// start at `i * outputs`. NOT the sculpted values -- those are what the
    /// solve was given.
    std::vector<double> weights;
};

/// Solves so that evaluating at example pose i returns value i exactly.
///
/// @p centres is `count * dimension` row-major, @p values is `count * outputs`
/// row-major, and @p radius is the Gaussian width. Several outputs are solved
/// together because they share one factorisation; the answer does not depend on
/// having done so.
///
/// Offline. Allocates, and factorises an n-by-n matrix.
[[nodiscard]] std::expected<RbfCoefficients, RbfError> rbfSolve(std::span<const double> centres,
                                                                size_t dimension,
                                                                std::span<const double> values,
                                                                size_t outputs, double radius);

/// Evaluates the interpolant at @p query, writing `outputs` values to @p out.
///
/// Returns false and writes nothing if @p query is not `dimension` long or
/// @p out cannot hold `outputs` values -- refused rather than guessed, because
/// the alternative is interpolating in the wrong space and looking plausible.
///
/// The hot path: no allocation, and the accumulation runs over centres in
/// stored order, which is what makes it bit-reproducible for a given blob
/// (directive 12.5 asks the same of the delta scatter-add).
[[nodiscard]] bool rbfEvaluate(const RbfCoefficients& coefficients, std::span<const double> query,
                               std::span<double> out);

}  // namespace mh::foundation
