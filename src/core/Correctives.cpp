// SPDX-License-Identifier: AGPL-3.0-or-later
//
// See the header for why a corrective is a `core::Target` and why the deltas go
// on before skinning.
#include "makehuman/core/Correctives.h"

#include <cstdint>

namespace mh::core {

void CorrectiveBuffer::setRest(std::span<const Vec3> rest) {
    rest_.assign(rest.begin(), rest.end());
    positions_ = rest_;
    dirty_.clear();
    marked_.assign(rest_.size(), 0);
}

bool CorrectiveBuffer::apply(std::span<const Target* const> correctives,
                             std::span<const float> weights) {
    if (correctives.size() != weights.size()) return false;
    if (rest_.empty()) return false;

    // Validated in full BEFORE anything is written, so a refusal leaves the
    // buffer exactly as it was. Inactive correctives are checked too: an index
    // past the end of the mesh is an authoring error whenever it is noticed,
    // and noticing it only when an animation activates the corrective is the
    // worst time.
    const auto vertexCount = static_cast<uint32_t>(rest_.size());
    for (const Target* c : correctives) {
        if (c == nullptr) return false;
        if (c->verts.size() != c->offsets.size()) return false;
        if (!c->verts.empty() && c->maxVertexIndex >= vertexCount) return false;
    }

    // Undo the last frame, and only where it happened. `dirty_` holds each
    // moved vertex once, so this is proportional to what actually moved rather
    // than to the mesh -- and clearing `marked_` here rather than wholesale is
    // the same trick for the same reason: a pass over all 19,158 flags every
    // frame is the cost the dirty list exists to avoid.
    for (const uint32_t v : dirty_) {
        positions_[v] = rest_[v];
        marked_[v]    = 0;
    }
    dirty_.clear();

    for (size_t c = 0; c < correctives.size(); ++c) {
        const float w = weights[c];
        if (w == 0.0F) continue;

        const Target& t = *correctives[c];
        for (size_t i = 0; i < t.verts.size(); ++i) {
            const uint32_t v = t.verts[i];
            const Vec3& d    = t.offsets[i];
            positions_[v].x += d.x * w;
            positions_[v].y += d.y * w;
            positions_[v].z += d.z * w;
            if (marked_[v] == 0) {
                marked_[v] = 1;
                dirty_.push_back(v);
            }
        }
    }
    return true;
}

}  // namespace mh::core
