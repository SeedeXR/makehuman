// SPDX-License-Identifier: AGPL-3.0-or-later
#include "makehuman/core/SurfaceWalk.h"

#include "makehuman/core/Mesh.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <queue>

namespace mh::core {

namespace {

constexpr float kInf = std::numeric_limits<float>::infinity();

/// Region membership by vertex id, for O(1) "is this vertex walkable".
///
/// All three entry points need exactly this, and built it separately.
std::vector<uint8_t> regionMask(const Mesh& mesh, std::span<const uint32_t> region) {
    std::vector<uint8_t> inRegion(mesh.vertexCount(), 0U);
    for (const uint32_t v : region) {
        if (v < inRegion.size()) inRegion[v] = 1U;
    }
    return inRegion;
}

/// Edge neighbours of every region vertex, keyed by mesh vertex id.
///
/// Built from the face corner array rather than the mesh's `vface` table: a
/// face's corners are adjacent in a ring, so consecutive pairs ARE its edges,
/// for triangles and quads alike. No separate edge structure has to exist, and
/// none has to be kept in step with the faces.
std::vector<std::vector<uint32_t>> edgeNeighbours(const Mesh& mesh,
                                                  std::span<const uint32_t> region) {
    const std::vector<uint8_t> inRegion = regionMask(mesh, region);

    std::vector<std::vector<uint32_t>> adj(mesh.vertexCount());
    const std::span<const uint32_t> fvert = mesh.fvert();
    const size_t stride                   = mesh.vertsPerPrimitive();
    if (stride < 2) return adj;

    for (size_t f = 0; f + stride <= fvert.size(); f += stride) {
        for (size_t c = 0; c < stride; ++c) {
            const uint32_t a = fvert[f + c];
            const uint32_t b = fvert[f + (c + 1) % stride];
            // Both ends must be in the region: an edge with one end outside is
            // the region's boundary, and crossing it is exactly what a walk
            // must not do.
            if (a >= inRegion.size() || b >= inRegion.size()) continue;
            if (inRegion[a] == 0U || inRegion[b] == 0U) continue;
            if (a == b) continue;
            adj[a].push_back(b);
            adj[b].push_back(a);
        }
    }
    for (auto& list : adj) {
        std::sort(list.begin(), list.end());
        list.erase(std::unique(list.begin(), list.end()), list.end());
    }
    return adj;
}

float edgeLength(const Mesh& mesh, uint32_t a, uint32_t b) {
    const auto coords = mesh.coord();
    const auto& pa    = coords[a];
    const auto& pb    = coords[b];
    const float dx    = pa.x - pb.x;
    const float dy    = pa.y - pb.y;
    const float dz    = pa.z - pb.z;
    return std::sqrt(dx * dx + dy * dy + dz * dz);
}

/// Dijkstra from @p sources over @p adj, writing into @p dist.
void walk(const Mesh& mesh, const std::vector<std::vector<uint32_t>>& adj,
          std::span<const uint32_t> sources, const std::vector<uint8_t>& inRegion,
          std::vector<float>& dist, std::vector<uint32_t>* prev = nullptr) {
    using Entry = std::pair<float, uint32_t>;
    std::priority_queue<Entry, std::vector<Entry>, std::greater<>> queue;
    for (const uint32_t s : sources) {
        if (s >= dist.size() || inRegion[s] == 0U) continue;
        dist[s] = 0.0F;
        queue.emplace(0.0F, s);
    }

    while (!queue.empty()) {
        const auto [d, v] = queue.top();
        queue.pop();
        if (d > dist[v]) continue;  // a stale entry, already improved on
        for (const uint32_t n : adj[v]) {
            const float step = d + edgeLength(mesh, v, n);
            if (step < dist[n]) {
                dist[n] = step;
                if (prev != nullptr) (*prev)[n] = v;
                queue.emplace(step, n);
            }
        }
    }
}

}  // namespace

std::vector<float> surfaceDistance(const Mesh& mesh, std::span<const uint32_t> region,
                                   std::span<const uint32_t> sources) {
    std::vector<float> dist(mesh.vertexCount(), kInf);
    if (region.empty()) return dist;

    const auto inRegion = regionMask(mesh, region);
    const auto adj      = edgeNeighbours(mesh, region);
    walk(mesh, adj, sources, inRegion, dist);
    return dist;
}

std::vector<uint32_t> spreadOverSurface(const Mesh& mesh, std::span<const uint32_t> region,
                                        size_t count) {
    std::vector<uint32_t> picks;
    if (region.empty() || count == 0) return picks;

    // Region members, deduplicated and in a stable order, so the same request
    // always returns the same roots: a style that moved between runs would be
    // impossible to review by looking at it.
    std::vector<uint32_t> members(region.begin(), region.end());
    std::sort(members.begin(), members.end());
    members.erase(std::unique(members.begin(), members.end()), members.end());
    if (count >= members.size()) return members;

    const auto inRegion = regionMask(mesh, members);
    const auto adj      = edgeNeighbours(mesh, members);

    picks.push_back(members.front());
    std::vector<float> best(mesh.vertexCount(), kInf);
    while (picks.size() < count) {
        // Distance to the NEAREST pick so far, grown one pick at a time rather
        // than recomputed from all of them: seeding the walk with the newest
        // pick and keeping the running minimum is the same answer for less
        // work, because a minimum over sources is what the walk already gives.
        std::vector<float> fromLast(mesh.vertexCount(), kInf);
        const std::array<uint32_t, 1> seed{picks.back()};
        walk(mesh, adj, seed, inRegion, fromLast);
        for (size_t i = 0; i < best.size(); ++i)
            best[i] = std::min(best[i], fromLast[i]);

        // The farthest member, ties broken by the lower index for determinism.
        uint32_t next = members.front();
        float far     = -1.0F;
        for (const uint32_t v : members) {
            if (std::find(picks.begin(), picks.end(), v) != picks.end()) continue;
            // An unreachable member is worth picking -- it is a disconnected
            // island, and skipping it would leave that island bare -- but it
            // must not beat a reachable one on an infinite score, or every
            // island would be taken before the surface was covered.
            const float score = std::isinf(best[v]) ? std::numeric_limits<float>::max() : best[v];
            if (score > far) {
                far  = score;
                next = v;
            }
        }
        picks.push_back(next);
    }
    return picks;
}

std::vector<uint32_t> pathOverSurface(const Mesh& mesh, std::span<const uint32_t> region,
                                      uint32_t source, uint32_t target) {
    std::vector<uint32_t> path;
    if (region.empty()) return path;

    const auto inRegion = regionMask(mesh, region);
    // Bounds only. An explicit in-region check for the two endpoints was here
    // and a mutation proved it could not change any answer: an edge is walkable
    // only when BOTH ends are in the region, so a vertex outside it has no
    // walkable edges -- an excluded source seeds nothing and an excluded target
    // stays unreachable, and either way the path below comes back empty. The
    // BOUNDS check is not redundant: `dist[target]` on an out-of-range target
    // would read past the end.
    if (source >= inRegion.size() || target >= inRegion.size()) return path;

    constexpr uint32_t kNone = std::numeric_limits<uint32_t>::max();
    std::vector<float> dist(mesh.vertexCount(), kInf);
    std::vector<uint32_t> prev(mesh.vertexCount(), kNone);
    const auto adj = edgeNeighbours(mesh, region);
    const std::array<uint32_t, 1> seed{source};
    walk(mesh, adj, seed, inRegion, dist, &prev);

    if (std::isinf(dist[target])) return path;  // no route: empty, not partial

    for (uint32_t v = target; v != kNone; v = prev[v]) {
        path.push_back(v);
    }
    std::reverse(path.begin(), path.end());
    return path;
}

}  // namespace mh::core
