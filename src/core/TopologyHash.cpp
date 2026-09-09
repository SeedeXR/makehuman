// SPDX-License-Identifier: AGPL-3.0-or-later
#include "makehuman/core/TopologyHash.h"

#include <string_view>

namespace mh::core {
namespace {

// FNV-1a, 64-bit. The constants are the published ones.
constexpr uint64_t kOffsetBasis = 1469598103934665603ULL;
constexpr uint64_t kPrime       = 1099511628211ULL;

void mix(uint64_t& h, uint64_t value) {
    // Byte at a time, little-endian, so the result does not depend on the
    // host's byte order -- this number is written into files.
    for (int byte = 0; byte < 8; ++byte) {
        h ^= (value >> (byte * 8)) & 0xFFULL;
        h *= kPrime;
    }
}

void mix(uint64_t& h, std::string_view text) {
    for (const char c : text) {
        h ^= static_cast<unsigned char>(c);
        h *= kPrime;
    }
    // A separator, so {"ab","c"} and {"a","bc"} do not collide.
    h ^= 0xFFULL;
    h *= kPrime;
}

}  // namespace

uint64_t topologyHash(const Mesh& mesh) {
    uint64_t h = kOffsetBasis;

    // Counts first: a mesh with the same indices but more unreferenced
    // vertices is a different target for anything indexed per vertex.
    mix(h, mesh.vertexCount());
    mix(h, mesh.uvCount());
    mix(h, mesh.faceCount());
    mix(h, mesh.vertsPerPrimitive());

    for (const uint32_t v : mesh.fvert())
        mix(h, v);
    for (const uint32_t t : mesh.fuvs())
        mix(h, t);
    for (const uint16_t g : mesh.group())
        mix(h, g);
    for (const FaceGroup& g : mesh.faceGroups())
        mix(h, g.name);

    return h;
}

}  // namespace mh::core
