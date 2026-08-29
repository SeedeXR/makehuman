// SPDX-License-Identifier: AGPL-3.0-or-later
#include "makehuman/core/Subdivider.h"

#include <algorithm>
#include <numeric>
#include <unordered_map>

namespace mh::core {
namespace {

constexpr uint64_t packEdge(uint32_t a, uint32_t b) noexcept {
    const uint32_t lo = std::min(a, b);
    const uint32_t hi = std::max(a, b);
    return (static_cast<uint64_t>(lo) << 32) | static_cast<uint64_t>(hi);
}

/// Collects the unique undirected edges of a quad mesh over `indices`
/// (either fvert or fuvs), recording each edge's first and last adjacent face.
///
/// The reference derives the same thing from np.unique's first/last occurrence
/// (catmull_clark_subdivision.py:148-152). first == last means only one face
/// touches the edge, i.e. a boundary — which is exactly the `inedge` test the
/// geometry pass uses (`:420`).
struct EdgeTable {
    std::vector<uint64_t> keys;  ///< sorted unique edge keys
    std::vector<uint32_t> firstFace;
    std::vector<uint32_t> lastFace;
    std::vector<uint32_t> cornerEdge;  ///< per face corner -> edge index
};

EdgeTable buildEdges(std::span<const uint32_t> indices, size_t nFaces, size_t vpp) {
    EdgeTable t;
    const size_t corners = nFaces * vpp;

    std::vector<uint64_t> keys(corners);
    for (size_t f = 0; f < nFaces; ++f) {
        for (size_t c = 0; c < vpp; ++c) {
            const uint32_t a  = indices[f * vpp + c];
            const uint32_t b  = indices[f * vpp + (c + 1) % vpp];
            keys[f * vpp + c] = packEdge(a, b);
        }
    }

    std::vector<uint32_t> order(corners);
    std::iota(order.begin(), order.end(), 0U);
    std::ranges::sort(order, [&](uint32_t x, uint32_t y) { return keys[x] < keys[y]; });

    t.cornerEdge.resize(corners);
    uint64_t prev = 0;
    bool first    = true;
    for (const uint32_t c : order) {
        const uint64_t k = keys[c];
        if (first || k != prev) {
            t.keys.push_back(k);
            t.firstFace.push_back(static_cast<uint32_t>(c / vpp));
            t.lastFace.push_back(static_cast<uint32_t>(c / vpp));
            prev  = k;
            first = false;
        } else {
            t.lastFace.back() = static_cast<uint32_t>(c / vpp);
        }
        t.cornerEdge[c] = static_cast<uint32_t>(t.keys.size() - 1);
    }
    return t;
}

}  // namespace

std::expected<Subdivider, MeshError> Subdivider::build(const Mesh& parent) {
    const size_t vpp = parent.vertsPerPrimitive();
    if (vpp != 4) return std::unexpected(MeshError::FaceArraySizeMismatch);

    const size_t nVerts = parent.vertexCount();
    const size_t nFaces = parent.faceCount();
    const size_t nUVs   = parent.uvCount();
    const bool hasUV    = parent.hasUV();

    Subdivider sd;
    sd.cbase_ = static_cast<uint32_t>(nVerts);
    sd.ebase_ = static_cast<uint32_t>(nVerts + nFaces);

    const EdgeTable ve = buildEdges(parent.fvert(), nFaces, vpp);

    sd.edgeVerts_.resize(ve.keys.size());
    for (size_t e = 0; e < ve.keys.size(); ++e) {
        sd.edgeVerts_[e] = Edge{static_cast<uint32_t>(ve.keys[e] >> 32),
                                static_cast<uint32_t>(ve.keys[e] & 0xFFFFFFFFULL), ve.firstFace[e],
                                ve.lastFace[e]};
    }

    // Parent edges incident to each parent vertex (the reference's vedge/nedges,
    // catmull_clark_subdivision.py:215-230).
    std::vector<uint32_t> counts(nVerts, 0);
    for (const Edge& e : sd.edgeVerts_) {
        if (e.v0 < nVerts) ++counts[e.v0];
        if (e.v1 < nVerts) ++counts[e.v1];
    }
    sd.maxEdgeValence_ = counts.empty() ? 1U : std::max(1U, *std::ranges::max_element(counts));

    sd.vedge_.assign(nVerts * sd.maxEdgeValence_, 0);
    sd.nedges_.assign(nVerts, 0);
    for (size_t e = 0; e < sd.edgeVerts_.size(); ++e) {
        for (const uint32_t v : {sd.edgeVerts_[e].v0, sd.edgeVerts_[e].v1}) {
            if (v >= nVerts) continue;
            uint32_t& n = sd.nedges_[v];
            if (n < sd.maxEdgeValence_) {
                sd.vedge_[v * sd.maxEdgeValence_ + n] = static_cast<uint32_t>(e);
                ++n;
            }
        }
    }

    // ---- build the subdivided mesh -------------------------------------
    const size_t outVerts = nVerts + nFaces + sd.edgeVerts_.size();
    const size_t outFaces = nFaces * 4;

    Mesh out(parent.name() + ".sub", 4);
    for (const FaceGroup& g : parent.faceGroups())
        out.addFaceGroup(g.name);

    // Positions are filled by refresh(); topology only needs the right count.
    if (!out.setCoords(std::vector<Vec3>(outVerts, Vec3{}))) {
        return std::unexpected(MeshError::VertexIndexOutOfRange);
    }

    EdgeTable te;
    size_t outUVs = 0;
    if (hasUV) {
        te     = buildEdges(parent.fuvs(), nFaces, vpp);
        outUVs = nUVs + nFaces + te.keys.size();
    }

    // Each parent quad -> four quads laid out v_k, e_k, c, e_{k-1}
    // (catmull_clark_subdivision.py:172-203).
    std::vector<uint32_t> fvert(outFaces * 4);
    std::vector<uint32_t> fuvs(hasUV ? outFaces * 4 : 0);
    std::vector<uint16_t> group(outFaces);

    const uint32_t tcbase = static_cast<uint32_t>(nUVs);
    const uint32_t tebase = static_cast<uint32_t>(nUVs + nFaces);

    for (size_t f = 0; f < nFaces; ++f) {
        for (size_t k = 0; k < 4; ++k) {
            const size_t of   = f * 4 + k;
            const size_t prev = (k + 3) % 4;

            fvert[of * 4 + 0] = parent.fvert()[f * 4 + k];
            fvert[of * 4 + 1] = sd.ebase_ + ve.cornerEdge[f * 4 + k];
            fvert[of * 4 + 2] = sd.cbase_ + static_cast<uint32_t>(f);
            fvert[of * 4 + 3] = sd.ebase_ + ve.cornerEdge[f * 4 + prev];

            if (hasUV) {
                fuvs[of * 4 + 0] = parent.fuvs()[f * 4 + k];
                fuvs[of * 4 + 1] = tebase + te.cornerEdge[f * 4 + k];
                fuvs[of * 4 + 2] = tcbase + static_cast<uint32_t>(f);
                fuvs[of * 4 + 3] = tebase + te.cornerEdge[f * 4 + prev];
            }
            group[of] = parent.group()[f];
        }
    }

    if (hasUV) {
        // UVs of the new points are fixed by the parent UV layout, so they are
        // computed once here rather than on every refresh.
        std::vector<Vec2> texco(outUVs, Vec2{});
        for (size_t i = 0; i < nUVs; ++i)
            texco[i] = parent.texco()[i];

        for (size_t f = 0; f < nFaces; ++f) {
            Vec2 sum{};
            for (size_t c = 0; c < 4; ++c) {
                const Vec2& uv = parent.texco()[parent.fuvs()[f * 4 + c]];
                sum.x += uv.x;
                sum.y += uv.y;
            }
            texco[tcbase + f] = Vec2{sum.x * 0.25F, sum.y * 0.25F};
        }
        for (size_t e = 0; e < te.keys.size(); ++e) {
            const auto a      = static_cast<uint32_t>(te.keys[e] >> 32);
            const auto b      = static_cast<uint32_t>(te.keys[e] & 0xFFFFFFFFULL);
            const Vec2& ua    = parent.texco()[a];
            const Vec2& ub    = parent.texco()[b];
            texco[tebase + e] = Vec2{(ua.x + ub.x) * 0.5F, (ua.y + ub.y) * 0.5F};
        }
        if (!out.setUVs(std::move(texco))) {
            return std::unexpected(MeshError::UvIndexOutOfRange);
        }
    }

    if (auto r = out.setFaces(std::move(fvert), std::move(fuvs), std::move(group)); !r) {
        return std::unexpected(r.error());
    }
    out.buildAdjacency();

    sd.mesh_             = std::move(out);
    sd.builtVertexCount_ = nVerts;
    sd.builtFaceCount_   = nFaces;
    sd.refresh(parent);
    return sd;
}

void Subdivider::refresh(const Mesh& parent) {
    if (!matches(parent)) return;

    const size_t nVerts = parent.vertexCount();
    const size_t nFaces = parent.faceCount();
    auto out            = mesh_.mutableCoord();

    // --- face points: the average of the parent face's four corners (:404) ---
    for (size_t f = 0; f < nFaces; ++f) {
        Vec3 sum{};
        for (size_t c = 0; c < 4; ++c)
            sum += parent.coord()[parent.fvert()[f * 4 + c]];
        out[cbase_ + f] = sum * 0.25F;
    }

    // --- edge points (:415-426) ---------------------------------------------
    // Boundary edge (one adjacent face): the edge midpoint.
    // Interior edge: the average of the two endpoints and the two face points.
    for (size_t e = 0; e < edgeVerts_.size(); ++e) {
        const Edge& ed = edgeVerts_[e];
        const Vec3 m   = parent.coord()[ed.v0] + parent.coord()[ed.v1];
        if (ed.f0 == ed.f1) {
            out[ebase_ + e] = m * 0.5F;
        } else {
            out[ebase_ + e] = (m + out[cbase_ + ed.f0] + out[cbase_ + ed.f1]) * 0.25F;
        }
    }

    // --- repositioned base points (:436-449) --------------------------------
    for (size_t v = 0; v < nVerts; ++v) {
        const uint32_t nEdge = nedges_[v];
        const uint32_t nFace = parent.nfacesAt(v);
        const Vec3& P        = parent.coord()[v];

        // R: mean of the midpoints of the edges at v.
        // Also the sum of boundary-edge midpoints, and how many there are.
        Vec3 R{};
        Vec3 boundarySum{};
        uint32_t nBoundary = 0;
        for (uint32_t k = 0; k < nEdge; ++k) {
            const Edge& ed = edgeVerts_[vedge_[v * maxEdgeValence_ + k]];
            const Vec3 mid = (parent.coord()[ed.v0] + parent.coord()[ed.v1]) * 0.5F;
            R += mid;
            if (ed.f0 == ed.f1) {
                boundarySum += mid;
                ++nBoundary;
            }
        }
        if (nEdge > 0) R *= 1.0F / static_cast<float>(nEdge);

        // F: mean of the face points of the faces at v.
        Vec3 F{};
        for (uint32_t k = 0; k < nFace; ++k) {
            F += out[cbase_ + parent.faceAt(v, k)];
        }
        if (nFace > 0) F *= 1.0F / static_cast<float>(nFace);

        if (nFace >= 3) {
            if (nEdge == nFace) {
                // Interior: the standard Catmull-Clark rule (F + 2R + (n-3)P)/n.
                const float n = static_cast<float>(nFace);
                out[v]        = (F + R * 2.0F + P * (n - 3.0F)) * (1.0F / n);
            } else {
                // Boundary: (sum of boundary edge midpoints + P) / (count + 1).
                out[v] = (boundarySum + P) * (1.0F / static_cast<float>(nBoundary + 1));
            }
        } else {
            // Valence below 3 has no well-defined limit surface; the reference
            // extrapolates (3R - F)/2 (:449). Replicated for parity.
            out[v] = (R * 3.0F - F) * 0.5F;
        }
    }

    mesh_.calcNormals();
}

}  // namespace mh::core
