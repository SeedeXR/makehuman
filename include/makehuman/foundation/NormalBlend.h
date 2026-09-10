// SPDX-License-Identifier: Apache-2.0
//
// Baking a wrinkle sheet into a normal map.
//
// The viewport blends the two per fragment (litsphere.frag, pbr.frag). No
// interchange format has a pose-driven normal map -- glTF, FBX and UsdSkel each
// give a material ONE normal texture -- so an export either bakes the blend at
// the pose it is writing, or ships a character whose creases exist only on the
// screen they were authored on.
//
// Apache-2.0 rather than AGPL, and that is checkable rather than assumed: the
// Python reference has no wrinkle-map code at all (grepped), so there is
// nothing here translated from it. The blend itself is the standard UDN
// combine.
//
// **It bakes the pose it is given and nothing else.** A baked map is correct at
// one point in signal space, which is the point the file records. A consumer
// that re-poses the exported rig keeps these creases -- that is a property of
// the formats, not a defect here, and it is the same trade the correctives
// themselves make when they refuse to bake into rest geometry.
#pragma once

#include <cstdint>
#include <expected>
#include <span>
#include <string>
#include <vector>

namespace mh::foundation {

/// Eight-bit RGBA pixels, row-major, four bytes each.
struct NormalMapImage {
    std::span<const uint8_t> rgba;
    int width{};
    int height{};
};

enum class NormalBlendErrorKind : uint8_t {
    /// No pixels, or a zero dimension. There is nothing to bake into.
    Empty,
    /// The declared size does not match the buffer. Reading past it would be a
    /// heap overflow, so it is refused rather than trusted.
    SizeMismatch,
    /// Outside [0, 1], or not a number. `rig::chooseWrinkle` clamps, so this is
    /// a caller bug rather than data -- and a caller bug is worth being loud
    /// about instead of clamping twice.
    BadWeight,
};

struct NormalBlendError {
    NormalBlendErrorKind kind{};
    std::string detail;

    [[nodiscard]] std::string message() const;
};

/// Bakes @p wrinkle into @p base at @p weight, returning RGBA8 at the BASE's
/// size.
///
/// The arithmetic is the shaders' arithmetic, deliberately:
///
///     tangentSpace = vec3(unpacked.xy * intensity, unpacked.z)
///     tangentSpace = vec3(tangentSpace.xy + weight * crease.xy, tangentSpace.z)
///
/// then normalize. The shaders normalize AFTER transforming by the TBN basis,
/// which is orthonormal -- so normalizing before the transform gives the same
/// world normal, and that is precisely what lets the blend be baked into a
/// texture at all. Two implementations of one blend is how an exported file and
/// the viewport come to disagree about a character.
///
/// @p baseIntensity is the material's `normalmapIntensity`, applied to the base
/// half exactly as the shaders apply it. It is baked in rather than exported
/// beside the texture because no writer here emits a normal-texture scale, so a
/// consumer would apply 1.0 to whatever it is handed.
///
/// The sheet is sampled **nearest-neighbour** to the base's resolution: the
/// shipped pairing is a 1024x1024 skin normal against a 256x256 sheet, so a
/// mismatch is the normal case rather than an error. Nearest and not bilinear
/// because interpolating packed normals across a crease edge invents a slope
/// that is in neither texel.
[[nodiscard]] std::expected<std::vector<uint8_t>, NormalBlendError> bakeWrinkleIntoNormalMap(
    NormalMapImage base, float baseIntensity, NormalMapImage wrinkle, float weight);

}  // namespace mh::foundation
