// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Ethnic skin-tone blending: a TRANSLATION of AGPL Python
// (`legacy/python/apps/autoskinblender.py`), so this is AGPL-3.0 and belongs in
// `core`, never in a permissive module.
#pragma once

#include "makehuman/foundation/Types.h"

#include <cstdint>
#include <expected>
#include <span>
#include <vector>

namespace mh::core {

using foundation::Vec3;

/// The three ethnic sliders, as `Human` reports them. MakeHuman renormalises
/// them to sum to 1, which is what keeps the blend below in range.
struct EthnicWeights {
    float caucasian{};
    float african{};
    float asian{};
};

/// The skin diffuse colour for @p w.
///
/// `autoskinblender.py:116-118`, verbatim including the term order:
///
///     diffuse = asian*asianColor + african*africanColor + caucasian*caucasianColor
///
/// The three constants are `:46-48`. This is what `autoBlendSkin true` in a
/// `.mhmat` selects instead of the file's own `diffuseColor`.
[[nodiscard]] Vec3 ethnicDiffuseColor(EthnicWeights w) noexcept;

enum class SkinBlendErrorKind { SizeMismatch, NoWeight };

/// Blends the three ethnic litspheres by @p w into one RGBA8 image.
///
/// Faithful to `autoskinblender.py:89-109`, whose shape is easy to get wrong:
///
///   * only ethnicities with weight **> 0** take part, gathered in the fixed
///     order caucasian, african, asian;
///   * exactly one contributor returns that image **unmixed**;
///   * two are mixed with BOTH weights given, so it is `w0*a + w1*b`, not a
///     lerp;
///   * a third is folded in with **weight 1.0 on the accumulator**
///     (`mix(img, third, 1.0, w2)`) -- not a running average. With
///     renormalised weights this still lands at 255 rather than overflowing.
///
/// Rounding is the reference's: `int(w1*d1 + w2*d2 + 0.5)`, which is
/// round-half-up for the non-negative values involved.
///
/// @param images RGBA8 pixels for caucasian, african, asian, in that order.
///        All present entries must be the same length.
[[nodiscard]] std::expected<std::vector<uint8_t>, SkinBlendErrorKind> blendEthnicLitsphere(
    std::span<const std::span<const uint8_t>> images, EthnicWeights w);

}  // namespace mh::core
