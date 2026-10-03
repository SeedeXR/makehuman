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

Silhouette silhouetteFromMask(const QImage& mask) {
    Silhouette out;
    if (mask.isNull() || mask.width() < 1 || mask.height() < 1) return out;

    const QImage grey = mask.convertToFormat(QImage::Format_Grayscale8);
    out.mask          = QImage(grey.width(), grey.height(), QImage::Format_Grayscale8);
    out.mask.fill(0);

    int minX = grey.width();
    int minY = grey.height();
    int maxX = -1;
    int maxY = -1;
    for (int y = 0; y < grey.height(); ++y) {
        const uchar* row = grey.constScanLine(y);
        uchar* dst       = out.mask.scanLine(y);
        for (int x = 0; x < grey.width(); ++x) {
            if (row[x] <= 128) continue;
            dst[x] = 255;
            ++out.area;
            minX = std::min(minX, x);
            minY = std::min(minY, y);
            maxX = std::max(maxX, x);
            maxY = std::max(maxY, y);
        }
    }
    out.coverage = static_cast<double>(out.area) /
                   (static_cast<double>(grey.width()) * static_cast<double>(grey.height()));
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

    // Both outlines cropped to their own box and scaled to a COMMON HEIGHT,
    // uniformly. Height is the one dimension a photograph's unknown distance
    // makes meaningless, so it is normalised away; width at that height is
    // real information about the body and is kept.
    //
    // SCALING BOTH BOXES TO A COMMON BOX WOULD BE WRONG, and it is the obvious
    // thing to write. `IgnoreAspectRatio` onto a shared rectangle normalises
    // the aspect ratio too, so a tall narrow body and a short wide one of the
    // same shape score 1.0 -- and a fit driven by that score could never find a
    // waist, because widening the character would not move the number.
    const double scale = static_cast<double>(a.bounds.height()) / b.bounds.height();
    const int scaledW  = std::max(1, static_cast<int>(std::lround(b.bounds.width() * scale)));
    const int common   = a.bounds.height();

    const QImage maskA = a.mask.copy(a.bounds);
    const QImage maskB = b.mask.copy(b.bounds).scaled(scaledW, common, Qt::IgnoreAspectRatio,
                                                      Qt::FastTransformation);

    // Centred horizontally and aligned at the top, because the two are already
    // the same height. Aligning an edge instead would score a correct body as
    // wrong whenever one render sat a few pixels off centre.
    const int canvasW = std::max(a.bounds.width(), scaledW);
    const int offsetA = (canvasW - a.bounds.width()) / 2;
    const int offsetB = (canvasW - scaledW) / 2;

    for (int y = 0; y < common; ++y) {
        const auto* rowA = maskA.constScanLine(y);
        const auto* rowB = maskB.constScanLine(y);
        for (int x = 0; x < canvasW; ++x) {
            const int xa = x - offsetA;
            const int xb = x - offsetB;
            // Scaling a binary mask produces intermediate values along every
            // edge. 128 puts the boundary where it was, rather than growing the
            // subject (>0) or eroding it (==255) by a pixel all the way round.
            const bool inA = xa >= 0 && xa < a.bounds.width() && rowA[xa] > 128;
            const bool inB = xb >= 0 && xb < scaledW && rowB[xb] > 128;
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
