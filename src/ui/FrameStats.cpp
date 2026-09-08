// SPDX-License-Identifier: Apache-2.0
#include "makehuman/ui/FrameStats.h"

#include <QColor>

#include <algorithm>
#include <cstdio>

namespace mh::ui {

bool describeFrame(const QImage& img, std::string& out) {
    if (img.isNull()) {
        out = "empty frame -- nothing was rendered";
        return false;
    }
    const QImage rgb      = img.convertToFormat(QImage::Format_RGB32);
    const QRgb bg         = rgb.pixel(0, 0);
    const long long total = static_cast<long long>(rgb.width()) * rgb.height();
    long long covered     = 0;
    double sum            = 0.0;
    int lo = 255, hi = 0;
    for (int y = 0; y < rgb.height(); ++y) {
        for (int x = 0; x < rgb.width(); ++x) {
            const QRgb p = rgb.pixel(x, y);
            if (p == bg) continue;
            ++covered;
            const int l = qGray(p);
            sum += l;
            lo = std::min(lo, l);
            hi = std::max(hi, l);
        }
    }
    char buf[256];
    if (covered == 0) {
        std::snprintf(buf, sizeof buf, "%dx%d frame is a flat fill -- nothing drew", rgb.width(),
                      rgb.height());
        out = buf;
        return false;
    }
    std::snprintf(buf, sizeof buf,
                  "%dx%d covered %lld of %lld pixels (%.1f%%) luminance min=%d max=%d mean=%.1f",
                  rgb.width(), rgb.height(), covered, total,
                  100.0 * static_cast<double>(covered) / static_cast<double>(total), lo, hi,
                  sum / static_cast<double>(covered));
    out = buf;
    return true;
}

}  // namespace mh::ui
