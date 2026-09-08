// SPDX-License-Identifier: AGPL-3.0-or-later
#include "makehuman/core/Decimator.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <queue>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace mh::core {
namespace {

/// How far a collapse may move the surface, as a fraction of the mesh's
/// bounding-box diagonal.
///
/// **Without a ceiling this decimator destroys the model to hit a number.**
/// Unbounded, the base mesh reduces to 4,373 triangles and every numeric
/// invariant in the test file passes on the result: the count is right, no
/// triangle is degenerate or duplicated, the bounding box is within 5 mm, no
/// normal has flipped. Rendered in Blender, the chest and shoulders had
/// collapsed into a flat triangular sheet, the legs were welded at the top and
/// the head was a faceted wedge. Only the picture showed it.
///
/// **Chosen by rendering the boundary, not by taste.** With the ceiling at
/// 0.004 of the diagonal the base mesh floors at 5,451 triangles and the body
/// still reads correctly -- separate legs, a head-shaped head, a clean
/// silhouette. At 0.008 it floors at 4,946 and the head is already a wedge and
/// the shoulders are flattening. Both were looked at. 0.004 of the ~19.85 dm
/// diagonal is about 8 mm, which is also the scale below which the reference's
/// own helper geometry stops resolving.
///
/// It is a fraction of the diagonal rather than an absolute distance so that
/// it transfers: the same number means the same visual tolerance on a hand-
/// sized proxy and on a whole body.
constexpr double kMaxErrorFraction = 0.004;

/// A symmetric 4x4 quadric, stored as its 10 distinct coefficients.
///
/// Q(v) = v^T A v + 2 b^T v + c for a plane (n, d): A = n n^T, b = d n,
/// c = d^2. Summed over a vertex's incident planes, Q(v) is the sum of squared
/// distances from v to those planes -- which is the whole idea: it says how far
/// a candidate position is from the surface that used to be here.
struct Quadric {
    std::array<double, 10> q{};  // a00 a01 a02 a03 a11 a12 a13 a22 a23 a33

    /// Total plane weight folded in -- twice the summed triangle area. `at()`
    /// is a sum of area-weighted squared distances, so dividing by this and
    /// taking the root turns it back into a LENGTH, which is the only form in
    /// which an error threshold can be compared against a mesh's size.
    double weight{};

    void addPlane(double a, double b, double c, double d) {
        q[0] += a * a;
        q[1] += a * b;
        q[2] += a * c;
        q[3] += a * d;
        q[4] += b * b;
        q[5] += b * c;
        q[6] += b * d;
        q[7] += c * c;
        q[8] += c * d;
        q[9] += d * d;
    }

    [[nodiscard]] Quadric operator+(const Quadric& o) const {
        Quadric r;
        for (size_t i = 0; i < 10; ++i)
            r.q[i] = q[i] + o.q[i];
        r.weight = weight + o.weight;
        return r;
    }

    /// Root-mean-square distance from @p v to the planes this quadric holds,
    /// in model units.
    [[nodiscard]] double rms(const Vec3& v) const {
        return weight > 0.0 ? std::sqrt(at(v) / weight) : 0.0;
    }

    /// v^T Q v, clamped at zero: the true value is a sum of squares, so a
    /// negative here is float cancellation and never information.
    [[nodiscard]] double at(const Vec3& v) const {
        const double x = static_cast<double>(v.x);
        const double y = static_cast<double>(v.y);
        const double z = static_cast<double>(v.z);
        const double e = q[0] * x * x + 2 * q[1] * x * y + 2 * q[2] * x * z + 2 * q[3] * x +
                         q[4] * y * y + 2 * q[5] * y * z + 2 * q[6] * y + q[7] * z * z +
                         2 * q[8] * z + q[9];
        return e < 0.0 ? 0.0 : e;
    }
};

/// The position minimising Q, i.e. the solution of the 3x3 system A v = -b.
///
/// Returns false when A is singular -- a flat or straight-edged neighbourhood,
/// where every point along a line or plane is equally good and there is no
/// single answer to return. The caller falls back to the edge's midpoint.
bool minimiser(const Quadric& q, Vec3& out) {
    const double a00 = q.q[0];
    const double a01 = q.q[1];
    const double a02 = q.q[2];
    const double a11 = q.q[4];
    const double a12 = q.q[5];
    const double a22 = q.q[7];

    const double c00 = a11 * a22 - a12 * a12;
    const double c01 = a02 * a12 - a01 * a22;
    const double c02 = a01 * a12 - a02 * a11;
    const double det = a00 * c00 + a01 * c01 + a02 * c02;

    // Scale-relative, because these are decimetres cubed: a fixed epsilon
    // would call a legitimately small body-scale system singular.
    const double scale = std::abs(a00) + std::abs(a11) + std::abs(a22) + 1e-30;
    if (std::abs(det) < 1e-12 * scale * scale * scale) return false;

    const double c11 = a00 * a22 - a02 * a02;
    const double c12 = a02 * a01 - a00 * a12;
    const double c22 = a00 * a11 - a01 * a01;

    const double b0 = -q.q[3];
    const double b1 = -q.q[6];
    const double b2 = -q.q[8];

    out = {static_cast<float>((c00 * b0 + c01 * b1 + c02 * b2) / det),
           static_cast<float>((c01 * b0 + c11 * b1 + c12 * b2) / det),
           static_cast<float>((c02 * b0 + c12 * b1 + c22 * b2) / det)};
    return true;
}

struct Tri {
    std::array<uint32_t, 3> v{};
    uint16_t group{};
    std::array<uint32_t, 3> uv{};
    bool live{true};
};

/// An edge, keyed low-index-first so (a,b) and (b,a) are one thing.
uint64_t edgeKey(uint32_t a, uint32_t b) {
    const uint64_t lo = std::min(a, b);
    const uint64_t hi = std::max(a, b);
    return (lo << 32) | hi;
}

struct Candidate {
    double cost{};
    uint32_t a{};
    uint32_t b{};
    uint64_t stamp{};  ///< version of the edge when this entry was pushed
    size_t tiebreak{};

    /// Cheapest first; ties by insertion order, so the queue is deterministic
    /// rather than dependent on the heap's internal shuffling.
    bool operator<(const Candidate& o) const {
        if (cost != o.cost) return cost > o.cost;
        return tiebreak > o.tiebreak;
    }
};

/// `foundation::dot` is float, and an area-weighted plane equation in
/// decimetres loses too much to it. Everything else -- `operator-`, `cross` --
/// comes from foundation unchanged.
double dotd(const Vec3& a, const Vec3& b) {
    return static_cast<double>(a.x) * static_cast<double>(b.x) +
           static_cast<double>(a.y) * static_cast<double>(b.y) +
           static_cast<double>(a.z) * static_cast<double>(b.z);
}

/// Unnormalised triangle normal; its length is twice the area.
Vec3 faceNormal(const Vec3& p0, const Vec3& p1, const Vec3& p2) {
    return foundation::cross(p1 - p0, p2 - p0);
}

}  // namespace

std::string DecimateError::message() const {
    switch (kind) {
        case DecimateErrorKind::BadRatio:
            return "the decimation ratio must be a number in (0, 1], got " + detail;
        case DecimateErrorKind::NoFaces: return "nothing to decimate: the mesh has no faces";
    }
    return "unknown decimation error";
}

std::expected<Mesh, DecimateError> decimate(const Mesh& src, DecimateOptions options,
                                            std::vector<uint32_t>* sourceVertex) {
    if (!std::isfinite(options.ratio) || options.ratio <= 0.0F || options.ratio > 1.0F) {
        return std::unexpected(DecimateError{.kind   = DecimateErrorKind::BadRatio,
                                             .detail = std::to_string(options.ratio)});
    }
    if (src.faceCount() == 0) {
        return std::unexpected(DecimateError{.kind = DecimateErrorKind::NoFaces, .detail = {}});
    }

    // ---- triangulate -----------------------------------------------------
    // A quad becomes (0,1,2) and (0,2,3); a degenerate quad (corner 3 == corner
    // 0) is already a triangle and contributes one.
    const auto fv        = src.fvert();
    const auto fu        = src.fuvs();
    const bool uvs       = src.hasUV();
    const size_t perFace = src.vertsPerPrimitive();
    std::vector<Tri> tris;
    tris.reserve(src.faceCount() * 2);
    for (size_t f = 0; f < src.faceCount(); ++f) {
        const uint16_t g    = f < src.group().size() ? src.group()[f] : uint16_t{0};
        const auto corner   = [&](size_t i) { return fv[f * perFace + i]; };
        const auto uvAt     = [&](size_t i) { return uvs ? fu[f * perFace + i] : 0U; };
        const bool triangle = perFace == 4 && corner(0) == corner(3);
        tris.push_back({.v     = {corner(0), corner(1), corner(2)},
                        .group = g,
                        .uv    = {uvAt(0), uvAt(1), uvAt(2)}});
        if (perFace == 4 && !triangle) {
            tris.push_back({.v     = {corner(0), corner(2), corner(3)},
                            .group = g,
                            .uv    = {uvAt(0), uvAt(2), uvAt(3)}});
        }
    }

    const size_t startTris = tris.size();
    const auto target      = static_cast<size_t>(
        std::llround(static_cast<double>(startTris) * static_cast<double>(options.ratio)));

    // ---- per-vertex state ------------------------------------------------
    const size_t nv = src.vertexCount();
    std::vector<Vec3> pos(src.coord().begin(), src.coord().end());
    std::vector<Quadric> quad(nv);
    std::vector<std::vector<size_t>> vtris(nv);
    std::vector<uint8_t> alive(nv, 0);

    for (size_t t = 0; t < tris.size(); ++t) {
        const Tri& tr    = tris[t];
        const Vec3 n     = faceNormal(pos[tr.v[0]], pos[tr.v[1]], pos[tr.v[2]]);
        const double len = std::sqrt(dotd(n, n));
        if (len > 0.0) {
            const double a = static_cast<double>(n.x) / len;
            const double b = static_cast<double>(n.y) / len;
            const double c = static_cast<double>(n.z) / len;
            const Vec3& p0 = pos[tr.v[0]];
            const double d = -(a * static_cast<double>(p0.x) + b * static_cast<double>(p0.y) +
                               c * static_cast<double>(p0.z));
            // Weighted by area, which is what makes a big flat region cheap to
            // simplify and a small detailed one expensive.
            Quadric contribution;
            contribution.addPlane(a, b, c, d);
            for (size_t i = 0; i < 10; ++i)
                contribution.q[i] *= len;
            contribution.weight = len;
            for (const uint32_t v : tr.v)
                quad[v] = quad[v] + contribution;
        }
        for (const uint32_t v : tr.v) {
            vtris[v].push_back(t);
            alive[v] = 1;
        }
    }

    // A vertex's UV set and group set, for the attribute-boundary refusals.
    std::vector<std::unordered_set<uint32_t>> vuv(nv);
    std::vector<std::unordered_set<uint16_t>> vgroup(nv);
    for (const Tri& tr : tris) {
        for (size_t i = 0; i < 3; ++i) {
            if (uvs) vuv[tr.v[i]].insert(tr.uv[i]);
            vgroup[tr.v[i]].insert(tr.group);
        }
    }

    // ---- boundary constraint planes --------------------------------------
    // Without this a decimator eats its own silhouette: every triangle at an
    // open edge is coplanar with its neighbours, so the quadric has nothing to
    // say about moving that edge INWARDS and the outline creeps in. The fix is
    // Garland & Heckbert's: for each edge with one incident triangle, add a
    // plane through the edge PERPENDICULAR to that triangle, so leaving the
    // boundary is expensive. It also protects the base mesh's real boundary
    // loops -- eye sockets, the mouth bag, every helper cage's open rim.
    {
        std::unordered_map<uint64_t, std::pair<uint32_t, size_t>> edgeFaces;
        for (size_t t = 0; t < tris.size(); ++t) {
            for (size_t i = 0; i < 3; ++i) {
                auto& e = edgeFaces[edgeKey(tris[t].v[i], tris[t].v[(i + 1) % 3])];
                ++e.first;
                e.second = t;
            }
        }
        for (const auto& [key, use] : edgeFaces) {
            if (use.first != 1) continue;
            const auto a     = static_cast<uint32_t>(key >> 32);
            const auto b     = static_cast<uint32_t>(key & 0xFFFFFFFFULL);
            const Tri& t     = tris[use.second];
            const Vec3 n     = faceNormal(pos[t.v[0]], pos[t.v[1]], pos[t.v[2]]);
            const Vec3 along = pos[b] - pos[a];
            const Vec3 c     = foundation::cross(along, n);
            const double len = std::sqrt(dotd(c, c));
            if (len <= 0.0) continue;
            const double px = static_cast<double>(c.x) / len;
            const double py = static_cast<double>(c.y) / len;
            const double pz = static_cast<double>(c.z) / len;
            const double pd =
                -(px * static_cast<double>(pos[a].x) + py * static_cast<double>(pos[a].y) +
                  pz * static_cast<double>(pos[a].z));
            // Weighted by edge length like the face quadrics, then by a factor
            // that makes the boundary term dominate a coplanar interior.
            Quadric constraint;
            constraint.addPlane(px, py, pz, pd);
            const double w = std::sqrt(dotd(along, along)) * 1000.0;
            for (size_t i = 0; i < 10; ++i)
                constraint.q[i] *= w;
            // Deliberately NOT added to `weight`: the boundary term is a
            // penalty, not surface area, and folding it in would divide the
            // error by a thousand and hide exactly the collapses it exists to
            // make expensive.
            quad[a] = quad[a] + constraint;
            quad[b] = quad[b] + constraint;
        }
    }

    // ---- the error ceiling -----------------------------------------------
    Vec3 lo{pos[0]};
    Vec3 hi{pos[0]};
    for (const Vec3& v : pos) {
        lo = {std::min(lo.x, v.x), std::min(lo.y, v.y), std::min(lo.z, v.z)};
        hi = {std::max(hi.x, v.x), std::max(hi.y, v.y), std::max(hi.z, v.z)};
    }
    const Vec3 diagonal   = hi - lo;
    const double maxError = std::sqrt(dotd(diagonal, diagonal)) * kMaxErrorFraction;

    // ---- the queue -------------------------------------------------------
    std::vector<uint64_t> version(nv, 0);
    std::priority_queue<Candidate> queue;
    size_t pushed = 0;

    const auto costOf = [&](uint32_t a, uint32_t b, Vec3& where) {
        const Quadric sum = quad[a] + quad[b];
        if (!minimiser(sum, where)) {
            where = {(pos[a].x + pos[b].x) * 0.5F, (pos[a].y + pos[b].y) * 0.5F,
                     (pos[a].z + pos[b].z) * 0.5F};
        }
        return sum.at(where);
    };

    const auto push = [&](uint32_t a, uint32_t b) {
        Vec3 where;
        queue.push({.cost     = costOf(a, b, where),
                    .a        = a,
                    .b        = b,
                    .stamp    = version[a] + version[b],
                    .tiebreak = pushed++});
    };

    {
        std::unordered_set<uint64_t> seen;
        for (const Tri& tr : tris) {
            for (size_t i = 0; i < 3; ++i) {
                const uint32_t a = tr.v[i];
                const uint32_t b = tr.v[(i + 1) % 3];
                if (seen.insert(edgeKey(a, b)).second) push(std::min(a, b), std::max(a, b));
            }
        }
    }

    // ---- collapse --------------------------------------------------------
    size_t liveTris = startTris;
    while (liveTris > target && !queue.empty()) {
        const Candidate top = queue.top();
        queue.pop();
        const uint32_t a = top.a;
        const uint32_t b = top.b;
        if (alive[a] == 0 || alive[b] == 0) continue;
        // Stale: one endpoint has moved since this cost was computed, so the
        // cost is a lie. Re-pushed rather than trusted.
        if (top.stamp != version[a] + version[b]) {
            push(a, b);
            continue;
        }

        // Where the survivor will land. Computed once and reused by the
        // ceiling and the flip test below: two calls would be two chances for
        // them to disagree about the position they are judging.
        Vec3 to;
        (void)costOf(a, b, to);

        // Too destructive to be worth the triangle. Refusing here is what
        // makes an over-ambitious ratio stop at a mesh that still looks like
        // the model instead of reaching the number and looking like a cloak.
        if ((quad[a] + quad[b]).rms(to) > maxError) continue;

        // Attribute boundaries: refuse rather than interpolate. See the header.
        if (vgroup[a] != vgroup[b]) continue;
        // A vertex carrying more than one UV index sits ON a seam, and merging
        // it would smear the texture across the cut. Refuse those; an interior
        // vertex has exactly one UV and the survivor keeps it.
        //
        // The first version of this compared the two UV SETS for equality,
        // which refused EVERY collapse on the base mesh: distinct vertices
        // have distinct UV indices there, so the sets are never equal. It
        // reduced 36,972 triangles to 36,972 and reported success.
        if (uvs && (vuv[a].size() != 1 || vuv[b].size() != 1)) continue;

        // The faces that vanish (both endpoints) and the ones that survive but
        // move (exactly one endpoint).
        std::vector<size_t> collapsing;
        std::vector<size_t> moving;
        for (const size_t t : vtris[a]) {
            if (!tris[t].live) continue;
            const bool hasB = std::ranges::find(tris[t].v, b) != tris[t].v.end();
            (hasB ? collapsing : moving).push_back(t);
        }
        for (const size_t t : vtris[b]) {
            if (!tris[t].live) continue;
            if (std::ranges::find(tris[t].v, a) == tris[t].v.end()) moving.push_back(t);
        }
        // An edge with no shared face is not an edge of this surface any more.
        if (collapsing.empty()) continue;

        // **The flip test below is the only topological guard, and that is
        // measured rather than assumed.** Two textbook guards were written
        // here and both are gone:
        //
        //   - The LINK CONDITION (the one-rings of a and b may share exactly
        //     the vertices opposite the edge). It fired 170 times on the base
        //     mesh, so it was not dead -- but removing it changed neither the
        //     triangle nor the vertex count at any ratio, and left every mesh
        //     manifold with "no edge in more than two triangles" asserted,
        //     base mesh included. Three set allocations per candidate,
        //     buying nothing the flip test does not already catch.
        //   - The TETRAHEDRON guard (`moving.empty()`, for the smallest closed
        //     surface). Instrumented, it fired ZERO times: not on the base
        //     mesh, and not on the closed octahedron the test file decimates
        //     by 90% specifically to reach that case.
        //
        // Both invariants are still ASSERTED -- manifoldness, no duplicate
        // triangle, no degenerate triangle -- so if a mesh ever does need one
        // of those guards the test fails rather than the file being quietly
        // wrong, and it comes back with a case that proves it.

        // Flip test, on the faces that survive: a collapse that turns a
        // triangle inside out makes a visible crease and a wrong normal.
        bool flips = false;
        for (const size_t t : moving) {
            std::array<Vec3, 3> p{};
            for (size_t i = 0; i < 3 && !flips; ++i) {
                const uint32_t v = tris[t].v[i];
                p[i]             = (v == a || v == b) ? to : pos[v];
            }
            const Vec3 before = faceNormal(pos[tris[t].v[0]], pos[tris[t].v[1]], pos[tris[t].v[2]]);
            const Vec3 after  = faceNormal(p[0], p[1], p[2]);
            if (dotd(before, after) <= 0.0) flips = true;
            if (flips) break;
        }
        if (flips) continue;

        // Commit. `a` survives at the minimiser; `b` is gone.
        pos[a]   = to;
        quad[a]  = quad[a] + quad[b];
        alive[b] = 0;
        ++version[a];
        ++version[b];

        for (const size_t t : collapsing) {
            tris[t].live = false;
            --liveTris;
        }
        for (const size_t t : vtris[b]) {
            if (!tris[t].live) continue;
            for (size_t i = 0; i < 3; ++i) {
                if (tris[t].v[i] == b) {
                    tris[t].v[i] = a;
                    // The surviving corner takes a's UV, which is a's own
                    // because a seam collapse was refused above.
                    if (uvs) tris[t].uv[i] = *vuv[a].begin();
                }
            }
            vtris[a].push_back(t);
        }
        for (const uint16_t g : vgroup[b])
            vgroup[a].insert(g);

        // Re-cost every edge that now touches a moved vertex.
        std::unordered_set<uint32_t> ring;
        for (const size_t t : vtris[a]) {
            if (!tris[t].live) continue;
            for (const uint32_t v : tris[t].v) {
                if (v != a && alive[v] != 0) ring.insert(v);
            }
        }
        for (const uint32_t v : ring)
            push(std::min(a, v), std::max(a, v));
    }

    // ---- compact ---------------------------------------------------------
    std::vector<uint32_t> remap(nv, std::numeric_limits<uint32_t>::max());
    std::vector<Vec3> outCoords;
    if (sourceVertex != nullptr) sourceVertex->clear();
    for (size_t v = 0; v < nv; ++v) {
        bool referenced = false;
        for (const size_t t : vtris[v]) {
            if (tris[t].live) {
                referenced = true;
                break;
            }
        }
        if (!referenced || alive[v] == 0) continue;
        remap[v] = static_cast<uint32_t>(outCoords.size());
        outCoords.push_back(pos[v]);
        if (sourceVertex != nullptr) sourceVertex->push_back(static_cast<uint32_t>(v));
    }

    std::vector<uint32_t> outFv;
    std::vector<uint32_t> outFu;
    std::vector<uint16_t> outGroup;
    for (const Tri& tr : tris) {
        if (!tr.live) continue;
        for (size_t i = 0; i < 3; ++i)
            outFv.push_back(remap[tr.v[i]]);
        outFv.push_back(remap[tr.v[0]]);  // degenerate 4th corner
        if (uvs) {
            for (size_t i = 0; i < 3; ++i)
                outFu.push_back(tr.uv[i]);
            outFu.push_back(tr.uv[0]);
        }
        outGroup.push_back(tr.group);
    }

    // UVs are compacted as well: a quarter-size mesh still carrying all 21,334
    // of the base mesh's UVs is not a level of detail.
    std::vector<Vec2> outUv;
    if (uvs) {
        std::vector<uint32_t> uvRemap(src.uvCount(), std::numeric_limits<uint32_t>::max());
        for (uint32_t& u : outFu) {
            if (uvRemap[u] == std::numeric_limits<uint32_t>::max()) {
                uvRemap[u] = static_cast<uint32_t>(outUv.size());
                outUv.push_back(src.texco()[u]);
            }
            u = uvRemap[u];
        }
    }

    Mesh out(src.name(), 4);
    for (const FaceGroup& g : src.faceGroups())
        out.addFaceGroup(g.name);
    if (auto set = out.setCoords(std::move(outCoords)); !set) {
        return std::unexpected(DecimateError{.kind = DecimateErrorKind::NoFaces, .detail = {}});
    }
    if (uvs) {
        if (auto set = out.setUVs(std::move(outUv)); !set) {
            return std::unexpected(DecimateError{.kind = DecimateErrorKind::NoFaces, .detail = {}});
        }
    }
    if (auto set = out.setFaces(std::move(outFv), std::move(outFu), std::move(outGroup)); !set) {
        return std::unexpected(DecimateError{.kind = DecimateErrorKind::NoFaces, .detail = {}});
    }
    out.calcNormals();
    return out;
}

}  // namespace mh::core
