// SPDX-License-Identifier: Apache-2.0
#include "makehuman/ui/Background.h"

#include "makehuman/ui/Backdrop.h"

#include <QPainter>

namespace mh::ui {

QImage overBackground(const QImage& frame, const QImage& background) {
    if (frame.isNull()) return frame;
    if (background.isNull()) return frame;

    // The cover rule lives in ONE place. `coverSource` returns the part of the
    // image a cover-fit draw shows, in image pixels; the viewport's backdrop
    // shader turns the same rectangle into UV scale and offset. Computing it
    // twice -- once here and once in GLSL -- is how the render and the viewport
    // would come to disagree about where a photograph sits.
    const QRectF src = coverSource(frame.size(), background.size());
    if (src.isEmpty()) return frame;

    QImage out(frame.size(), QImage::Format_ARGB32);
    // QImage's buffer is UNINITIALISED. The backdrop below covers every pixel,
    // so this costs nothing in the normal path -- but it is what makes the
    // result defined rather than merely benign. Deleting the null-backdrop
    // early-out above kills no test, because an uninitialised buffer comes back
    // zero-filled in practice and an unpainted frame then looks correct; that
    // is unspecified behaviour to be removed, not a test to be cleverer about.
    out.fill(Qt::transparent);
    QPainter painter(&out);
    // The backdrop is opaque and covers the whole frame, so there is nothing
    // under it to blend with -- copy it in, then draw the frame over the top
    // with normal source-over alpha.
    painter.setCompositionMode(QPainter::CompositionMode_Source);
    // CROP first, then `scaled`. A scaled `drawImage` is bilinear point
    // sampling, which drops source pixels past about 2x and aliases every edge
    // -- exactly what this comment used to claim to avoid, on a 6000px
    // photograph going into a 1024px render. `QImage::scaled` area-averages.
    // The cover RULE still lives in one place; only the resampling moved back.
    const QImage shown = background.copy(src.toRect())
                             .scaled(frame.size(), Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
    painter.drawImage(QPoint(0, 0), shown);
    painter.setCompositionMode(QPainter::CompositionMode_SourceOver);
    painter.drawImage(QPoint(0, 0), frame);
    painter.end();
    return out;
}

}  // namespace mh::ui
