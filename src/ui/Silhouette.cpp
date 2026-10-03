// SPDX-License-Identifier: Apache-2.0
#include "makehuman/ui/Silhouette.h"

#include <QColor>

#include <algorithm>
#include <cmath>

namespace mh::ui {

namespace {

/// Summed per-channel distance. Cheap, and the right shape for a tolerance a
/// human can reason about: "within 30 across all three channels".
int distance(QRgb a, QRgb b) {
    return std::abs(qRed(a) - qRed(b)) + std::abs(qGreen(a) - qGreen(b)) +
           std::abs(qBlue(a) - qBlue(b));
}

}  // namespace

Silhouette silhouetteOf(const QImage& image, int tolerance) {
    Silhouette out;
    if (image.isNull() || image.width() < 1 || image.height() < 1) return out;

    const QImage rgb      = image.convertToFormat(QImage::Format_ARGB32);
    const QRgb background = rgb.pixel(0, 0);

    // When the CLEAR was transparent, alpha IS the mask and the colour under
    // it is undefined -- `--transparent` leaves whatever the clear wrote there,
    // and on this machine that is the subject's own colour, so a distance test
    // classifies the entire frame as background and finds nothing at all.
    const bool clearIsTransparent = qAlpha(background) <= 8;

    out.mask = QImage(rgb.width(), rgb.height(), QImage::Format_Grayscale8);
    out.mask.fill(0);

    int minX = rgb.width();
    int minY = rgb.height();
    int maxX = -1;
    int maxY = -1;

    for (int y = 0; y < rgb.height(); ++y) {
        const auto* row = reinterpret_cast<const QRgb*>(rgb.constScanLine(y));
        auto* maskRow   = out.mask.scanLine(y);
        for (int x = 0; x < rgb.width(); ++x) {
            // A transparent pixel is background whatever its colour; an
            // opaque one against a transparent clear is subject whatever its
            // colour. Only when the clear is opaque does the colour decide.
            const bool opaque = qAlpha(row[x]) > 8;
            const bool isSubject =
                clearIsTransparent ? opaque : (opaque && distance(row[x], background) > tolerance);
            if (!isSubject) continue;
            maskRow[x] = 255;
            ++out.area;
            minX = std::min(minX, x);
            minY = std::min(minY, y);
            maxX = std::max(maxX, x);
            maxY = std::max(maxY, y);
        }
    }

    out.coverage = static_cast<double>(out.area) /
                   (static_cast<double>(rgb.width()) * static_cast<double>(rgb.height()));
    if (maxX >= 0) out.bounds = QRect(QPoint(minX, minY), QPoint(maxX, maxY));
    return out;
}

SilhouetteMatch compareSilhouettes(const Silhouette& a, const Silhouette& b) {
    SilhouetteMatch m;
    if (a.area == 0 || b.area == 0 || a.bounds.isEmpty() || b.bounds.isEmpty()) return m;

    m.widthRatio  = static_cast<double>(b.bounds.width()) / a.bounds.width();
    m.heightRatio = static_cast<double>(b.bounds.height()) / a.bounds.height();
    m.aspectA     = static_cast<double>(a.bounds.width()) / a.bounds.height();
    m.aspectB     = static_cast<double>(b.bounds.width()) / b.bounds.height();

    // Both outlines cropped to their own box and scaled to a common one, which
    // is what makes the score independent of where the subject sits in frame
    // and how far away the camera was. `a`'s box is the common one; using a
    // fixed size instead would resample BOTH and blur the sharper of the two.
    const QSize common = a.bounds.size();
    const QImage maskA = a.mask.copy(a.bounds);
    const QImage maskB =
        b.mask.copy(b.bounds).scaled(common, Qt::IgnoreAspectRatio, Qt::FastTransformation);

    for (int y = 0; y < common.height(); ++y) {
        const auto* rowA = maskA.constScanLine(y);
        const auto* rowB = maskB.constScanLine(y);
        for (int x = 0; x < common.width(); ++x) {
            // Scaling a binary mask produces intermediate values along every
            // edge. 128 puts the boundary where it was, rather than growing the
            // subject (>0) or eroding it (==255) by a pixel all the way round.
            const bool inA = rowA[x] > 128;
            const bool inB = rowB[x] > 128;
            if (inA && inB) {
                ++m.intersection;
            } else if (inA) {
                ++m.onlyA;
            } else if (inB) {
                ++m.onlyB;
            }
        }
    }

    const int64_t uni = m.intersection + m.onlyA + m.onlyB;
    if (uni > 0) m.iou = static_cast<double>(m.intersection) / static_cast<double>(uni);
    return m;
}

}  // namespace mh::ui
