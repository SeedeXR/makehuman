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
// Usage: mh_png_compare <a.png> <b.png> (--max-differing N | --min-differing N)

#include <QImage>
#include <QString>

#include <cstdio>
#include <cstdlib>

namespace {

/// Below this a channel is called equal. Small on purpose: the noise being
/// tolerated is a handful of PIXELS, not a shift across the frame, so the
/// pixel COUNT is what absorbs it.
constexpr int kChannelTolerance = 2;

}  // namespace

int main(int argc, char** argv) {
    if (argc != 5) {
        std::fprintf(stderr, "usage: %s <a.png> <b.png> (--max-differing N | --min-differing N)\n",
                     argv[0]);
        return 2;
    }
    const QString aPath = QString::fromLocal8Bit(argv[1]);
    const QString bPath = QString::fromLocal8Bit(argv[2]);
    const QString mode  = QString::fromLocal8Bit(argv[3]);
    const long want     = std::strtol(argv[4], nullptr, 10);

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

    long differing = 0;
    for (int y = 0; y < a.height(); ++y) {
        const uchar* ra = a.constScanLine(y);
        const uchar* rb = b.constScanLine(y);
        for (int x = 0; x < a.width(); ++x) {
            const int i = x * 3;
            if (std::abs(ra[i] - rb[i]) > kChannelTolerance ||
                std::abs(ra[i + 1] - rb[i + 1]) > kChannelTolerance ||
                std::abs(ra[i + 2] - rb[i + 2]) > kChannelTolerance) {
                ++differing;
            }
        }
    }

    const long total = static_cast<long>(a.width()) * a.height();
    std::printf("%ld of %ld pixels differ (%s %ld)\n", differing, total,
                mode.toLocal8Bit().constData(), want);
    if (mode == QLatin1String("--max-differing")) return differing <= want ? 0 : 1;
    if (mode == QLatin1String("--min-differing")) return differing >= want ? 0 : 1;
    std::fprintf(stderr, "unknown mode %s\n", mode.toLocal8Bit().constData());
    return 2;
}
