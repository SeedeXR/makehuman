// SPDX-License-Identifier: Apache-2.0
//
// Draco compression of a glTF primitive.

#pragma once

#include "makehuman/foundation/Geometry.h"

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace mh::io {

/// One primitive, compressed, plus what `KHR_draco_mesh_compression` needs to
/// describe it.
struct DracoBuffer {
    /// The encoded bitstream, to be written as its own glTF bufferView.
    std::vector<uint8_t> bytes;

    /// glTF attribute name -> the draco attribute's unique id, which is
    /// literally the `attributes` map the extension carries. The names are
    /// glTF's ("POSITION"), not draco's, because that is what a consumer looks
    /// them up by.
    std::vector<std::pair<std::string, uint32_t>> attributes;
};

/// The skin attributes of a primitive, already in glTF's shape.
///
/// Four influences per vertex, because that is what `JOINTS_0`/`WEIGHTS_0` are.
/// Taken as flat spans rather than a `SkinView` on purpose: the conversion from
/// our per-influence arrays to glTF's fixed four belongs to the writer, which
/// already does it, and repeating it here would be a second place for the two
/// to disagree.
struct DracoSkin {
    std::span<const uint16_t> joints;
    std::span<const float> weights;
};

/// Whether this build can compress at all.
///
/// Draco is OPTIONAL, like assimp: a build without it still writes glTF, just
/// uncompressed. Reported rather than assumed so a stale CMake cache cannot
/// leave the writer declaring an extension nothing encoded.
[[nodiscard]] bool dracoAvailable() noexcept;

/// Compresses @p mesh, or `nullopt` if this build has no Draco.
///
/// Every attribute the view carries goes in. That is not thoroughness, it is
/// the extension's rule: a consumer reading the compressed buffer has nowhere
/// else to get an attribute from, so one left out is one the file no longer
/// has.
///
/// LOSSY, and only where it is safe. Positions, normals and UVs are quantised
/// -- 14, 10 and 12 bits, the values the glTF ecosystem converged on. Tangents,
/// joints and weights are not.
///
/// **Measured, because the obvious reasoning is wrong here.** Quantisation is
/// requested per attribute TYPE, and tangents, joints and weights all arrive as
/// GENERIC -- so "we simply do not quantise that type" sounds like the
/// safeguard and is not one: `SetAttributeQuantization(GENERIC, 2)` changes
/// **nothing** in this draco (1.5.7). Weights still come back summing to
/// exactly 1. The option is honoured for named types only.
///
/// What actually keeps joints exact is the declared data TYPE. `DT_UINT16`
/// against glTF's UNSIGNED_SHORT; declaring `DT_UINT8` truncates index 300 to
/// 44 and weights a vertex to the wrong bone, which is what the tests pin.
/// Revisit all of this if draco's own JOINTS/WEIGHTS types are ever used --
/// those are named, and named types ARE quantisable.
[[nodiscard]] std::optional<DracoBuffer> dracoEncode(const foundation::RenderView& mesh,
                                                     const DracoSkin& skin = {});

}  // namespace mh::io
