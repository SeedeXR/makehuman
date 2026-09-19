// SPDX-License-Identifier: Apache-2.0
//
// Encodes a PNG to ETC1S and writes what a decoder gets back.
//
// It asserts nothing itself. The quality question is "how far did the image
// move", which `mh_png_compare --min-psnr` already answers, so this only has to
// produce the second image for it to compare. Keeping the two apart means the
// PSNR gate is the same one, and the same tested code, everywhere it is used.
//
// Usage: mh_etc1s_roundtrip <in.png> <out.png>

#include "makehuman/io/Etc1s.h"

#include <QImage>
#include <QString>

#include <array>
#include <cstdio>
#include <cstring>
#include <vector>

int main(int argc, char** argv) {
    if (argc != 3) {
        std::fprintf(stderr, "usage: %s <in.png> <out.png>\n", argv[0]);
        return 2;
    }
    QImage in;
    if (!in.load(QString::fromLocal8Bit(argv[1]))) {
        std::fprintf(stderr, "cannot read %s\n", argv[1]);
        return 2;
    }
    in               = in.convertToFormat(QImage::Format_RGB888);
    const uint32_t w = static_cast<uint32_t>(in.width());
    const uint32_t h = static_cast<uint32_t>(in.height());
    if (w % 4 != 0 || h % 4 != 0) {
        std::fprintf(stderr, "%s is %ux%u; ETC1S needs multiples of 4\n", argv[1], w, h);
        return 2;
    }

    // QImage rows are padded to four bytes, so copy row by row rather than
    // assuming the buffer is tightly packed.
    std::vector<uint8_t> rgb(size_t{w} * h * 3);
    for (uint32_t y = 0; y < h; ++y)
        std::memcpy(rgb.data() + size_t{y} * w * 3, in.constScanLine(static_cast<int>(y)),
                    size_t{w} * 3);

    const auto blocks = mh::io::etc1sEncode(rgb, w, h);
    if (blocks.empty()) {
        std::fprintf(stderr, "encoder refused %ux%u\n", w, h);
        return 2;
    }

    QImage out(static_cast<int>(w), static_cast<int>(h), QImage::Format_RGB888);
    std::array<uint8_t, 48> tile{};
    const uint32_t bw = w / 4;
    for (size_t bi = 0; bi < blocks.size(); ++bi) {
        mh::io::etc1sDecodeBlock(blocks[bi], tile);
        const uint32_t bx = static_cast<uint32_t>(bi) % bw;
        const uint32_t by = static_cast<uint32_t>(bi) / bw;
        for (uint32_t y = 0; y < 4; ++y)
            std::memcpy(out.scanLine(static_cast<int>(by * 4 + y)) + size_t{bx} * 4 * 3,
                        tile.data() + y * 12, 12);
    }
    if (!out.save(QString::fromLocal8Bit(argv[2]), "PNG")) {
        std::fprintf(stderr, "could not write %s\n", argv[2]);
        return 2;
    }
    std::printf("%ux%u, %zu blocks\n", w, h, blocks.size());
    return 0;
}
