// SPDX-License-Identifier: AGPL-3.0-or-later
#include "makehuman/rig/PoseUnits.h"
#include "makehuman/foundation/FileRead.h"

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

    // One unit's contribution as a rigid transform: its rotation slerped from
    // identity by the weight, and its translation scaled by the same weight.
    //
    // Linear in the weight for the translation, because "half of this
    // expression" has to mean half the slide; slerp is the rotational
    // equivalent of the same idea.
    const auto contribution = [this](size_t unitIndex, size_t bone, float weight) {
        const Mat4& src = unit(unitIndex)[bone];
        Mat4 m          = foundation::quaternionMatrix(foundation::quaternionSlerp(
            kRest, foundation::quaternionFromMatrix(src), static_cast<double>(weight)));
        m.m[0][3]       = src.m[0][3] * weight;
        m.m[1][3]       = src.m[1][3] * weight;
        m.m[2][3]       = src.m[2][3] * weight;
        return m;
    };

    for (size_t b = 0; b < boneCount; ++b) {
        // Each unit contributes a rotation scaled by its weight: slerping from
        // identity toward the unit's own rotation by `w` is how a fractional
        // amount of a pose is expressed. The TRANSLATION rides along in the
        // same matrix -- until 2026-09-08 this went through a quaternion and
        // back, which discarded it silently, so a jaw slide or a lip purse was
        // inexpressible however it was authored (M9).
        Mat4 acc = contribution(unitIndices[0], b, weights[0]);

        for (size_t k = 1; k < unitIndices.size(); ++k) {
            // Left-multiplied, so later units compose ON TOP of earlier ones.
            // This is what makes the blend order-dependent. Multiplying the
            // full 4x4 is what carries an earlier unit's translation through a
            // later unit's rotation, which adding the two translations
            // separately would get wrong in a plausible-looking way.
            acc = contribution(unitIndices[k], b, weights[k]) * acc;
        }

        out[b] = acc;
    }
    return out;
}

std::expected<Expression, PoseUnitsError> loadExpression(const std::filesystem::path& path) {
    // openForRead rather than exists()+ifstream, for the reason the names
    // loader below gives: a DIRECTORY satisfies both and then parses as empty.
    auto opened = foundation::openForRead(path);
    if (!opened) {
        const auto kind = opened.error() == foundation::FileReadErrorKind::NotFound
                              ? PoseUnitsErrorKind::NotFound
                              : PoseUnitsErrorKind::Unreadable;
        return std::unexpected(PoseUnitsError{kind, path.string(), {}});
    }

    // ordered_json, NOT json: the default keeps object members in a std::map and
    // hands them back SORTED. The blend multiplies quaternions and does not
    // commute, and the reference feeds it `list(unit_poses.keys())` in file
    // order -- so a sorted reader produces a different, plausible, wrong
    // expression. Measured on the fixture: 0.0245, against a tolerance of 1e-4.
    // The first version of this used `json` and the parity test caught it.
    nlohmann::ordered_json root;
    try {
        root = nlohmann::ordered_json::parse(*opened);
    } catch (const nlohmann::ordered_json::parse_error& e) {
        return std::unexpected(
            PoseUnitsError{PoseUnitsErrorKind::Malformed, path.string(), e.what()});
    }
    const auto malformed = [&path](const char* why) {
        return std::unexpected(PoseUnitsError{PoseUnitsErrorKind::Malformed, path.string(), why});
    };
    if (!root.is_object()) return malformed("not a JSON object");

    // The reference reads `mhupb['name']` unguarded, so a file without one
    // raises there; it is an error here for the same reason.
    if (!root.contains("name") || !root["name"].is_string()) return malformed("no \"name\"");
    if (!root.contains("unit_poses") || !root["unit_poses"].is_object()) {
        return malformed("no \"unit_poses\" object");
    }

    Expression expr;
    expr.name = root["name"].get<std::string>();
    if (root.contains("description") && root["description"].is_string()) {
        expr.description = root["description"].get<std::string>();
    }
    if (root.contains("tags") && root["tags"].is_array()) {
        for (const auto& t : root["tags"]) {
            if (t.is_string()) expr.tags.push_back(t.get<std::string>());
        }
    }

    // In the FILE's order. nlohmann::json preserves insertion order for objects
    // parsed from text, which is what the reference relies on when it hands
    // `list(unit_poses.keys())` to a blend that does not commute.
    for (const auto& [unitName, weight] : root["unit_poses"].items()) {
        if (!weight.is_number()) return malformed("a unit weight is not a number");
        expr.units.push_back({unitName, weight.get<float>()});
    }
    if (expr.units.empty()) return malformed("\"unit_poses\" is empty");
    return expr;
}

std::expected<std::vector<std::string>, PoseUnitsError> loadPoseUnitNames(
    const std::filesystem::path& path) {
    // openForRead, not exists()+ifstream: a DIRECTORY satisfies both and
    // then parses as an empty file, so this reader used to accept one.
    // See foundation/FileRead.h for what each reader did before.
    auto opened = foundation::openForRead(path);
    if (!opened) {
        // NotAFile maps to Unreadable rather than NotFound: something IS
        // there, and saying "not found" about a path that exists sends
        // whoever is debugging it looking in the wrong place.
        const auto kind = opened.error() == foundation::FileReadErrorKind::NotFound
                              ? PoseUnitsErrorKind::NotFound
                              : PoseUnitsErrorKind::Unreadable;
        return std::unexpected(PoseUnitsError{kind, path.string(), {}});
    }
    std::ifstream& in = *opened;

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

std::expected<std::vector<Mat4>, PoseUnitsError> loadBodyPose(const std::filesystem::path& path,
                                                              const Skeleton& skeleton) {
    auto bvh = io::readBvh(path);
    if (!bvh) {
        // The two error sets line up one for one except for BVH's frame-data
        // kind, which is a malformed file by any other name.
        PoseUnitsErrorKind kind = PoseUnitsErrorKind::Malformed;
        switch (bvh.error().kind) {
            case io::BvhErrorKind::NotFound: kind = PoseUnitsErrorKind::NotFound; break;
            case io::BvhErrorKind::Unreadable: kind = PoseUnitsErrorKind::Unreadable; break;
            case io::BvhErrorKind::Malformed:
            case io::BvhErrorKind::FrameDataMismatch: break;
        }
        return std::unexpected(PoseUnitsError{kind, path.string(), bvh.error().message()});
    }
    if (bvh->frameCount != 1) {
        return std::unexpected(PoseUnitsError{
            PoseUnitsErrorKind::FrameCountMismatch, path.string(),
            "a body pose must hold exactly one frame; this file has " +
                std::to_string(bvh->frameCount) + " and is an animation, not a pose"});
    }

    // makePoseUnits already does the hard part: walk the rig's bones and take
    // each one's identically-named BVH joint, identity where there is none.
    auto units = makePoseUnits(*bvh, skeleton, {std::string(path.stem().string())});
    if (!units) return std::unexpected(units.error());

    const std::span<const Mat4> frame = units->unit(0);
    return std::vector<Mat4>(frame.begin(), frame.end());
}

std::expected<std::vector<Mat4>, PoseUnitsError> mixPoses(std::span<const Mat4> base,
                                                          std::span<const Mat4> overlay,
                                                          std::span<const size_t> bones) {
    if (base.size() != overlay.size()) {
        return std::unexpected(
            PoseUnitsError{PoseUnitsErrorKind::FrameCountMismatch,
                           {},
                           "poses are for different skeletons: " + std::to_string(base.size()) +
                               " bones vs " + std::to_string(overlay.size())});
    }
    for (const size_t b : bones) {
        if (b >= base.size()) {
            return std::unexpected(PoseUnitsError{PoseUnitsErrorKind::Malformed,
                                                  {},
                                                  "bone index " + std::to_string(b) +
                                                      " is past the end of a " +
                                                      std::to_string(base.size()) + "-bone pose"});
        }
    }

    std::vector<Mat4> out(base.begin(), base.end());
    for (const size_t b : bones)
        out[b] = overlay[b];
    return out;
}

}  // namespace mh::rig
