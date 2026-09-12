// SPDX-License-Identifier: Apache-2.0
#include "makehuman/ui/Background.h"

#include <QPainter>

namespace mh::ui {

QImage overBackground(const QImage& frame, const QImage& background) {
    if (frame.isNull()) return frame;
    if (background.isNull()) return frame;

    // KeepAspectRatioByExpanding is "cover": scale until both dimensions are at
    // least the frame's, then take the middle. Qt::SmoothTransformation because
    // a backdrop is a photograph, and nearest-neighbour on a downscaled photo
    // is visible as aliasing along every edge.
    const QImage scaled =
        background.scaled(frame.size(), Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation);
    const int x = (scaled.width() - frame.width()) / 2;
    const int y = (scaled.height() - frame.height()) / 2;

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
    painter.drawImage(QPoint(0, 0), scaled, QRect(x, y, frame.width(), frame.height()));
    painter.setCompositionMode(QPainter::CompositionMode_SourceOver);
    painter.drawImage(QPoint(0, 0), frame);
    painter.end();
    return out;
}

}  // namespace mh::ui
