// SPDX-License-Identifier: Apache-2.0
//
// Encodes a PNG to a KTX2 file through `mh::io::ktx2EncodeEtc1s`.
//
// It asserts nothing, and it deliberately does NOT decode. The return leg is
// the `basisu` CLI (`basisu -unpack`, which reads .KTX2 and writes
// `*_unpacked_rgb_*.png`), so the image the quality gate compares against came
// out of an INDEPENDENT transcoder. Decoding with our own code would only
// prove we are self-consistent, and self-consistent is not correct -- that is
// the whole reason the oracle exists. Oracle at test time, our code at
// runtime.
//
// Usage: mh_ktx2_encode <in.png> <out.ktx2>

#include "makehuman/io/Ktx2Encode.h"

#include <QImage>
#include <QString>

#include <cstdio>
#include <cstring>
#include <fstream>
#include <vector>

int main(int argc, char** argv) {
    if (argc != 3) {
        std::fprintf(stderr, "usage: %s <in.png> <out.ktx2>\n", argv[0]);
        return 2;
    }
    if (!mh::io::ktx2Available()) {
        std::fprintf(stderr, "this build has no KTX2 encoder\n");
        return 2;
    }

    QImage in;
    if (!in.load(QString::fromLocal8Bit(argv[1]))) {
        std::fprintf(stderr, "cannot read %s\n", argv[1]);
        return 2;
    }
    in               = in.convertToFormat(QImage::Format_RGBA8888);
    const uint32_t w = static_cast<uint32_t>(in.width());
    const uint32_t h = static_cast<uint32_t>(in.height());

    // QImage pads rows to four bytes, so copy row by row rather than assuming
    // the buffer is tightly packed -- the same trap `mh_etc1s_roundtrip` notes.
    std::vector<uint8_t> rgba(size_t{w} * h * 4);
    for (uint32_t y = 0; y < h; ++y)
        std::memcpy(rgba.data() + size_t{y} * w * 4, in.constScanLine(static_cast<int>(y)),
                    size_t{w} * 4);

    const auto ktx2 = mh::io::ktx2EncodeEtc1s(rgba, w, h, mh::io::Ktx2Transfer::Srgb);
    if (!ktx2) {
        std::fprintf(stderr, "encoder refused %ux%u\n", w, h);
        return 2;
    }

    std::ofstream out(argv[2], std::ios::binary);
    if (!out) {
        std::fprintf(stderr, "could not open %s\n", argv[2]);
        return 2;
    }
    out.write(reinterpret_cast<const char*>(ktx2->data()),
              static_cast<std::streamsize>(ktx2->size()));
    if (!out) {
        std::fprintf(stderr, "could not write %s\n", argv[2]);
        return 2;
    }
    std::printf("%ux%u, %zu bytes\n", w, h, ktx2->size());
    return 0;
}
