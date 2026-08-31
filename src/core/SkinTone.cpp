// SPDX-License-Identifier: AGPL-3.0-or-later
#include "makehuman/core/SkinTone.h"

#include <array>
#include <cmath>

namespace mh::core {
namespace {

// autoskinblender.py:46-48.
constexpr Vec3 kAsianColor{0.721F, 0.568F, 0.431F};
constexpr Vec3 kAfricanColor{0.207F, 0.113F, 0.066F};
constexpr Vec3 kCaucasianColor{0.843F, 0.639F, 0.517F};

/// `mixData`: `int(w1*d1 + w2*d2 + 0.5)` (image_operations.py:139-143).
uint8_t mixByte(uint8_t a, uint8_t b, float wa, float wb) noexcept {
    const float v = wa * static_cast<float>(a) + wb * static_cast<float>(b) + 0.5F;
    if (v <= 0.0F) return 0;
    if (v >= 255.0F) return 255;
    return static_cast<uint8_t>(v);
}

}  // namespace

Vec3 ethnicDiffuseColor(EthnicWeights w) noexcept {
    return Vec3{
        w.asian * kAsianColor.x + w.african * kAfricanColor.x + w.caucasian * kCaucasianColor.x,
        w.asian * kAsianColor.y + w.african * kAfricanColor.y + w.caucasian * kCaucasianColor.y,
        w.asian * kAsianColor.z + w.african * kAfricanColor.z + w.caucasian * kCaucasianColor.z};
}

std::expected<std::vector<uint8_t>, SkinBlendErrorKind> blendEthnicLitsphere(
    std::span<const std::span<const uint8_t>> images, EthnicWeights w) {
    if (images.size() != 3) return std::unexpected(SkinBlendErrorKind::SizeMismatch);

    // Fixed gather order, weight > 0 only -- autoskinblender.py:95-100.
    const std::array<float, 3> weights{w.caucasian, w.african, w.asian};
    std::vector<size_t> blends;
    for (size_t i = 0; i < 3; ++i) {
        if (weights[i] > 0.0F) blends.push_back(i);
    }
    if (blends.empty()) return std::unexpected(SkinBlendErrorKind::NoWeight);

    const size_t bytes = images[blends[0]].size();
    for (const size_t i : blends) {
        if (images[i].size() != bytes || bytes == 0) {
            return std::unexpected(SkinBlendErrorKind::SizeMismatch);
        }
    }

    // One contributor: the image itself, unmixed. Going through mixByte would
    // still round it, which the reference does not do here.
    if (blends.size() == 1) {
        return std::vector<uint8_t>(images[blends[0]].begin(), images[blends[0]].end());
    }

    std::vector<uint8_t> out(bytes);
    const auto& a  = images[blends[0]];
    const auto& b  = images[blends[1]];
    const float wa = weights[blends[0]];
    const float wb = weights[blends[1]];
    for (size_t i = 0; i < bytes; ++i)
        out[i] = mixByte(a[i], b[i], wa, wb);

    if (blends.size() == 3) {
        // Weight 1.0 on the accumulator, NOT a running average.
        const auto& c  = images[blends[2]];
        const float wc = weights[blends[2]];
        for (size_t i = 0; i < bytes; ++i)
            out[i] = mixByte(out[i], c[i], 1.0F, wc);
    }
    return out;
}

}  // namespace mh::core
