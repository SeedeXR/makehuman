// SPDX-License-Identifier: AGPL-3.0-or-later
#include "makehuman/core/Mesh.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

namespace mh::core {

Mesh::Mesh(std::string name, uint8_t vertsPerPrimitive)
    : name_(std::move(name)),
      vertsPerPrimitive_(vertsPerPrimitive),
      vertsPerFaceForExport_(vertsPerPrimitive) {}

std::expected<void, MeshError> Mesh::setCoords(std::vector<Vec3> coords) {
    if (!fvert_.empty()) {
        const auto n = static_cast<uint32_t>(coords.size());
        for (const uint32_t v : fvert_) {
            if (v >= n) return std::unexpected(MeshError::VertexIndexOutOfRange);
        }
    }

    coord_     = std::move(coords);
    origCoord_ = coord_;  // the morph base, module3d.py:532
    // Left EMPTY, not zero-filled: a size-based "do I have normals?" guard is
    // only meaningful if the absent state is distinguishable. Zero-filling made
    // calcVertexTangents orthogonalise against the zero vector.
    vnorm_.clear();
    vtang_.clear();
    return {};
}

std::expected<void, MeshError> Mesh::setUVs(std::vector<Vec2> uvs) {
    if (!fuvs_.empty()) {
        const auto n = static_cast<uint32_t>(uvs.size());
        for (const uint32_t t : fuvs_) {
            if (t >= n) return std::unexpected(MeshError::UvIndexOutOfRange);
        }
    }
    texco_ = std::move(uvs);
    return {};
}

std::expected<void, MeshError> Mesh::setFaces(std::vector<uint32_t> faceVerts,
                                              std::vector<uint32_t> faceUVs,
                                              std::vector<uint16_t> faceGroup) {
    const size_t vpp = vertsPerPrimitive_;
    if (vpp == 0 || faceVerts.size() % vpp != 0) {
        return std::unexpected(MeshError::FaceArraySizeMismatch);
    }
    if (!faceUVs.empty() && faceUVs.size() != faceVerts.size()) {
        return std::unexpected(MeshError::UvArraySizeMismatch);
    }

    // Validate BEFORE mutating: calcFaceNormals and calcVertexNormals index
    // coord_/texco_ through these arrays without bounds checks, so an invalid
    // index here becomes an out-of-bounds read later.
    const auto nVerts = static_cast<uint32_t>(coord_.size());
    for (const uint32_t v : faceVerts) {
        if (v >= nVerts) return std::unexpected(MeshError::VertexIndexOutOfRange);
    }
    if (!faceUVs.empty()) {
        const auto nUVs = static_cast<uint32_t>(texco_.size());
        for (const uint32_t t : faceUVs) {
            if (t >= nUVs) return std::unexpected(MeshError::UvIndexOutOfRange);
        }
    }

    fvert_ = std::move(faceVerts);
    fuvs_  = std::move(faceUVs);

    const size_t nFaces = faceCount();
    group_              = std::move(faceGroup);
    if (group_.size() != nFaces) {
        group_.assign(nFaces, 0);
    }
    // The face set just changed, so any adjacency built from the previous one
    // is stale and its indices may point past the new fnorm_. Clearing forces
    // calcVertexNormals to rebuild instead of reading through dead indices.
    vface_.clear();
    nfaces_.clear();
    ++topologyVersion_;
    fnorm_.assign(nFaces, Vec3{});
    vnorm_.clear();
    vtang_.clear();

    // A triangle mesh is stored as quads with corner 0 repeated. The reference
    // decides this from the FIRST face only (module3d.py:634-639); a mixed
    // tri/quad mesh is therefore mis-tagged. We replicate the check but over
    // every face, so a mixed mesh stays tagged as quads rather than silently
    // exporting as triangles.
    vertsPerFaceForExport_ = vertsPerPrimitive_;
    if (vertsPerPrimitive_ == 4 && nFaces > 0) {
        bool allDegenerate = true;
        for (size_t f = 0; f < nFaces; ++f) {
            if (fvert_[f * 4] != fvert_[f * 4 + 3]) {
                allDegenerate = false;
                break;
            }
        }
        if (allDegenerate) {
            vertsPerFaceForExport_ = 3;
        }
    }
    return {};
}

uint16_t Mesh::addFaceGroup(std::string name) {
    if (const auto it = groupsByName_.find(name); it != groupsByName_.end()) {
        return it->second;
    }
    const auto idx = static_cast<uint16_t>(faceGroups_.size());
    faceGroups_.push_back(FaceGroup{name, idx});
    groupsByName_.emplace(std::move(name), idx);
    return idx;
}

std::optional<uint16_t> Mesh::findFaceGroup(std::string_view name) const {
    const auto it = groupsByName_.find(std::string{name});
    if (it == groupsByName_.end()) return std::nullopt;
    return it->second;
}

void Mesh::resetToOriginal() {
    coord_ = origCoord_;
}

void Mesh::buildAdjacency() {
    const size_t nVerts = coord_.size();
    const size_t nFaces = faceCount();
    const size_t vpp    = vertsPerPrimitive_;

    // Pass 1: count incident faces per vertex. A vertex may appear more than
    // once in a degenerate quad; count it once per face, matching the
    // reference's per-face-per-corner append (module3d.py:709-717).
    std::vector<uint32_t> counts(nVerts, 0);
    for (size_t f = 0; f < nFaces; ++f) {
        for (size_t c = 0; c < vpp; ++c) {
            const uint32_t v    = fvert_[f * vpp + c];
            bool seenInThisFace = false;
            for (size_t p = 0; p < c; ++p) {
                if (fvert_[f * vpp + p] == v) {
                    seenInThisFace = true;
                    break;
                }
            }
            if (!seenInThisFace && v < nVerts) {
                ++counts[v];
            }
        }
    }

    const uint32_t maxCount = counts.empty() ? 0U : *std::ranges::max_element(counts);
    // Floored at 4 (module3d.py:764-765). Kept 32-bit: a uint8_t silently wraps
    // at 256 incident faces, which would zero the adjacency stride and turn
    // every vertex normal into the zero-guard fallback with no diagnostic.
    maxValence_ = std::max<uint32_t>(4U, maxCount);

    // Pass 2: fill. Flat array, stride = maxValence_.
    vface_.assign(nVerts * maxValence_, 0);
    nfaces_.assign(nVerts, 0);
    for (size_t f = 0; f < nFaces; ++f) {
        for (size_t c = 0; c < vpp; ++c) {
            const uint32_t v = fvert_[f * vpp + c];
            if (v >= nVerts) continue;
            bool seenInThisFace = false;
            for (size_t p = 0; p < c; ++p) {
                if (fvert_[f * vpp + p] == v) {
                    seenInThisFace = true;
                    break;
                }
            }
            if (seenInThisFace) continue;
            uint32_t& n = nfaces_[v];
            if (n < maxValence_) {
                vface_[v * maxValence_ + n] = static_cast<uint32_t>(f);
                ++n;
            }
        }
    }
}

void Mesh::calcFaceNormals() {
    const size_t nFaces = faceCount();
    const size_t vpp    = vertsPerPrimitive_;
    fnorm_.assign(nFaces, Vec3{});
    if (vpp < 3) return;

    for (size_t f = 0; f < nFaces; ++f) {
        const Vec3& v1 = coord_[fvert_[f * vpp + 0]];
        const Vec3& v2 = coord_[fvert_[f * vpp + 1]];
        const Vec3& v3 = coord_[fvert_[f * vpp + 2]];
        // Deliberately NOT normalised: the magnitude is twice the triangle area,
        // which is what makes the vertex-normal sum area-weighted.
        // Reference: module3d.py:339-341, :353.
        fnorm_[f] = cross(v1 - v2, v2 - v3);
    }
}

void Mesh::calcVertexNormals() {
    const size_t nVerts = coord_.size();
    vnorm_.assign(nVerts, Vec3{});
    if (vface_.size() != nVerts * static_cast<size_t>(maxValence_)) {
        buildAdjacency();
    }

    for (size_t v = 0; v < nVerts; ++v) {
        Vec3 sum{};
        const uint32_t n = nfaces_[v];
        for (uint32_t k = 0; k < n; ++k) {
            sum += fnorm_[vface_[v * maxValence_ + k]];
        }
        const float len = std::sqrt(dot(sum, sum));
        // The reference divides unconditionally (module3d.py:368) and produces
        // NaN for an isolated vertex. Guarding is a deliberate divergence.
        vnorm_[v] = (len > 0.0F) ? sum * (1.0F / len) : Vec3{0.0F, 1.0F, 0.0F};
    }
}

void Mesh::calcVertexTangents() {
    vtang_.clear();
    if (!hasUV()) return;  // module3d.py:375-376 -- no UVs, no tangent basis

    const size_t nVerts = coord_.size();
    const size_t nFaces = faceCount();
    const size_t vpp    = vertsPerPrimitive_;
    if (vpp < 3 || nVerts == 0) return;

    if (vnorm_.size() != nVerts) calcNormals();

    // Accumulate per TRIANGLE, over the same fan a renderer draws:
    // (0,1,2), (0,2,3), ... Building one basis from corners 0,1,2 and
    // broadcasting it to the whole face would give corner 3 a basis from a
    // triangle it is not part of, and would discard triangle (0,2,3) entirely.
    std::vector<Vec3> sAcc(nVerts, Vec3{});
    std::vector<Vec3> tAcc(nVerts, Vec3{});

    for (size_t f = 0; f < nFaces; ++f) {
        const size_t base = f * vpp;

        for (size_t c = 2; c < vpp; ++c) {
            const uint32_t i0 = fvert_[base + 0];
            const uint32_t i1 = fvert_[base + c - 1];
            const uint32_t i2 = fvert_[base + c];
            // A repeated corner marks a triangle stored as a degenerate quad
            // (wavefront.py:105-106); it has no area and no basis.
            if (i0 == i1 || i1 == i2 || i0 == i2) continue;

            const Vec3& p1 = coord_[i0];
            const Vec3& p2 = coord_[i1];
            const Vec3& p3 = coord_[i2];
            const Vec2& w1 = texco_[fuvs_[base + 0]];
            const Vec2& w2 = texco_[fuvs_[base + c - 1]];
            const Vec2& w3 = texco_[fuvs_[base + c]];

            const float x1 = p2.x - p1.x, x2 = p3.x - p1.x;
            const float y1 = p2.y - p1.y, y2 = p3.y - p1.y;
            const float z1 = p2.z - p1.z, z2 = p3.z - p1.z;

            const float s1 = w2.x - w1.x, s2 = w3.x - w1.x;
            const float t1 = w2.y - w1.y, t2 = w3.y - w1.y;

            // A degenerate UV triangle has no tangent basis. The reference
            // nudges each zero delta to 1e-7 (module3d.py:414-417), inventing a
            // direction from nothing; skipping lets neighbours decide instead.
            const float det = s1 * t2 - s2 * t1;
            if (std::abs(det) < 1e-12F) continue;
            const float r = 1.0F / det;

            // Note: these are dPosition/dUV, so a face with a small UV
            // footprint carries a LARGER weight. That is Lengyel's method as
            // published; it is not area weighting.
            const Vec3 sd{(t2 * x1 - t1 * x2) * r, (t2 * y1 - t1 * y2) * r,
                          (t2 * z1 - t1 * z2) * r};
            const Vec3 td{(s1 * x2 - s2 * x1) * r, (s1 * y2 - s2 * y1) * r,
                          (s1 * z2 - s2 * z1) * r};

            for (const uint32_t v : {i0, i1, i2}) {
                sAcc[v] += sd;
                tAcc[v] += td;
            }
        }
    }

    vtang_.assign(nVerts, Vec4{});
    for (size_t v = 0; v < nVerts; ++v) {
        const Vec3& nrm = vnorm_[v];
        Vec3 t          = sAcc[v] - nrm * dot(nrm, sAcc[v]);  // Gram-Schmidt
        float len       = std::sqrt(dot(t, t));

        if (!(len > 1e-8F)) {
            // No usable tangent here. Any unit vector orthogonal to the normal
            // is valid; pick one deterministically so output is reproducible.
            const Vec3 axis = (std::abs(nrm.x) < 0.9F) ? Vec3{1, 0, 0} : Vec3{0, 1, 0};
            t               = axis - nrm * dot(nrm, axis);
            len             = std::sqrt(dot(t, t));
            if (!(len > 1e-8F)) {
                t   = Vec3{1, 0, 0};
                len = 1.0F;
            }
        }
        t *= 1.0F / len;

        // Handedness uses the raw accumulated sAcc, per Lengyel.
        const float handedness = (dot(cross(nrm, sAcc[v]), tAcc[v]) < 0.0F) ? -1.0F : 1.0F;
        vtang_[v]              = Vec4{t.x, t.y, t.z, handedness};
    }
}

void Mesh::calcNormals() {
    calcFaceNormals();
    calcVertexNormals();
}

std::optional<std::pair<Vec3, Vec3>> Mesh::boundingBox() const {
    if (coord_.empty()) return std::nullopt;

    constexpr float inf = std::numeric_limits<float>::infinity();
    Vec3 lo{inf, inf, inf};
    Vec3 hi{-inf, -inf, -inf};
    for (const Vec3& v : coord_) {
        lo.x = std::min(lo.x, v.x);
        lo.y = std::min(lo.y, v.y);
        lo.z = std::min(lo.z, v.z);
        hi.x = std::max(hi.x, v.x);
        hi.y = std::max(hi.y, v.y);
        hi.z = std::max(hi.z, v.z);
    }
    return std::make_pair(lo, hi);
}

std::expected<std::vector<uint8_t>, MeshError> Mesh::faceMaskForVisibleVertices(
    std::span<const uint8_t> vertexVisible) const {
    if (vertexVisible.size() != coord_.size()) return std::unexpected(MeshError::MaskSizeMismatch);

    const size_t vpp    = vertsPerPrimitive_;
    const size_t nFaces = faceCount();
    std::vector<uint8_t> faceVisible(nFaces, 0U);

    for (size_t f = 0; f < nFaces; ++f) {
        const size_t base = f * vpp;
        for (size_t c = 0; c < vpp; ++c) {
            if (vertexVisible[fvert_[base + c]] != 0U) {
                faceVisible[f] = 1U;
                break;  // one visible corner is enough
            }
        }
    }
    return faceVisible;
}

float Mesh::heightCm() const {
    const auto bb = boundingBox();
    if (!bb) return 0.0F;
    return (bb->second.y - bb->first.y) * kDecimetresToCentimetres;
}

std::expected<Mesh, MeshError> Mesh::fromData(foundation::MeshData data) {
    Mesh m(std::move(data.name), data.vertsPerPrimitive);
    if (auto r = m.setCoords(std::move(data.coord)); !r) return std::unexpected(r.error());
    if (!data.texco.empty()) {
        if (auto r = m.setUVs(std::move(data.texco)); !r) return std::unexpected(r.error());
    }
    m.addFaceGroup("imported");
    if (auto r = m.setFaces(std::move(data.fvert), std::move(data.fuvs), {}); !r) {
        return std::unexpected(r.error());
    }
    return m;
}

}  // namespace mh::core
