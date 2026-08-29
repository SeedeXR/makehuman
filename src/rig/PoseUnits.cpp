// SPDX-License-Identifier: AGPL-3.0-or-later
#include "makehuman/rig/PoseUnits.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <fstream>
#include <unordered_map>

namespace mh::rig {
namespace {

using foundation::Quat;

/// The reference's REST_QUAT: identity, scalar first.
constexpr Quat kRest{1.0, 0.0, 0.0, 0.0};

}  // namespace

std::string PoseUnitsError::message() const {
    const char* k = "unknown error";
    switch (kind) {
        case PoseUnitsErrorKind::NotFound: k = "file not found"; break;
        case PoseUnitsErrorKind::Unreadable: k = "file unreadable"; break;
        case PoseUnitsErrorKind::Malformed: k = "malformed pose units"; break;
        case PoseUnitsErrorKind::FrameCountMismatch:
            k = "name count does not match the frame count";
            break;
    }
    std::string m = file + ": " + k;
    if (!detail.empty()) m += " (" + detail + ")";
    return m;
}

std::optional<size_t> PoseUnits::indexOf(std::string_view name) const {
    const auto it = std::ranges::find(names, name);
    if (it == names.end()) return std::nullopt;
    return static_cast<size_t>(std::distance(names.begin(), it));
}

std::vector<Mat4> PoseUnits::blend(std::span<const size_t> unitIndices,
                                   std::span<const float> weights) const {
    if (unitIndices.size() != weights.size() || unitIndices.empty()) return {};
    for (const size_t i : unitIndices) {
        if (i >= unitCount()) return {};
    }

    std::vector<Mat4> out(boneCount, Mat4::identity());

    for (size_t b = 0; b < boneCount; ++b) {
        // Each unit contributes a rotation scaled by its weight: slerping from
        // identity toward the unit's own rotation by `w` is how a fractional
        // amount of a pose is expressed.
        Quat acc = foundation::quaternionSlerp(
            kRest, foundation::quaternionFromMatrix(unit(unitIndices[0])[b]),
            static_cast<double>(weights[0]));

        for (size_t k = 1; k < unitIndices.size(); ++k) {
            const Quat q = foundation::quaternionSlerp(
                kRest, foundation::quaternionFromMatrix(unit(unitIndices[k])[b]),
                static_cast<double>(weights[k]));
            // Left-multiplied, so later units compose ON TOP of earlier ones.
            // This is what makes the blend order-dependent.
            acc = foundation::quaternionMultiply(q, acc);
        }

        out[b] = foundation::quaternionMatrix(acc);
    }
    return out;
}

std::expected<std::vector<std::string>, PoseUnitsError> loadPoseUnitNames(
    const std::filesystem::path& path) {
    std::error_code ec;
    if (!std::filesystem::exists(path, ec)) {
        return std::unexpected(PoseUnitsError{PoseUnitsErrorKind::NotFound, path.string(), {}});
    }
    std::ifstream in(path);
    if (!in) {
        return std::unexpected(PoseUnitsError{PoseUnitsErrorKind::Unreadable, path.string(), {}});
    }

    nlohmann::json root;
    try {
        root = nlohmann::json::parse(in);
    } catch (const nlohmann::json::parse_error& e) {
        return std::unexpected(
            PoseUnitsError{PoseUnitsErrorKind::Malformed, path.string(), e.what()});
    }
    if (!root.is_object() || !root.contains("framemapping") || !root["framemapping"].is_array()) {
        return std::unexpected(PoseUnitsError{PoseUnitsErrorKind::Malformed, path.string(),
                                              "no \"framemapping\" array"});
    }

    std::vector<std::string> names;
    names.reserve(root["framemapping"].size());
    for (const auto& n : root["framemapping"]) {
        if (!n.is_string()) {
            return std::unexpected(PoseUnitsError{PoseUnitsErrorKind::Malformed, path.string(),
                                                  "framemapping holds a non-string"});
        }
        names.push_back(n.get<std::string>());
    }
    return names;
}

std::expected<PoseUnits, PoseUnitsError> makePoseUnits(const io::BvhFile& bvh,
                                                       const Skeleton& skeleton,
                                                       std::vector<std::string> names) {
    if (names.size() != bvh.frameCount) {
        return std::unexpected(PoseUnitsError{PoseUnitsErrorKind::FrameCountMismatch,
                                              {},
                                              std::to_string(names.size()) + " names for " +
                                                  std::to_string(bvh.frameCount) + " frames"});
    }

    std::unordered_map<std::string, size_t> bvhByName;
    bvhByName.reserve(bvh.joints.size());
    for (size_t i = 0; i < bvh.joints.size(); ++i) {
        if (!bvh.joints[i].endSite) bvhByName.emplace(bvh.joints[i].name, i);
    }

    PoseUnits out;
    out.names     = std::move(names);
    out.boneCount = skeleton.bones.size();
    out.data.assign(out.unitCount() * out.boneCount, Mat4::identity());

    for (size_t f = 0; f < bvh.frameCount; ++f) {
        for (size_t b = 0; b < out.boneCount; ++b) {
            const auto it = bvhByName.find(skeleton.bones[b].name);
            if (it == bvhByName.end()) continue;  // identity: the BVH has no such joint
            out.data[f * out.boneCount + b] = bvh.joints[it->second].frames[f];
        }
    }
    return out;
}

}  // namespace mh::rig
