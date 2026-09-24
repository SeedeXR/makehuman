// SPDX-License-Identifier: Apache-2.0
//
// Counts differing pixels between two PNGs, and asserts a bound.
//
// Byte comparison does not work on a RENDER, and this was learned the hard way
// twice. Generated PNGs of the same pixels can differ in their zlib stream; and
// -- the reason this exists -- two runs of the SAME scene on the same GPU are
// not bit-identical either. MEASURED on this machine: two screenshots that
// should be indistinguishable differ in NINE pixels, by up to 37 in a channel,
// scattered over the model. MSAA resolve order is not promised to be stable.
//
// So both directions of the claim need a tolerance, not just the "identical"
// one. A `files_differ` check on two renders is satisfied by that same noise,
// which means it would pass while the feature under test did nothing at all.
//
// PSNR is the other question, and counting cannot answer it. A lossy codec
// changes nearly every pixel a LITTLE, so a differing-pixel count says
// "everything changed" and tells you nothing about whether it still looks
// right -- basisu's ETC1S decodes to 38.92 dB while differing almost
// everywhere. `--min-psnr` is the quality floor that number is compared
// against; `--max-psnr` is its opposite, and exists so a test can assert that
// two frames really are different, which is the only thing that would catch
// the metric being inverted or stuck high.
//
// Usage: mh_png_compare <a.png> <b.png>
//            (--max-differing N | --min-differing N | --min-psnr DB | --max-psnr DB)
//
// On any difference it also prints the bounding box of the differing pixels.
// See the comment on the compare loop for why a count on its own has twice
// left a failure unexplained.

#include <QImage>
#include <QString>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <limits>

namespace {

/// Below this a channel is called equal. Small on purpose: the noise being
/// tolerated is a handful of PIXELS, not a shift across the frame, so the
/// pixel COUNT is what absorbs it.
constexpr int kChannelTolerance = 2;

/// Peak signal-to-noise ratio over the three colour channels, in decibels.
///
/// Both images must already be RGB888 and the same size. Infinity when they
/// are identical: log10(0) is not a number, and "as close as two images get"
/// is the honest reading, so a floor passes and a ceiling fails.
double psnrOf(const QImage& a, const QImage& b) {
    double squaredError = 0.0;
    for (int y = 0; y < a.height(); ++y) {
        const uchar* ra = a.constScanLine(y);
        const uchar* rb = b.constScanLine(y);
        for (int x = 0; x < a.width() * 3; ++x) {
            // No tolerance here, deliberately: PSNR measures how far the image
            // moved, so the small differences the pixel COUNT forgives are
            // exactly what this has to add up.
            const double d = static_cast<double>(ra[x]) - rb[x];
            squaredError += d * d;
        }
    }
    const double samples = static_cast<double>(a.width()) * a.height() * 3.0;
    const double mse     = squaredError / samples;
    if (mse == 0.0) return std::numeric_limits<double>::infinity();
    return 10.0 * std::log10(255.0 * 255.0 / mse);
}

/// Checks the metric against values that can be worked out by hand.
///
/// This exists because the two frame tests could not pin the FORMULA. They
/// compare an identical pair against a changed one, which discriminates
/// infinity from 11 dB and nothing finer -- so mutations that merely RESCALE
/// the result (dividing by pixels instead of samples, using MAX instead of
/// MAX squared, not squaring the error at all) sailed past both while leaving
/// every reported number wrong. Five of them survived before this was added.
/// That matters because the KTX2 work has to hold a specific 38.92 dB, and a
/// metric off by a constant would certify the wrong quality silently.
///
/// Two images differing by exactly `d` in every channel have MSE = d squared,
/// so PSNR = 20 log10(255/d) exactly. Three values pin the scale, the squaring
/// and the per-sample average independently.
int selfTest() {
    struct Case {
        int delta;
        double expected;
    };

    // 20*log10(255/1), /2, /5 -- worked out by hand, not read off the tool.
    const Case cases[] = {{1, 48.1308}, {2, 42.1085}, {5, 34.1514}};
    int bad            = 0;
    for (const Case& c : cases) {
        QImage a(4, 4, QImage::Format_RGB888);
        QImage b(4, 4, QImage::Format_RGB888);
        a.fill(QColor(100, 100, 100));
        b.fill(QColor(100 + c.delta, 100 + c.delta, 100 + c.delta));
        const double got = psnrOf(a, b);
        // `!(x <= tol)` rather than `x > tol`, because BOTH comparisons are
        // false for NaN and the naive spelling let a NaN result through as a
        // pass. A mutation that stopped squaring the error made `squaredError`
        // negative (d is negative here), log10 of a negative is NaN, and this
        // check waved it past -- the same hole this project found in
        // mh_glb_stat's bound parsing on the same day.
        if (!(std::abs(got - c.expected) <= 0.01)) {
            std::fprintf(stderr, "selftest: delta %d gave %.4f dB, expected %.4f\n", c.delta, got,
                         c.expected);
            ++bad;
        }
    }
    QImage same(4, 4, QImage::Format_RGB888);
    same.fill(QColor(7, 8, 9));
    if (!std::isinf(psnrOf(same, same))) {
        std::fprintf(stderr, "selftest: identical images are not infinite\n");
        ++bad;
    }
    if (bad == 0) std::printf("selftest: PSNR matches 48.13 / 42.11 / 34.15 dB and inf\n");
    return bad == 0 ? 0 : 1;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc == 2 && QString::fromLocal8Bit(argv[1]) == QLatin1String("--selftest")) {
        return selfTest();
    }
    if (argc != 5) {
        std::fprintf(stderr, "usage: %s <a.png> <b.png> (--max-differing N | --min-differing N)\n",
                     argv[0]);
        return 2;
    }
    const QString aPath = QString::fromLocal8Bit(argv[1]);
    const QString bPath = QString::fromLocal8Bit(argv[2]);
    const QString mode  = QString::fromLocal8Bit(argv[3]);
    const bool psnrMode =
        mode == QLatin1String("--min-psnr") || mode == QLatin1String("--max-psnr");
    const long want     = psnrMode ? 0 : std::strtol(argv[4], nullptr, 10);
    const double wantDb = psnrMode ? std::strtod(argv[4], nullptr) : 0.0;

    QImage a;
    QImage b;
    if (!a.load(aPath) || !b.load(bPath)) {
        std::fprintf(stderr, "could not read both images\n");
        return 2;
    }
    if (a.size() != b.size()) {
        std::fprintf(stderr, "sizes differ: %dx%d vs %dx%d\n", a.width(), a.height(), b.width(),
                     b.height());
        return 1;
    }
    a = a.convertToFormat(QImage::Format_RGB888);
    b = b.convertToFormat(QImage::Format_RGB888);

    // The BOX the differences fall in, not just how many there are.
    //
    // A count alone cannot tell the three failures apart, and each wants a
    // different investigation: MSAA noise is a handful of pixels scattered
    // right across the frame, a feature that did not draw is one compact
    // region, and a frame that composited late is a band. The intermittent
    // `app_backdrop_transparent_shows_nothing` failure of 2026-09-21 --
    // 278,285 of 2,363,772 -- was never reproduced, and the reason it stayed
    // unexplained is that the only thing recorded about it was the count. A
    // region would have said in one line whether that was the model's
    // silhouette (11.55% of a frame, measured) or something else entirely.
    long differing = 0;
    int minX       = a.width();
    int minY       = a.height();
    int maxX       = -1;
    int maxY       = -1;
    for (int y = 0; y < a.height(); ++y) {
        const uchar* ra = a.constScanLine(y);
        const uchar* rb = b.constScanLine(y);
        for (int x = 0; x < a.width(); ++x) {
            const int i = x * 3;
            if (std::abs(ra[i] - rb[i]) > kChannelTolerance ||
                std::abs(ra[i + 1] - rb[i + 1]) > kChannelTolerance ||
                std::abs(ra[i + 2] - rb[i + 2]) > kChannelTolerance) {
                ++differing;
                minX = std::min(minX, x);
                minY = std::min(minY, y);
                maxX = std::max(maxX, x);
                maxY = std::max(maxY, y);
            }
        }
    }

    const long total = static_cast<long>(a.width()) * a.height();

    if (psnrMode) {
        const double psnr = psnrOf(a, b);
        std::printf("PSNR %.2f dB over %ld pixels (%s %.2f)\n", psnr, total,
                    mode.toLocal8Bit().constData(), wantDb);
        if (mode == QLatin1String("--min-psnr")) return psnr >= wantDb ? 0 : 1;
        return psnr <= wantDb ? 0 : 1;  // --max-psnr
    }

    std::printf("%ld of %ld pixels differ (%s %ld)\n", differing, total,
                mode.toLocal8Bit().constData(), want);
    if (differing > 0) {
        // Printed next to the image's own size, because "662x663 in 1024x1024"
        // reads as a region and "1024x1024 in 1024x1024" reads as everything.
        std::printf("  differing region x %d..%d y %d..%d (%dx%d in %dx%d)\n", minX, maxX, minY,
                    maxY, maxX - minX + 1, maxY - minY + 1, a.width(), a.height());
    }
    if (mode == QLatin1String("--max-differing")) return differing <= want ? 0 : 1;
    if (mode == QLatin1String("--min-differing")) return differing >= want ? 0 : 1;
    std::fprintf(stderr, "unknown mode %s\n", mode.toLocal8Bit().constData());
    return 2;
}
