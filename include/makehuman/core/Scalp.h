// SPDX-License-Identifier: AGPL-3.0-or-later
//
// The hair-bearing scalp: which body vertices a hairstyle may cover.
//
// Every style so far has used "the cranium cap", every body vertex above the
// cranium centre's height. That is a scalp by any definition, and RENDERING
// the cornrows settled that it is the wrong one. MEASURED on the shipped base
// mesh, with rows routed over the cap and looked at from four Blender
// cameras: each row's front end sat 3 to 11 degrees BELOW the hairline -- on
// the forehead -- and its back end stopped 52 to 57 degrees ABOVE the nape,
// leaving the whole occiput bare. The cap is not the hairline region shifted;
// it trades 29 forehead vertices for 109 nape ones.
//
// The hairline itself was already measured, in `tools/make_hair_styles.py`,
// and an earlier comment in the walk tests argued the trigonometry "belongs to
// the style generator, not to this primitive". Cornrows disprove that: a braid
// is ROUTED over the region by `pathOverSurface`, so the walk needs the region
// before any style exists, and the region is anatomy rather than style -- the
// afro, cornrows and locs all grow from the same skin.
#pragma once

#include <cstdint>
#include <vector>

namespace mh::core {

class Mesh;

/// The cranium centre, in mesh units: the frame both functions below measure in.
inline constexpr float kCraniumY = 7.75F;
inline constexpr float kCraniumZ = 0.50F;

/// Elevation of the hairline, in degrees, at azimuth @p azimuthDeg about the
/// cranium centre, measured from the FRONT (+z).
///
/// MEASURED on the base mesh: forehead ~+9 degrees in this frame, brow -13,
/// ears -25, nape -50.
[[nodiscard]] float hairlineElevation(float azimuthDeg);

/// Body-group vertices at or above the hairline.
///
/// The group restriction IS the region's correctness, not a refinement:
/// `helper-hair` (a long-hair envelope carrying ribbons down over the face)
/// and `joint-head-2` also sit over the cranium, and height alone cannot tell
/// them apart.
[[nodiscard]] std::vector<uint32_t> hairBearingScalp(const Mesh& mesh);

}  // namespace mh::core
