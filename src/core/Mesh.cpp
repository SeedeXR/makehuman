// SPDX-License-Identifier: AGPL-3.0-or-later
#include "makehuman/core/Mesh.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

namespace mh::core {

Mesh::Mesh(std::string name, uint8_t vertsPerPrimitive)
    : name_(std::move(name)), vertsPerPrimitive_(vertsPerPrimitive),
      vertsPerFaceForExport_(vertsPerPrimitive) {}

void Mesh::setCoords(std::vector<Vec3> coords) {
    coord_     = std::move(coords);
    origCoord_ = coord_;                 // the morph base, module3d.py:532
    vnorm_.assign(coord_.size(), Vec3{});
}

void Mesh::setUVs(std::vector<Vec2> uvs) {
    texco_ = std::move(uvs);
    hasUV_ = !texco_.empty();
}

void Mesh::setFaces(std::vector<uint32_t> faceVerts,
                    std::vector<uint32_t> faceUVs,
                    std::vector<uint16_t> faceGroup) {
    fvert_ = std::move(faceVerts);
    fuvs_  = std::move(faceUVs);

    const size_t nFaces = faceCount();
    group_ = std::move(faceGroup);
    if (group_.size() != nFaces) {
        group_.assign(nFaces, 0);
    }
    fnorm_.assign(nFaces, Vec3{});
    hasUV_ = hasUV_ && !fuvs_.empty();

    // A triangle mesh is stored as quads with corner 0 repeated. The reference
    // decides this from the FIRST face only (module3d.py:634-639); a mixed
    // tri/quad mesh is therefore mis-tagged. We replicate the check but over
    // every face, so a mixed mesh stays tagged as quads rather than silently
    // exporting as triangles.
    vertsPerFaceForExport_ = vertsPerPrimitive_;
    if (vertsPerPrimitive_ == 4 && nFaces > 0) {
        bool allDegenerate = true;
        for (size_t f = 0; f < nFaces; ++f) {
            const size_t base = f * 4;
            if (fvert_[base] != fvert_[base + 3]) { allDegenerate = false; break; }
        }
        if (allDegenerate) {
            vertsPerFaceForExport_ = 3;
        }
    }
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
            const uint32_t v = fvert_[f * vpp + c];
            bool seenInThisFace = false;
            for (size_t p = 0; p < c; ++p) {
                if (fvert_[f * vpp + p] == v) { seenInThisFace = true; break; }
            }
            if (!seenInThisFace && v < nVerts) {
                ++counts[v];
            }
        }
    }

    const uint32_t maxCount = counts.empty() ? 0U : *std::ranges::max_element(counts);
    maxValence_ = static_cast<uint8_t>(std::max<uint32_t>(4U, maxCount));

    // Pass 2: fill. Flat array, stride = maxValence_.
    vface_.assign(nVerts * maxValence_, 0);
    nfaces_.assign(nVerts, 0);
    for (size_t f = 0; f < nFaces; ++f) {
        for (size_t c = 0; c < vpp; ++c) {
            const uint32_t v = fvert_[f * vpp + c];
            if (v >= nVerts) continue;
            bool seenInThisFace = false;
            for (size_t p = 0; p < c; ++p) {
                if (fvert_[f * vpp + p] == v) { seenInThisFace = true; break; }
            }
            if (seenInThisFace) continue;
            uint8_t& n = nfaces_[v];
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
    if (vface_.size() != nVerts * maxValence_) {
        buildAdjacency();
    }

    for (size_t v = 0; v < nVerts; ++v) {
        Vec3 sum{};
        const uint8_t n = nfaces_[v];
        for (uint8_t k = 0; k < n; ++k) {
            sum += fnorm_[vface_[v * maxValence_ + k]];
        }
        const float len = std::sqrt(dot(sum, sum));
        // The reference divides unconditionally (module3d.py:368) and produces
        // NaN for an isolated vertex. Guarding is a deliberate divergence.
        vnorm_[v] = (len > 0.0F) ? sum * (1.0F / len) : Vec3{0.0F, 1.0F, 0.0F};
    }
}

void Mesh::calcNormals() {
    calcFaceNormals();
    calcVertexNormals();
}

std::optional<std::pair<Vec3, Vec3>> Mesh::boundingBox() const {
    if (coord_.empty()) return std::nullopt;

    constexpr float inf = std::numeric_limits<float>::infinity();
    Vec3 lo{ inf,  inf,  inf};
    Vec3 hi{-inf, -inf, -inf};
    for (const Vec3& v : coord_) {
        lo.x = std::min(lo.x, v.x); lo.y = std::min(lo.y, v.y); lo.z = std::min(lo.z, v.z);
        hi.x = std::max(hi.x, v.x); hi.y = std::max(hi.y, v.y); hi.z = std::max(hi.z, v.z);
    }
    return std::make_pair(lo, hi);
}

float Mesh::heightCm() const {
    const auto bb = boundingBox();
    if (!bb) return 0.0F;
    return (bb->second.y - bb->first.y) * kDecimetresToCentimetres;
}

} // namespace mh::core
