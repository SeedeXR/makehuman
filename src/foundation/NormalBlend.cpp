// SPDX-License-Identifier: Apache-2.0
//
// See the header for why this is the shaders' arithmetic and not its own.
#include "makehuman/foundation/NormalBlend.h"

#include <algorithm>
#include <cmath>

namespace mh::foundation {

namespace {

constexpr size_t kChannels = 4;

/// A stored byte as a tangent-space component.
float unpack(uint8_t c) noexcept {
    return 2.0F * (static_cast<float>(c) / 255.0F) - 1.0F;
}

uint8_t pack(float v) noexcept {
    const float s = (v * 0.5F + 0.5F) * 255.0F;
    return static_cast<uint8_t>(std::lround(std::clamp(s, 0.0F, 255.0F)));
}

bool describesItsBuffer(const NormalMapImage& img) noexcept {
    if (img.width <= 0 || img.height <= 0) return false;
    const size_t need =
        static_cast<size_t>(img.width) * static_cast<size_t>(img.height) * kChannels;
    return img.rgba.size() == need;
}

}  // namespace

std::string NormalBlendError::message() const {
    std::string s;
    switch (kind) {
        case NormalBlendErrorKind::Empty: s = "nothing to bake"; break;
        case NormalBlendErrorKind::SizeMismatch: s = "image size does not match its buffer"; break;
        case NormalBlendErrorKind::BadWeight: s = "wrinkle weight must be within [0, 1]"; break;
    }
    if (!detail.empty()) s += ": " + detail;
    return s;
}

std::expected<std::vector<uint8_t>, NormalBlendError> bakeWrinkleIntoNormalMap(
    NormalMapImage base, float baseIntensity, NormalMapImage wrinkle, float weight) {
    // `!(weight >= 0 && weight <= 1)` rather than the negation spelled out, so
    // a NaN weight is refused too -- every comparison against NaN is false, and
    // `weight < 0 || weight > 1` would let it through.
    if (!(weight >= 0.0F && weight <= 1.0F)) {
        return std::unexpected(NormalBlendError{NormalBlendErrorKind::BadWeight, {}});
    }
    if (base.rgba.empty() || wrinkle.rgba.empty()) {
        return std::unexpected(NormalBlendError{NormalBlendErrorKind::Empty, {}});
    }
    if (!describesItsBuffer(base)) {
        return std::unexpected(NormalBlendError{NormalBlendErrorKind::SizeMismatch, "base"});
    }
    if (!describesItsBuffer(wrinkle)) {
        return std::unexpected(NormalBlendError{NormalBlendErrorKind::SizeMismatch, "wrinkle"});
    }

    std::vector<uint8_t> out(base.rgba.size());
    for (int y = 0; y < base.height; ++y) {
        // Nearest-neighbour, sampled from the CENTRE of the base texel rather
        // than its corner. Sampling the corner rounds every texel toward the
        // sheet's origin, which reads as the whole sheet shifted half a texel.
        const int wy =
            std::min(wrinkle.height - 1, static_cast<int>((static_cast<float>(y) + 0.5F) /
                                                          static_cast<float>(base.height) *
                                                          static_cast<float>(wrinkle.height)));
        for (int x = 0; x < base.width; ++x) {
            const int wx =
                std::min(wrinkle.width - 1, static_cast<int>((static_cast<float>(x) + 0.5F) /
                                                             static_cast<float>(base.width) *
                                                             static_cast<float>(wrinkle.width)));

            const size_t bi = (static_cast<size_t>(y) * static_cast<size_t>(base.width) +
                               static_cast<size_t>(x)) *
                              kChannels;
            const size_t wi = (static_cast<size_t>(wy) * static_cast<size_t>(wrinkle.width) +
                               static_cast<size_t>(wx)) *
                              kChannels;

            const float bx = unpack(base.rgba[bi + 0]) * baseIntensity;
            const float by = unpack(base.rgba[bi + 1]) * baseIntensity;
            const float bz = unpack(base.rgba[bi + 2]);

            const float nx = bx + weight * unpack(wrinkle.rgba[wi + 0]);
            const float ny = by + weight * unpack(wrinkle.rgba[wi + 1]);

            const float len = std::sqrt(nx * nx + ny * ny + bz * bz);
            // A zero-length result is not reachable from real data -- it needs
            // z exactly 0 and xy cancelling exactly -- but dividing by it would
            // write NaN into a texture, which reads as a black hole in the
            // surface rather than as an error.
            const float inv = len > 0.0F ? 1.0F / len : 0.0F;

            out[bi + 0] = pack(nx * inv);
            out[bi + 1] = pack(ny * inv);
            out[bi + 2] = len > 0.0F ? pack(bz * inv) : uint8_t{255};
            out[bi + 3] = base.rgba[bi + 3];
        }
    }
    return out;
}

}  // namespace mh::foundation
