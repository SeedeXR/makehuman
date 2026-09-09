// SPDX-License-Identifier: Apache-2.0
//
// See the header for the split between the offline solve and the runtime
// evaluation, and for why the kernel is Gaussian.
#include "makehuman/foundation/Rbf.h"

#include <cmath>
#include <cstddef>

namespace mh::foundation {

namespace {

/// exp(-(d/r)^2), with the squared distance passed in.
///
/// The distance is never square-rooted: the Gaussian only ever needs d^2, and
/// taking a root to square it again would cost accuracy as well as time.
double kernel(double distanceSquared, double radius) {
    return std::exp(-distanceSquared / (radius * radius));
}

double distanceSquared(std::span<const double> a, std::span<const double> b, size_t dimension) {
    double sum = 0.0;
    for (size_t d = 0; d < dimension; ++d) {
        const double delta = a[d] - b[d];
        sum += delta * delta;
    }
    return sum;
}

/// In-place Cholesky: replaces the lower triangle of @p m with L, where
/// `m == L * L^T`. Returns false if a pivot is not positive.
///
/// Reads and writes ONLY the lower triangle, so the caller need not fill the
/// other half of a symmetric matrix.
///
/// A pivot that is zero or negative means the matrix is not positive definite
/// -- which for a Gaussian kernel means the example poses are not distinct, or
/// are so close relative to the radius that the system has collapsed. That is
/// the failure worth having: an unpivoted factorisation of an indefinite matrix
/// stops, rather than dividing by something tiny and returning a plausible
/// answer built from noise.
bool cholesky(std::vector<double>& m, size_t n) {
    for (size_t i = 0; i < n; ++i) {
        for (size_t j = 0; j <= i; ++j) {
            double sum = m[i * n + j];
            for (size_t k = 0; k < j; ++k) {
                sum -= m[i * n + k] * m[j * n + k];
            }
            if (i == j) {
                // A tolerance rather than `<= 0.0`: a matrix on the edge of
                // singular gives a pivot of 1e-18 that is positive and useless,
                // and the solve that follows amplifies it into nonsense. Scaled
                // by the diagonal, which is 1.0 for this kernel -- k(0) = 1 --
                // so the comparison is against a known quantity rather than an
                // arbitrary absolute.
                if (sum <= 1e-12) return false;
                m[i * n + i] = std::sqrt(sum);
            } else {
                m[i * n + j] = sum / m[j * n + j];
            }
        }
    }
    return true;
}

/// Solves `L * L^T * x = b` in place for one right-hand side, given L in the
/// lower triangle of @p l.
void choleskySolve(const std::vector<double>& l, size_t n, size_t stride, size_t column,
                   std::vector<double>& x) {
    // Forward substitution through L.
    for (size_t i = 0; i < n; ++i) {
        double sum = x[i * stride + column];
        for (size_t k = 0; k < i; ++k) {
            sum -= l[i * n + k] * x[k * stride + column];
        }
        x[i * stride + column] = sum / l[i * n + i];
    }
    // Back substitution through L^T.
    for (size_t i = n; i-- > 0;) {
        double sum = x[i * stride + column];
        for (size_t k = i + 1; k < n; ++k) {
            sum -= l[k * n + i] * x[k * stride + column];
        }
        x[i * stride + column] = sum / l[i * n + i];
    }
}

}  // namespace

std::expected<RbfCoefficients, RbfError> rbfSolve(std::span<const double> centres, size_t dimension,
                                                  std::span<const double> values, size_t outputs,
                                                  double radius) {
    if (dimension == 0 || outputs == 0) return std::unexpected(RbfError::ShapeMismatch);
    if (centres.empty()) return std::unexpected(RbfError::NoCentres);
    if (centres.size() % dimension != 0) return std::unexpected(RbfError::ShapeMismatch);

    const size_t count = centres.size() / dimension;
    if (values.size() != count * outputs) return std::unexpected(RbfError::ShapeMismatch);
    if (!(radius > 0.0)) return std::unexpected(RbfError::BadRadius);

    // The interpolation matrix, k(|c_i - c_j|). Its diagonal is k(0) = 1, so
    // the scale is known, which is what lets the pivot test above use a fixed
    // tolerance.
    //
    // LOWER TRIANGLE ONLY. The matrix is symmetric and `cholesky` reads nothing
    // above the diagonal, so filling the other half is work with no reader. It
    // was filled here at first and a mutation removing that line passed all 12
    // cases -- correctly, because the line was dead. Deleted rather than given
    // a test it cannot have.
    std::vector<double> matrix(count * count);
    for (size_t i = 0; i < count; ++i) {
        for (size_t j = 0; j <= i; ++j) {
            matrix[i * count + j] =
                kernel(distanceSquared(centres.subspan(i * dimension, dimension),
                                       centres.subspan(j * dimension, dimension), dimension),
                       radius);
        }
    }

    if (!cholesky(matrix, count)) return std::unexpected(RbfError::NotSolvable);

    // One factorisation, one solve per output column. The weights start as the
    // sculpted values and are overwritten in place.
    std::vector<double> weights(values.begin(), values.end());
    for (size_t o = 0; o < outputs; ++o) {
        choleskySolve(matrix, count, outputs, o, weights);
    }

    return RbfCoefficients{.dimension = dimension,
                           .outputs   = outputs,
                           .count     = count,
                           .radius    = radius,
                           .centres   = std::vector<double>(centres.begin(), centres.end()),
                           .weights   = std::move(weights)};
}

bool rbfEvaluate(const RbfCoefficients& coefficients, std::span<const double> query,
                 std::span<double> out) {
    if (query.size() != coefficients.dimension) return false;
    if (out.size() < coefficients.outputs) return false;

    for (size_t o = 0; o < coefficients.outputs; ++o) {
        out[o] = 0.0;
    }
    // Centres in stored order, which is what makes this bit-reproducible for a
    // given blob. Accumulating per output inside the centre loop rather than
    // the other way round touches each kernel value once.
    for (size_t i = 0; i < coefficients.count; ++i) {
        const double k =
            kernel(distanceSquared(query,
                                   std::span<const double>(coefficients.centres)
                                       .subspan(i * coefficients.dimension, coefficients.dimension),
                                   coefficients.dimension),
                   coefficients.radius);
        for (size_t o = 0; o < coefficients.outputs; ++o) {
            out[o] += k * coefficients.weights[i * coefficients.outputs + o];
        }
    }
    return true;
}

}  // namespace mh::foundation
