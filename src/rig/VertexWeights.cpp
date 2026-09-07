// SPDX-License-Identifier: AGPL-3.0-or-later
#include "makehuman/rig/VertexWeights.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <fstream>
#include <numeric>

namespace mh::rig {
namespace {

using json = nlohmann::ordered_json;

}  // namespace

std::string WeightsError::message() const {
    const char* k = "unknown error";
    switch (kind) {
        case WeightsErrorKind::NotFound: k = "file not found"; break;
        case WeightsErrorKind::Unreadable: k = "file unreadable"; break;
        case WeightsErrorKind::Malformed: k = "malformed weights"; break;
        case WeightsErrorKind::VertexOutOfRange: k = "vertex index out of range"; break;
    }
    std::string m = file + ": " + k;
    if (!detail.empty()) m += " (" + detail + ")";
    return m;
}

uint8_t VertexWeights::maxInfluences() const {
    std::vector<uint16_t> counts(vertexCount, 0);
    for (const auto& [bone, bw] : perBone) {
        for (const uint32_t v : bw.verts) {
            if (v < counts.size()) ++counts[v];
        }
    }
    const auto it = std::ranges::max_element(counts);
    return it == counts.end() ? uint8_t{0} : static_cast<uint8_t>(std::min<uint16_t>(*it, 255));
}

CompiledWeights VertexWeights::compile(const Skeleton& skeleton, uint8_t influences) const {
    CompiledWeights out;
    out.influences = influences == 0 ? uint8_t{1} : influences;
    const size_t n = out.influences;

    out.boneIndex.assign(vertexCount * n, 0U);
    out.weight.assign(vertexCount * n, 0.0F);

    // Bone name -> index in the skeleton's canonical order. A weight naming a
    // bone the skeleton does not have is skipped, as the reference does
    // (it catches KeyError and warns).
    std::unordered_map<std::string, uint32_t> boneIndexOf;
    boneIndexOf.reserve(skeleton.bones.size());
    for (size_t i = 0; i < skeleton.bones.size(); ++i)
        boneIndexOf.emplace(skeleton.bones[i].name, static_cast<uint32_t>(i));

    // Gather per vertex.
    std::vector<std::vector<std::pair<float, uint32_t>>> perVertex(vertexCount);
    for (const auto& [boneName, bw] : perBone) {
        const auto bi = boneIndexOf.find(boneName);
        if (bi == boneIndexOf.end()) continue;

        for (size_t k = 0; k < bw.verts.size(); ++k) {
            const uint32_t v = bw.verts[k];
            if (v < vertexCount) perVertex[v].emplace_back(bw.weights[k], bi->second);
        }
    }

    for (size_t v = 0; v < vertexCount; ++v) {
        auto& infl = perVertex[v];
        if (infl.empty()) continue;

        // Descending by weight, ties broken by DESCENDING bone index. This is
        // Python's `sorted(tuples, reverse=True)`, and it decides which
        // influence survives truncation on symmetric vertices.
        std::ranges::sort(infl, std::ranges::greater{});

        out.maxInfluences =
            std::max(out.maxInfluences, static_cast<uint8_t>(std::min<size_t>(infl.size(), 255)));

        const bool truncated = infl.size() > n;
        if (truncated) {
            ++out.clampedVertices;
            infl.resize(n);
        }

        if (truncated) {
            // Re-normalise so the kept weights still sum to 1. Without this,
            // every heavily-weighted vertex loses mass and drifts toward the
            // origin when posed.
            const float sum =
                std::accumulate(infl.begin(), infl.end(), 0.0F,
                                [](float acc, const auto& e) { return acc + e.first; });
            if (sum > 0.0F) {
                for (auto& e : infl)
                    e.first /= sum;
            }
        }

        for (size_t i = 0; i < infl.size(); ++i) {
            out.boneIndex[v * n + i] = infl[i].second;
            out.weight[v * n + i]    = infl[i].first;
        }
    }
    return out;
}

std::expected<VertexWeights, WeightsError> loadWeights(const std::filesystem::path& path,
                                                       size_t vertexCount,
                                                       const std::string& rootBone) {
    std::error_code ec;
    if (!std::filesystem::exists(path, ec)) {
        return std::unexpected(WeightsError{WeightsErrorKind::NotFound, path.string(), {}});
    }
    std::ifstream in(path);
    if (!in) {
        return std::unexpected(WeightsError{WeightsErrorKind::Unreadable, path.string(), {}});
    }

    json root;
    try {
        root = json::parse(in);
    } catch (const json::parse_error& e) {
        return std::unexpected(WeightsError{WeightsErrorKind::Malformed, path.string(), e.what()});
    }
    if (!root.is_object() || !root.contains("weights") || !root["weights"].is_object()) {
        return std::unexpected(
            WeightsError{WeightsErrorKind::Malformed, path.string(), "no \"weights\" object"});
    }

    VertexWeights vw;
    vw.name        = root.value("name", std::string{});
    vw.description = root.value("description", std::string{});
    vw.version     = root.value("version", 0);
    vw.vertexCount = vertexCount;

    const json& weights = root["weights"];

    // Pass 1: total weight per vertex, across EVERY bone. The file's numbers
    // are relative, so nothing can be stored until this is known.
    std::vector<float> total(vertexCount, 0.0F);
    for (const auto& [boneName, list] : weights.items()) {
        if (!list.is_array()) continue;
        for (const auto& pair : list) {
            if (!pair.is_array() || pair.size() != 2) continue;
            const auto v = pair[0].get<int64_t>();
            if (v < 0 || static_cast<size_t>(v) >= vertexCount) {
                return std::unexpected(
                    WeightsError{WeightsErrorKind::VertexOutOfRange, path.string(),
                                 boneName + " references vertex " + std::to_string(v) + " of " +
                                     std::to_string(vertexCount)});
            }
            total[static_cast<size_t>(v)] += pair[1].get<float>();
        }
    }

    // Pass 2: normalise, merge doubles, sort by vertex, drop sub-threshold.
    for (const auto& [boneName, list] : weights.items()) {
        if (!list.is_array() || list.empty()) continue;

        std::unordered_map<uint32_t, float> merged;
        merged.reserve(list.size());
        for (const auto& pair : list) {
            if (!pair.is_array() || pair.size() != 2) continue;
            const auto v  = static_cast<uint32_t>(pair[0].get<int64_t>());
            const float w = pair[1].get<float>();
            const float t = total[v];
            merged[v] += (t > 0.0F) ? w / t : 0.0F;
        }

        BoneWeights bw;
        bw.verts.reserve(merged.size());
        for (const auto& [v, w] : merged) {
            if (w > kWeightThreshold) bw.verts.push_back(v);
        }
        std::ranges::sort(bw.verts);
        bw.weights.reserve(bw.verts.size());
        for (const uint32_t v : bw.verts)
            bw.weights.push_back(merged[v]);

        if (!bw.verts.empty()) vw.perBone.emplace(boneName, std::move(bw));
    }

    // A vertex with no weight at all binds to the root bone with weight 1.
    // Without this it collapses to the origin the moment the rig is posed.
    std::vector<uint32_t> unweighted;
    for (size_t v = 0; v < vertexCount; ++v) {
        if (total[v] == 0.0F) unweighted.push_back(static_cast<uint32_t>(v));
    }
    if (!unweighted.empty()) {
        BoneWeights& rw = vw.perBone[rootBone];
        for (const uint32_t v : unweighted) {
            rw.verts.push_back(v);
            rw.weights.push_back(1.0F);
        }
    }

    return vw;
}

CompiledWeights proxyWeights(const CompiledWeights& body,
                             std::span<const std::array<uint32_t, 3>> refs,
                             std::span<const std::array<float, 3>> fit) {
    CompiledWeights out;
    if (body.influences == 0 || refs.size() != fit.size()) return out;
    const size_t bodyVertices = body.vertexCount();
    if (bodyVertices == 0) return out;

    out.influences = body.influences;
    out.boneIndex.reserve(refs.size() * out.influences);
    out.weight.reserve(refs.size() * out.influences);

    // Reused across vertices: a proxy has tens of thousands and this is the
    // only allocation in the loop.
    std::vector<std::pair<uint32_t, float>> blended;

    for (size_t p = 0; p < refs.size(); ++p) {
        blended.clear();
        for (size_t r = 0; r < 3; ++r) {
            const uint32_t base = refs[p][r];
            const float share   = fit[p][r];
            if (base >= bodyVertices) return {};  // out of range: refuse the lot
            if (share == 0.0F) continue;
            for (uint8_t i = 0; i < body.influences; ++i) {
                const size_t at = (static_cast<size_t>(base) * body.influences) + i;
                const float w   = body.weight[at] * share;
                if (w == 0.0F) continue;
                const uint32_t bone = body.boneIndex[at];
                // The three ref vertices routinely share bones, so accumulate
                // rather than append: keeping duplicates would spend the four
                // slots on one bone listed three times.
                const auto found = std::ranges::find_if(
                    blended, [bone](const auto& e) { return e.first == bone; });
                if (found == blended.end()) {
                    blended.emplace_back(bone, w);
                } else {
                    found->second += w;
                }
            }
        }

        if (blended.size() > out.influences) {
            ++out.clampedVertices;
        }
        out.maxInfluences = std::max(out.maxInfluences,
                                     static_cast<uint8_t>(std::min<size_t>(blended.size(), 255)));

        // Strongest first, so truncating keeps the influences that matter.
        std::ranges::sort(blended,
                          [](const auto& a, const auto& b) { return a.second > b.second; });
        blended.resize(std::min<size_t>(blended.size(), out.influences));

        // Renormalise AFTER truncating. Dropping influences without it shrinks
        // the vertex toward the origin by whatever weight was discarded.
        float total = 0.0F;
        for (const auto& [bone, w] : blended)
            total += w;
        for (uint8_t i = 0; i < out.influences; ++i) {
            const bool present = i < blended.size();
            out.boneIndex.push_back(present ? blended[i].first : 0U);
            out.weight.push_back(present && total > 0.0F ? blended[i].second / total : 0.0F);
        }
    }
    return out;
}

}  // namespace mh::rig
