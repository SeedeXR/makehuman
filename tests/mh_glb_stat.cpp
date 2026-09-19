// SPDX-License-Identifier: Apache-2.0
//
// Reports what a .glb is made of, by byte, and asserts a bound on the share
// the images take.
//
// This exists because a number was quoted for two days that nothing could
// reproduce. "74.5% of the GLB is PNG (1,749,562 of 2,348,760 bytes)" sat in
// memory as the whole justification for adopting KTX2, and no tool, test or
// benchmark in the tree produced it -- `mh_export_fixture` writes its `base.glb`
// with no material at all, so it has no textures and cannot be the file that
// figure came from. A claim that cannot be re-measured cannot be falsified, and
// "we reduced it to N%" would have been unfalsifiable in exactly the same way.
//
// Re-measured with this: the image bytes are 1,749,562 exactly, as recorded.
// The DENOMINATOR had drifted -- 2,382,920 rather than 2,348,760, because the
// geometry grew -- so the share reads 73.4%, not 74.5%. That is the useful
// failure mode of writing the measurement down as code: the part that was
// right stays right, and the part that quietly moved is visible.
//
// Both bounds exist, and pairing them is the point. `--max-image-share` alone
// passes on a GLB with NO images at all (0% is ≤ anything), which is the same
// trap `obj_verts_differ.cmake` documents at length: a compare with no
// expectation passes on anything. A test that says "the textures are not most
// of the file" has to be paired with one that says "there are textures".
//
// Usage: mh_glb_stat <file.glb> [--max-image-share PCT | --min-image-share PCT]
//        With no bound it prints the breakdown and exits 0.

#include <nlohmann/json.hpp>

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

namespace {

/// glTF 2.0 §4.4.2: a 12-byte header of magic/version/length, then chunks of
/// {UInt32 length, UInt32 type, payload}. The two chunk types a .glb carries.
constexpr uint32_t kMagic = 0x46546C67;  // "glTF"
constexpr uint32_t kJson  = 0x4E4F534A;  // "JSON"
constexpr uint32_t kBin   = 0x004E4942;  // "BIN\0"

uint32_t readU32(const std::vector<char>& b, size_t at) {
    uint32_t v = 0;
    std::memcpy(&v, b.data() + at, sizeof v);  // .glb is little-endian, as are we
    return v;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc != 2 && argc != 4) {
        std::fprintf(stderr,
                     "usage: %s <file.glb> [--max-image-share PCT | --min-image-share PCT]\n",
                     argv[0]);
        return 2;
    }

    std::ifstream in(argv[1], std::ios::binary);
    if (!in) {
        std::fprintf(stderr, "cannot open %s\n", argv[1]);
        return 2;
    }
    const std::vector<char> bytes{std::istreambuf_iterator<char>(in),
                                  std::istreambuf_iterator<char>()};
    if (bytes.size() < 12 || readU32(bytes, 0) != kMagic) {
        std::fprintf(stderr, "%s is not a .glb (bad magic)\n", argv[1]);
        return 2;
    }

    // The header's length field, not the file size on disk: they agree in a
    // well-formed file, and disagreeing is worth reporting rather than hiding.
    const uint32_t total = readU32(bytes, 8);
    if (total != bytes.size()) {
        std::fprintf(stderr, "%s: header says %u bytes, file is %zu\n", argv[1], total,
                     bytes.size());
        return 2;
    }

    size_t jsonAt  = 0;
    size_t jsonLen = 0;
    size_t binLen  = 0;
    for (size_t at = 12; at + 8 <= bytes.size();) {
        const uint32_t len  = readU32(bytes, at);
        const uint32_t type = readU32(bytes, at + 4);
        if (at + 8 + len > bytes.size()) {
            std::fprintf(stderr, "%s: chunk at %zu overruns the file\n", argv[1], at);
            return 2;
        }
        if (type == kJson) {
            jsonAt  = at + 8;
            jsonLen = len;
        } else if (type == kBin) {
            binLen = len;
        }
        at += 8 + len;
    }
    if (jsonLen == 0) {
        std::fprintf(stderr, "%s: no JSON chunk\n", argv[1]);
        return 2;
    }

    const nlohmann::json doc =
        nlohmann::json::parse(std::string(bytes.data() + jsonAt, jsonLen), nullptr, false);
    if (doc.is_discarded()) {
        std::fprintf(stderr, "%s: JSON chunk does not parse\n", argv[1]);
        return 2;
    }

    // Images are counted through their bufferView rather than by summing the
    // BIN chunk: a .glb may also reference external images, and the accessors,
    // indices and inverse-bind matrices share that same chunk. What is wanted
    // is specifically "how much of this file is texture".
    const auto& views   = doc.value("bufferViews", nlohmann::json::array());
    const auto& images  = doc.value("images", nlohmann::json::array());
    uint64_t imageBytes = 0;
    std::printf("total %u\njson %zu\nbin %zu\n", total, jsonLen, binLen);
    for (size_t i = 0; i < images.size(); ++i) {
        const auto& img = images[i];
        const auto v    = img.value("bufferView", static_cast<size_t>(-1));
        if (v >= views.size()) {
            std::fprintf(stderr, "%s: image %zu has no usable bufferView\n", argv[1], i);
            return 2;
        }
        const uint64_t n = views[v].value("byteLength", uint64_t{0});
        imageBytes += n;
        std::printf("image %zu %s %llu\n", i, img.value("mimeType", "?").c_str(),
                    static_cast<unsigned long long>(n));
    }

    const double share = 100.0 * static_cast<double>(imageBytes) / total;
    std::printf("images %zu %llu %.1f%%\n", images.size(),
                static_cast<unsigned long long>(imageBytes), share);

    if (argc == 2) return 0;

    // Parsed strictly, which `mh_png_compare` does not bother to do with its
    // own bound -- deliberately different here. A typo'd `--min-image-share`
    // would otherwise come back 0.0 from atof, and a FLOOR of zero is a gate
    // that cannot fail: it would sit in the suite looking green while
    // asserting nothing. That is the failure this whole tool exists to avoid.
    // Written as a POSITIVE range test rather than two rejections, because
    // `strtod` accepts "nan" and every comparison against NaN is false --
    // `bound < 0.0 || bound > 100.0` let it through, and `share < NaN` is false
    // too, so the gate passed while asserting nothing. That is the same
    // unfalsifiable gate this parsing exists to prevent, so it is worth the
    // slightly awkward spelling. (`inf` and `-inf` were already caught.)
    char* end          = nullptr;
    const double bound = std::strtod(argv[3], &end);
    if (end == argv[3] || *end != '\0' || !(bound >= 0.0 && bound <= 100.0)) {
        std::fprintf(stderr, "%s is not a percentage between 0 and 100\n", argv[3]);
        return 2;
    }
    if (std::strcmp(argv[2], "--max-image-share") == 0) {
        if (share > bound) {
            std::fprintf(stderr, "images are %.1f%% of the GLB, expected at most %.1f%%\n", share,
                         bound);
            return 1;
        }
    } else if (std::strcmp(argv[2], "--min-image-share") == 0) {
        if (share < bound) {
            std::fprintf(stderr, "images are %.1f%% of the GLB, expected at least %.1f%%\n", share,
                         bound);
            return 1;
        }
    } else {
        std::fprintf(stderr, "unknown option %s\n", argv[2]);
        return 2;
    }
    return 0;
}
