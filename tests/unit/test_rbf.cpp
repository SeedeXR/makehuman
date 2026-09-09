// SPDX-License-Identifier: Apache-2.0
//
// Radial basis interpolation: the second stage of the pose-space deformer.
//
// The pose signal (tests/unit/test_swing_twist.cpp) turns a joint rotation into
// a small vector. This turns that vector into a WEIGHT VECTOR -- how much of
// each authored corrective to apply -- by interpolating between example poses
// an artist actually sculpted.
//
// Owner directive 12.4 splits it in two and this file tests both halves:
//
//   OFFLINE  solve the interpolation matrix once, bake the coefficients.
//   RUNTIME  kernel evaluation and a small matvec. No solve per frame.
//
// The oracle is the analytic one directive 12.7 asks for. There is nothing to
// be parity-tested against -- the Python reference has no PSD at all -- so
// these are cases whose answer follows from what interpolation MEANS:
//
//   AT an example pose, an interpolant reproduces the sculpted value exactly.
//       That is the definition, not an approximation, and it is the test a
//       plausible-but-wrong solve fails.
//   BETWEEN example poses, it must track a smooth function it was sampled
//       from. Sampled from a known analytic function, the error at the
//       midpoints is measurable and bounded.
//
// The kernel is Gaussian, and that choice buys something specific: the Gaussian
// is a positive definite function, so for distinct centres the interpolation
// matrix is symmetric POSITIVE DEFINITE and Cholesky is both correct and
// stable. A thin-plate-spline kernel is only conditionally positive definite,
// needs a polynomial term and a pivoted factorisation, and would be the reason
// to reach for Eigen (LICENSING.md 5.1.1). Not needed yet, so not taken yet.
#include "makehuman/foundation/Rbf.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <cmath>
#include <numbers>
#include <vector>

using namespace mh::foundation;
using Catch::Matchers::WithinAbs;

namespace {

/// The analytic corrective function standing in for a sculpted one.
///
/// Smooth, not separable, and not symmetric in its arguments, so an
/// implementation that transposes the centre matrix or collapses a dimension
/// gets a different answer rather than the same one.
double oracle(double a, double b) {
    return 0.7 * std::sin(1.3 * a) + 0.4 * std::cos(0.9 * b) + 0.25 * a * b;
}

/// A grid of centres over [-1, 1]^2, and the oracle sampled on it.
struct Samples {
    std::vector<double> centres;  ///< n * 2, row-major
    std::vector<double> values;   ///< n * 1
    size_t count{};
};

Samples grid(int side) {
    Samples s;
    for (int i = 0; i < side; ++i) {
        for (int j = 0; j < side; ++j) {
            const double a = -1.0 + 2.0 * static_cast<double>(i) / (side - 1);
            const double b = -1.0 + 2.0 * static_cast<double>(j) / (side - 1);
            s.centres.push_back(a);
            s.centres.push_back(b);
            s.values.push_back(oracle(a, b));
            ++s.count;
        }
    }
    return s;
}

double evaluateAt(const RbfCoefficients& c, double a, double b) {
    const std::vector<double> q{a, b};
    std::vector<double> out(c.outputs);
    // Asserted rather than discarded: a helper that silently read an untouched
    // `out` would make every case below pass on whatever was in the buffer.
    REQUIRE(rbfEvaluate(c, q, out));
    return out[0];
}

}  // namespace

TEST_CASE("the interpolant reproduces every example pose exactly", "[foundation][rbf]") {
    // The defining property. An implementation that returns something merely
    // close -- a normalised kernel average, say, which is what "RBF" often
    // loosely means -- fails here and nowhere else, because between the
    // centres a weighted average also looks plausible.
    //
    // The values here are deliberately NOT the oracle sampled at the centres,
    // and that is a hardening rather than a detail. With oracle values, a
    // mutation making `evaluateAt` return `oracle(a, b)` instead of the
    // interpolant passed this case -- of course it did, since the expected
    // value WAS the oracle. Index-derived values cannot be recovered from the
    // coordinates by any function, so passing requires having actually solved.
    const Samples s = grid(5);
    std::vector<double> arbitrary;
    for (size_t i = 0; i < s.count; ++i) {
        arbitrary.push_back(0.37 * static_cast<double>(i) - 1.1);
    }

    const auto solved = rbfSolve(s.centres, 2, arbitrary, 1, 0.6);
    REQUIRE(solved.has_value());
    REQUIRE(solved->count == s.count);

    // 1e-12, not 1e-9: the measured worst reproduction error is 6.7e-16, so a
    // loose tolerance would let a solve that is merely close pass the one test
    // that exists to demand exactness.
    for (size_t i = 0; i < s.count; ++i) {
        CHECK_THAT(evaluateAt(*solved, s.centres[2 * i], s.centres[2 * i + 1]),
                   WithinAbs(arbitrary[i], 1e-12));
    }

    // And the same holds for the sampled-oracle values the other cases use, so
    // this is a stronger test rather than a different one.
    const auto sampled = rbfSolve(s.centres, 2, s.values, 1, 0.6);
    REQUIRE(sampled.has_value());
    for (size_t i = 0; i < s.count; ++i) {
        CHECK_THAT(evaluateAt(*sampled, s.centres[2 * i], s.centres[2 * i + 1]),
                   WithinAbs(s.values[i], 1e-12));
    }
}

namespace {

/// Worst error against the oracle at points that are NOT sample points.
///
/// The probe list is fixed and deliberately off-grid for every side used here.
/// An earlier version probed `-0.8 + 1.6 * i / 4`, which lands exactly on a
/// sample for several of the grids -- so part of what it called interpolation
/// error was reproduction error, and reproduction is already exact.
double worstOffGrid(const RbfCoefficients& c) {
    constexpr double kProbes[] = {-0.77, -0.31, 0.13, 0.59, 0.83};
    double worst               = 0.0;
    for (const double a : kProbes) {
        for (const double b : kProbes) {
            worst = std::max(worst, std::abs(evaluateAt(c, a, b) - oracle(a, b)));
        }
    }
    return worst;
}

/// The authoring rule the numbers below establish: a Gaussian width of three
/// times the sample spacing.
double radiusFor(int side) {
    return 3.0 * 2.0 / (side - 1);
}

}  // namespace

TEST_CASE("between example poses it tracks the function it was sampled from", "[foundation][rbf]") {
    // "Verified AT and BETWEEN example poses" (directive 12.7).
    //
    // The bound is measured, and this is the second version of it: the first
    // asserted 6e-3 with a comment claiming the measured worst was 3.0e-3. Both
    // numbers were invented and the test failed on the first run -- at radius
    // 0.5 the real worst error is 7.0e-2. The measurement is in the case below.
    // Radius 3x spacing, grid(7): worst off-grid error 2.27e-3, so 5e-3 leaves
    // headroom without being a bound anything would pass.
    const Samples s   = grid(7);
    const auto solved = rbfSolve(s.centres, 2, s.values, 1, radiusFor(7));
    REQUIRE(solved.has_value());
    CHECK(worstOffGrid(*solved) < 5e-3);

    // And it is not passing because the oracle happens to be small: the
    // function's own range over this domain is three orders of magnitude
    // larger than that error.
    CHECK(std::abs(oracle(1.0, 1.0) - oracle(-1.0, -1.0)) > 1.0);
}

TEST_CASE("a radius near the sample spacing is exact AT the samples and poor between",
          "[foundation][rbf]") {
    // The trap this kernel sets, and the reason the manifest of directive 12.4
    // has to carry a radius tied to the spacing rather than a constant.
    //
    // A narrow Gaussian reproduces every sculpted pose perfectly and still
    // interpolates badly: between samples each kernel has already decayed, so
    // the surface sags toward zero. Measured on grid(7), spacing 0.333, worst
    // error at the cell centres:
    //
    //     radius = 1x spacing -> 1.58e-1     3x -> 2.31e-3
    //                      2x -> 2.28e-2     4x -> 4.17e-4
    //
    // Two orders of magnitude between the first and the third, from a
    // parameter an author would not think of as accuracy-critical.
    const Samples s   = grid(7);
    const auto narrow = rbfSolve(s.centres, 2, s.values, 1, 2.0 / 6.0);
    const auto sane   = rbfSolve(s.centres, 2, s.values, 1, radiusFor(7));
    REQUIRE(narrow.has_value());
    REQUIRE(sane.has_value());

    // Exact at the samples either way -- which is exactly why this cannot be
    // caught by the reproduction test.
    for (size_t i = 0; i < s.count; ++i) {
        CHECK_THAT(evaluateAt(*narrow, s.centres[2 * i], s.centres[2 * i + 1]),
                   WithinAbs(s.values[i], 1e-12));
    }
    CHECK(worstOffGrid(*narrow) > 10.0 * worstOffGrid(*sane));
}

TEST_CASE("a finer sample set tracks the function better", "[foundation][rbf]") {
    // Convergence, which is what says the error above is interpolation error
    // rather than a bug that happens to be small: a wrong implementation has no
    // reason to improve when given more samples.
    //
    // Measured at 3x spacing, worst off-grid error by side:
    //
    //     4 -> 7.01e-3    6 -> 2.21e-3    9 -> 1.17e-3    13 -> 1.74e-4
    //
    // and it is NOT monotone at every step -- side 6 to 7 rises slightly, from
    // 2.21e-3 to 2.27e-3, because the radius shrinks with the grid and the two
    // effects trade off. So the sides here are spaced far enough apart for the
    // trend to dominate, which is the honest way to assert a trend that has
    // noise in it.
    double previous = 1.0e9;
    for (const int side : {4, 6, 9, 13}) {
        const Samples s   = grid(side);
        const auto solved = rbfSolve(s.centres, 2, s.values, 1, radiusFor(side));
        REQUIRE(solved.has_value());
        const double worst = worstOffGrid(*solved);
        CHECK(worst < previous);
        previous = worst;
    }
    // The finest grid is an order of magnitude better than the coarsest, so the
    // chain above is a real improvement and not four numbers within noise.
    CHECK(previous < 7.01e-3 / 10.0);
}

TEST_CASE("several outputs solve together and match solving them apart", "[foundation][rbf]") {
    // A weight vector, not a scalar (directive 12.3). The point of solving them
    // together is that they share one factorisation, so the answer must not
    // depend on having done so -- and an implementation that mixed up the
    // row-major stride would pass a single-output test and fail this one.
    Samples s = grid(4);
    std::vector<double> two;
    std::vector<double> first;
    std::vector<double> second;
    for (size_t i = 0; i < s.count; ++i) {
        const double a = s.centres[2 * i];
        const double b = s.centres[2 * i + 1];
        two.push_back(oracle(a, b));
        two.push_back(oracle(b, a) * 2.0);  // deliberately NOT the same function
        first.push_back(oracle(a, b));
        second.push_back(oracle(b, a) * 2.0);
    }

    const auto both  = rbfSolve(s.centres, 2, two, 2, 0.7);
    const auto one   = rbfSolve(s.centres, 2, first, 1, 0.7);
    const auto other = rbfSolve(s.centres, 2, second, 1, 0.7);
    REQUIRE(both.has_value());
    REQUIRE(one.has_value());
    REQUIRE(other.has_value());
    REQUIRE(both->outputs == 2);

    const std::vector<double> q{0.31, -0.17};
    std::vector<double> pair(2);
    std::vector<double> solo(1);
    REQUIRE(rbfEvaluate(*both, q, pair));
    REQUIRE(rbfEvaluate(*one, q, solo));
    CHECK_THAT(pair[0], WithinAbs(solo[0], 1e-9));
    REQUIRE(rbfEvaluate(*other, q, solo));
    CHECK_THAT(pair[1], WithinAbs(solo[0], 1e-9));
}

TEST_CASE("far from every example pose the interpolant returns to rest", "[foundation][rbf]") {
    // A Gaussian kernel with no polynomial term decays to zero, and for a
    // corrective that is the right behaviour rather than a limitation: a pose
    // nothing was sculpted for gets NO corrective, not an extrapolation of the
    // nearest one. Worth pinning, because adding a polynomial term later would
    // change it and should be a decision rather than a surprise.
    const Samples s   = grid(4);
    const auto solved = rbfSolve(s.centres, 2, s.values, 1, 0.4);
    REQUIRE(solved.has_value());
    CHECK_THAT(evaluateAt(*solved, 40.0, -40.0), WithinAbs(0.0, 1e-12));
}

TEST_CASE("the query is refused, not guessed, when it is the wrong shape", "[foundation][rbf]") {
    const Samples s   = grid(3);
    const auto solved = rbfSolve(s.centres, 2, s.values, 1, 0.8);
    REQUIRE(solved.has_value());

    // A query of the wrong length would otherwise read past its own span or
    // silently interpolate in the wrong space.
    const std::vector<double> shortQuery{0.1};
    std::vector<double> out(1);
    CHECK_FALSE(rbfEvaluate(*solved, shortQuery, out));

    // An output span too small to hold the weight vector.
    const std::vector<double> q{0.1, 0.2};
    std::vector<double> tooSmall;
    CHECK_FALSE(rbfEvaluate(*solved, q, tooSmall));

    // The right shapes still work, so the guards are not refusing everything.
    CHECK(rbfEvaluate(*solved, q, out));
}

TEST_CASE("a solve that cannot be trusted is refused", "[foundation][rbf]") {
    const Samples s = grid(3);

    SECTION("no centres at all") {
        const std::vector<double> none;
        const auto r = rbfSolve(none, 2, none, 1, 0.5);
        REQUIRE_FALSE(r.has_value());
        CHECK(r.error() == RbfError::NoCentres);
    }

    SECTION("centre array not a whole number of points") {
        // Three doubles in a 2-D space: one whole point and half of another.
        //
        // The value array is sized to the TRUNCATED count (1), deliberately.
        // With two values instead, this section passed through the
        // value-count guard below and the divisibility guard was never
        // exercised -- removing it passed all 12 cases. Sized this way, a
        // missing guard means silently interpolating on the first point and
        // dropping the stray coordinate, which is the failure worth refusing.
        const std::vector<double> ragged{0.0, 1.0, 2.0};
        const std::vector<double> vals{5.0};
        const auto r = rbfSolve(ragged, 2, vals, 1, 0.5);
        REQUIRE_FALSE(r.has_value());
        CHECK(r.error() == RbfError::ShapeMismatch);
    }

    SECTION("one value per centre, or the system is not square") {
        const std::vector<double> vals{0.0, 1.0};
        const auto r = rbfSolve(s.centres, 2, vals, 1, 0.5);
        REQUIRE_FALSE(r.has_value());
        CHECK(r.error() == RbfError::ShapeMismatch);
    }

    SECTION("zero dimensions is not a space to interpolate in") {
        // This guard has to come FIRST, and that ordering is the point of it:
        // the divisibility check is `centres.size() % dimension`, which for
        // dimension 0 is a modulo by zero -- undefined behaviour, not a
        // refusal. Removing this guard still passes in the debug preset,
        // because the UB happens to leave a non-zero remainder and the next
        // guard catches it. The ASan+UBSan preset is where it is caught;
        // verified by running the mutation there rather than reasoned about.
        const auto r = rbfSolve(s.centres, 0, s.values, 1, 0.5);
        REQUIRE_FALSE(r.has_value());
        CHECK(r.error() == RbfError::ShapeMismatch);
    }

    SECTION("no outputs means nothing to solve for") {
        // The values array is EMPTY here, and that is what makes this case the
        // guard's own. With `s.values` instead, the value-count check catches
        // it first -- nine values against an expected count * 0 -- and
        // removing this guard passed all 12 cases. With nothing to solve for,
        // 0 == 0 and the solve goes on to succeed, returning coefficients
        // whose weight vector has no entries: an interpolant that silently
        // does nothing.
        const std::vector<double> noValues;
        const auto r = rbfSolve(s.centres, 2, noValues, 0, 0.5);
        REQUIRE_FALSE(r.has_value());
        CHECK(r.error() == RbfError::ShapeMismatch);
    }

    SECTION("a radius of zero or less has no kernel") {
        for (const double radius : {0.0, -1.0}) {
            const auto r = rbfSolve(s.centres, 2, s.values, 1, radius);
            REQUIRE_FALSE(r.has_value());
            CHECK(r.error() == RbfError::BadRadius);
        }
    }

    SECTION("two example poses at the same point make the system singular") {
        // The realistic authoring mistake: the same pose keyed twice. The
        // matrix has two identical rows, so there is no unique solution --
        // refused rather than returning whichever of the infinitely many
        // answers the arithmetic happens to land on.
        std::vector<double> centres = s.centres;
        std::vector<double> values  = s.values;
        centres.push_back(s.centres[0]);
        centres.push_back(s.centres[1]);
        values.push_back(s.values[0]);
        const auto r = rbfSolve(centres, 2, values, 1, 0.5);
        REQUIRE_FALSE(r.has_value());
        CHECK(r.error() == RbfError::NotSolvable);
    }
}

TEST_CASE("an ill-conditioned system is refused rather than answered wrongly",
          "[foundation][rbf]") {
    // A radius far larger than the spacing makes every kernel value nearly 1,
    // so the matrix approaches all-ones: rank one, and the solve is meaningless
    // long before it is impossible. Cholesky on a positive definite matrix
    // fails by producing a non-positive pivot, which is a DETECTABLE failure --
    // that is the whole reason the Gaussian was chosen over a kernel needing a
    // pivoted factorisation.
    //
    // The threshold IS bisected, 60 iterations, and the first version of this
    // comment claimed a bisection that had not been run: it asserted radius 10
    // still solves, and radius 10 does not. Measured on this 5x5 grid, spacing
    // 0.5, it solves up to radius 5.8299 and is refused above that -- roughly
    // twelve times the spacing, well past anything an author would choose.
    const Samples s = grid(5);
    const auto huge = rbfSolve(s.centres, 2, s.values, 1, 100.0);
    REQUIRE_FALSE(huge.has_value());
    CHECK(huge.error() == RbfError::NotSolvable);

    const auto tooWide = rbfSolve(s.centres, 2, s.values, 1, 10.0);
    REQUIRE_FALSE(tooWide.has_value());

    // And it is not refusing everything: five times the spacing still solves.
    const auto fine = rbfSolve(s.centres, 2, s.values, 1, 5.0);
    REQUIRE(fine.has_value());
}

TEST_CASE("the stored centre order is part of the baked contract", "[foundation][rbf]") {
    // Permuting the example poses gives the SAME interpolant mathematically,
    // and this checks that -- a solve that depended on input order would be a
    // bug. What it does NOT promise is bit-identical output, because summing
    // the same terms in a different order does not give the same last bit.
    //
    // That is a note for the step-4 compiler rather than a limitation: the
    // baked blob must keep a stable centre order between builds, or a
    // regression test comparing exports byte-for-byte will fail for no reason
    // anyone can find. Directive 12.5 makes the same point about scatter-add.
    Samples s = grid(4);
    std::vector<double> centres;
    std::vector<double> values;
    for (size_t i = s.count; i-- > 0;) {
        centres.push_back(s.centres[2 * i]);
        centres.push_back(s.centres[2 * i + 1]);
        values.push_back(s.values[i]);
    }

    const auto forward = rbfSolve(s.centres, 2, s.values, 1, 0.7);
    const auto reverse = rbfSolve(centres, 2, values, 1, 0.7);
    REQUIRE(forward.has_value());
    REQUIRE(reverse.has_value());

    for (const double a : {-0.6, -0.1, 0.35, 0.9}) {
        for (const double b : {-0.9, 0.2, 0.7}) {
            CHECK_THAT(evaluateAt(*forward, a, b), WithinAbs(evaluateAt(*reverse, a, b), 1e-9));
        }
    }
}

TEST_CASE("one dimension and one centre are both real cases", "[foundation][rbf]") {
    // A single driving joint component with a single sculpted pose is the
    // smallest thing an artist will actually author, and a solver written for
    // the general case tends to fall over on n = 1.
    const std::vector<double> centre{0.5};
    const std::vector<double> value{3.0};
    const auto solved = rbfSolve(centre, 1, value, 1, 0.4);
    REQUIRE(solved.has_value());

    const std::vector<double> at{0.5};
    std::vector<double> out(1);
    REQUIRE(rbfEvaluate(*solved, at, out));
    CHECK_THAT(out[0], WithinAbs(3.0, 1e-12));

    // Falls off from there, and is symmetric about the one centre.
    const std::vector<double> left{0.5 - 0.3};
    const std::vector<double> right{0.5 + 0.3};
    std::vector<double> l(1);
    std::vector<double> r(1);
    REQUIRE(rbfEvaluate(*solved, left, l));
    REQUIRE(rbfEvaluate(*solved, right, r));
    CHECK_THAT(l[0], WithinAbs(r[0], 1e-12));
    CHECK(l[0] < 3.0);
    CHECK(l[0] > 0.0);
}

TEST_CASE("the rest pose is the natural centre of the signal space", "[foundation][rbf]") {
    // Why `rotationVector` returns the log map rather than a quaternion
    // (SwingTwist.h): identity maps to the origin, so a corrective sculpted at
    // rest sits at the origin of this space and an unposed character gets
    // exactly what was sculpted there. Checked end to end rather than assumed,
    // because it is the one place the two stages have to agree.
    const std::vector<double> centres{0.0, 0.0, 0.9, 0.0, 0.0, 0.9};
    const std::vector<double> values{0.0, 1.0, 1.0};
    const auto solved = rbfSolve(centres, 2, values, 1, 0.45);
    REQUIRE(solved.has_value());
    CHECK_THAT(evaluateAt(*solved, 0.0, 0.0), WithinAbs(0.0, 1e-9));
}
