// SPDX-License-Identifier: AGPL-3.0-or-later
#include "makehuman/rig/PoseUnits.h"
#include "makehuman/foundation/FileRead.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <fstream>
#include <optional>
#include <unordered_map>
#include <unordered_set>
#include <vector>

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

std::expected<void, PoseUnitsError> saveExpression(const std::filesystem::path& path,
                                                   const Expression& expression) {
    const auto malformed = [&path](std::string why) {
        return std::unexpected(
            PoseUnitsError{PoseUnitsErrorKind::Malformed, path.string(), std::move(why)});
    };
    if (expression.name.empty()) return malformed("an expression needs a \"name\"");

    // ordered_json, for the reason loadExpression gives at length: the default
    // sorts an object's members and the blend does not commute.
    nlohmann::ordered_json units = nlohmann::ordered_json::object();
    for (const auto& u : expression.units) {
        // The reference's own filter (7_expression_mixer.py:223). A unit at
        // rest is not part of the expression.
        if (u.weight == 0.0F) continue;
        // `unit_poses` is a JSON object, so it cannot say "this unit, twice" --
        // and `blend` composes quaternions, so two applications are not one
        // bigger weight. Assigning would keep the last and write a file that
        // loads cleanly into a different face. Reachable from the CLI today as
        // `--facs AU12=0.3 --facs AU12=0.7`.
        if (units.contains(u.name)) {
            return malformed("\"" + u.name +
                             "\" appears more than once; a .mhpose names each unit at most once");
        }
        units[u.name] = u.weight;
    }
    if (units.empty()) return malformed("\"unit_poses\" would be empty");

    nlohmann::ordered_json root = nlohmann::ordered_json::object();
    root["name"]                = expression.name;
    root["description"]         = expression.description;
    root["tags"]                = expression.tags;
    root["unit_poses"]          = std::move(units);

    // Checked AFTER the refusals above, so a bad expression never creates a
    // file even when the path is perfectly writable.
    std::ofstream out(path);
    if (!out) {
        return std::unexpected(PoseUnitsError{PoseUnitsErrorKind::Unreadable, path.string(),
                                              "cannot open for writing"});
    }
    out << root.dump(4) << '\n';
    if (!out) {
        return std::unexpected(
            PoseUnitsError{PoseUnitsErrorKind::Unreadable, path.string(), "write failed"});
    }
    return {};
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

namespace {

/// Reads @p path and maps its frames onto @p skeleton, or an error.
///
/// Shared so that the pose loader and the frame loader cannot drift: they
/// differ only in which frames they will accept and which one they return.
std::expected<PoseUnits, PoseUnitsError> poseUnitsOf(const std::filesystem::path& path,
                                                     const Skeleton& skeleton,
                                                     const io::BvhFile& bvh) {
    std::vector<std::string> names;
    names.reserve(bvh.frameCount);
    for (size_t f = 0; f < bvh.frameCount; ++f) {
        names.push_back(path.stem().string() + "#" + std::to_string(f));
    }
    return makePoseUnits(bvh, skeleton, std::move(names));
}

/// The least rotation taking unit @p from to unit @p to.
///
/// Two directions do not determine a rotation: any amount of twist about the
/// shared axis also maps one to the other. This picks the one that adds none,
/// which is the only defensible choice when the source gives no roll -- and a
/// BVH does not, its joints having no rest orientation at all. Where the two
/// skeletons disagree about a bone's roll, that disagreement survives; it
/// shows as a twist about the bone rather than as the limb pointing the wrong
/// way, which is the smaller of the two wrongs.
///
/// Rodrigues, written out rather than routed through a quaternion: the
/// antiparallel case needs handling either way and this keeps it visible.
foundation::Mat4 minimalRotation(const foundation::Vec3& from, const foundation::Vec3& to) {
    const foundation::Vec3 v = foundation::cross(from, to);
    const float s            = std::sqrt(foundation::dot(v, v));
    const float c            = foundation::dot(from, to);
    foundation::Mat4 out     = foundation::Mat4::identity();
    if (s < 1e-6F) {
        // Parallel is identity. ANTIparallel has no minimal rotation -- every
        // half-turn about an axis perpendicular to the bone does it -- and it
        // means the two skeletons point this bone in opposite directions,
        // which is a mapping error rather than a rest difference. Identity
        // leaves it visibly wrong instead of inventing an axis.
        return out;
    }
    const float k = (1.0F - c) / (s * s);
    const float x = v.x;
    const float y = v.y;
    const float z = v.z;
    // I + [v]x + [v]x^2 * k
    out.m[0][0] = 1.0F + k * (-z * z - y * y);
    out.m[0][1] = -z + k * (y * x);
    out.m[0][2] = y + k * (z * x);
    out.m[1][0] = z + k * (x * y);
    out.m[1][1] = 1.0F + k * (-z * z - x * x);
    out.m[1][2] = -x + k * (z * y);
    out.m[2][0] = -y + k * (x * z);
    out.m[2][1] = x + k * (y * z);
    out.m[2][2] = 1.0F + k * (-y * y - x * x);
    return out;
}

/// The one joint @p joint drives, or none when that is not a single joint.
///
/// A BRANCHING joint drives no single segment: `Hips` carries the spine and
/// both legs, and which comes first is file order, not anatomy. Taking one put
/// our `hips` 150.7 degrees from the source's and tore the pelvis open --
/// MEASURED as the worst edge in the mesh, the worst stretch going 3.8x to
/// 7.4x. End sites are not candidates: they give a leaf a direction but there
/// is no bone of ours to match them against.
std::optional<size_t> soleChild(const io::BvhFile& bvh, size_t joint) {
    std::optional<size_t> only;
    for (size_t i = joint + 1; i < bvh.joints.size(); ++i) {
        if (bvh.joints[i].parent != static_cast<int32_t>(joint) || bvh.joints[i].endSite) continue;
        if (only) return std::nullopt;
        only = i;
    }
    return only;
}

}  // namespace

std::vector<foundation::Mat4> restAlignment(const io::BvhFile& bvh, const Skeleton& skeleton) {
    std::unordered_map<std::string_view, size_t> byName;
    byName.reserve(bvh.joints.size());
    for (size_t i = 0; i < bvh.joints.size(); ++i) {
        if (!bvh.joints[i].endSite) byName.emplace(bvh.joints[i].name, i);
    }

    std::vector<foundation::Mat4> out(skeleton.bones.size(), foundation::Mat4::identity());
    for (size_t b = 0; b < skeleton.bones.size(); ++b) {
        const Bone& bone = skeleton.bones[b];
        // Parents precede children (loadSkeleton guarantees it), so the
        // parent's rotation is already final here.
        const foundation::Mat4 inherited =
            bone.parent < 0 ? foundation::Mat4::identity() : out[static_cast<size_t>(bone.parent)];

        // The ROOT is never aligned. It carries the character's placement in
        // the world, not an anatomical direction: this rig's `root` runs to
        // `spine05` while MakeHuman 1.x's runs to `Hips`, and MEASURED they
        // sit 48.2 degrees apart. Turning the root by that swings the whole
        // body against its own skin -- rendered, it bulges the waist and
        // collapses the chest while the spine below dutifully turns back.
        // Leaving it identity costs nothing elsewhere: every other bone's
        // rotation is computed against its own rest, and the
        // `inv(R_parent)` term already absorbs whatever the parent did.
        const auto joint = byName.find(bone.name);
        if (bone.parent < 0 || joint == byName.end()) {
            out[b] = inherited;
            continue;
        }
        // Compare the SAME anatomy in both skeletons, head to head of the
        // next driven bone -- not "this bone's own direction", which spans a
        // different amount of body in each. This rig splits the neck into
        // three and interposes `upperarm02` in the humerus where a MakeHuman
        // 1.x file has one bone for each, so a bone-for-bone comparison
        // measures a third of our neck against the whole of theirs. MEASURED,
        // that put `neck01` 22.2 degrees out and tipped the head back to stare
        // at the sky -- while the file itself keeps the head 5.3 degrees from
        // vertical, read from the reference's own parser.
        const auto child = soleChild(bvh, joint->second);
        if (!child) {
            out[b] = inherited;
            continue;
        }
        const auto tip = std::ranges::find_if(
            skeleton.bones, [&](const Bone& x) { return x.name == bvh.joints[*child].name; });
        if (tip == skeleton.bones.end()) {
            out[b] = inherited;
            continue;
        }
        const foundation::Vec3 ours = tip->head - bone.head;
        const foundation::Vec3 theirs =
            bvh.joints[*child].position - bvh.joints[joint->second].position;
        const float lo = std::sqrt(foundation::dot(ours, ours));
        const float lt = std::sqrt(foundation::dot(theirs, theirs));
        if (lo < 1e-6F || lt < 1e-6F) {
            out[b] = inherited;
            continue;
        }
        const foundation::Vec3 u{ours.x / lo, ours.y / lo, ours.z / lo};
        const foundation::Vec3 source{theirs.x / lt, theirs.y / lt, theirs.z / lt};
        out[b] = minimalRotation(u, source);
    }
    return out;
}

namespace {

/// Rewrites @p pose so the source's rotations act on the source's rest.
///
/// Derivation, because the `inv(parent)` is not guessable. Skinning composes
/// `global_b = global_parent * matRestRelative_b * matPose_b`, and what is
/// wanted is `global_b = A_b * R_b * T_b` -- the file's accumulated rotation
/// `A_b` acting on our rest `T_b` once `R_b` has turned it to the source's.
/// Substituting `matRestRelative_b = inv(T_parent) * T_b` and expanding by
/// induction gives `matPose_b = inv(T_b) * inv(R_parent) * L_b * R_b * T_b`.
/// `poseToBoneLocal` already supplies the outer `inv(T_b) * ... * T_b`, so
/// what belongs here is exactly the middle: `inv(R_parent) * L_b * R_b`.
///
/// VERIFIED by the induction and by a numeric prototype against the reference
/// parser: every arm bone of walk1 frame 0 lands 0.0 degrees from where the
/// file's author put it, against 42.3 to 78.4 degrees before.
/// An undriven bone falls out as identity, which is the correct "keep your
/// rest, relative to your corrected parent".
void alignToSourceRest(std::vector<foundation::Mat4>& pose, const io::BvhFile& bvh,
                       const Skeleton& skeleton) {
    if (pose.size() != skeleton.bones.size()) return;
    const std::vector<foundation::Mat4> r = restAlignment(bvh, skeleton);
    for (size_t b = 0; b < pose.size(); ++b) {
        const int32_t parent = skeleton.bones[b].parent;
        const foundation::Mat4 inverseParent =
            parent < 0 ? foundation::Mat4::identity()
                       : foundation::rigidInverse(r[static_cast<size_t>(parent)]);
        pose[b] = inverseParent * pose[b] * r[b];
    }
}

/// The BVH at @p path, with the two error sets lined up.
std::expected<io::BvhFile, PoseUnitsError> readBvhFor(const std::filesystem::path& path) {
    auto bvh = io::readBvh(path);
    if (bvh) return *bvh;
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

/// One frame of an ALREADY-READ BVH.
///
/// Takes the parsed file rather than a path so that `loadBodyPose`, which has
/// to look at `frameCount` before it decides, does not read and parse the same
/// file twice. It did in the first draft of this, on every posed export.
std::expected<std::vector<Mat4>, PoseUnitsError> frameOf(const std::filesystem::path& path,
                                                         const Skeleton& skeleton,
                                                         const io::BvhFile& bvh, size_t frame) {
    if (frame >= bvh.frameCount) {
        return std::unexpected(PoseUnitsError{
            PoseUnitsErrorKind::FrameCountMismatch, path.string(),
            "no frame " + std::to_string(frame) + "; the file has " +
                std::to_string(bvh.frameCount) + (bvh.frameCount == 1 ? " frame" : " frames")});
    }

    // makePoseUnits already does the hard part: walk the rig's bones and take
    // each one's identically-named BVH joint, identity where there is none.
    //
    // It builds every frame to hand back one. That is the whole file's worth of
    // transforms for the shipped 60-frame library and costs nothing worth
    // naming; a thousand-frame mocap capture would want a version that maps a
    // single frame, and this is where it would go.
    auto units = poseUnitsOf(path, skeleton, bvh);
    if (!units) return std::unexpected(units.error());

    const std::span<const Mat4> f = units->unit(frame);
    return std::vector<Mat4>(f.begin(), f.end());
}

}  // namespace

std::expected<size_t, PoseUnitsError> bonesDrivenBy(const std::filesystem::path& path,
                                                    const Skeleton& skeleton,
                                                    const RetargetMap* names) {
    auto bvh = readBvhFor(path);
    if (!bvh) return std::unexpected(bvh.error());
    // Renamed before the count for the same reason the pose loaders rename
    // before posing: a count that disagreed with the posing would be believed.
    if (names) retargetJoints(*bvh, *names);

    // The same rule `makePoseUnits` poses by: a bone is driven when the file
    // holds a non-end-site joint of IDENTICALLY the same name. Counted the same
    // way on purpose -- a count that disagreed with the posing would be worse
    // than no count, because it would be believed.
    //
    // The end-site condition is kept for that parity and NOT because a test
    // can kill it: `BvhReader` names an end site `<parent>_end` expressly so it
    // "cannot collide with a real joint", and neither shipped rig has a bone
    // ending in `_end` (0 of 163, 0 of 179). Removing it changes no count this
    // repository can produce -- a mutation that survives, recorded rather than
    // dressed up as covered.
    std::unordered_set<std::string_view> joints;
    joints.reserve(bvh->joints.size());
    for (const auto& joint : bvh->joints) {
        if (!joint.endSite) joints.insert(joint.name);
    }
    return static_cast<size_t>(
        std::count_if(skeleton.bones.begin(), skeleton.bones.end(),
                      [&joints](const auto& bone) { return joints.contains(bone.name); }));
}

std::expected<std::vector<Mat4>, PoseUnitsError> loadBodyPoseFrame(
    const std::filesystem::path& path, const Skeleton& skeleton, size_t frame,
    const RetargetMap* names) {
    auto bvh = readBvhFor(path);
    if (!bvh) return std::unexpected(bvh.error());
    if (names) retargetJoints(*bvh, *names);
    auto pose = frameOf(path, skeleton, *bvh, frame);
    // Only when a table was used. A file that already names this rig's bones
    // was authored against this rig's rest -- `data/poses/tpose.bvh` carries
    // this rig's own offsets -- so there is nothing to align and the
    // correction would be identity anyway. Gating on the table says so
    // outright rather than relying on that.
    if (names && pose) alignToSourceRest(*pose, *bvh, skeleton);
    return pose;
}

std::expected<std::vector<Mat4>, PoseUnitsError> loadBodyPose(const std::filesystem::path& path,
                                                              const Skeleton& skeleton,
                                                              const RetargetMap* names) {
    auto bvh = readBvhFor(path);
    if (!bvh) return std::unexpected(bvh.error());
    if (names) retargetJoints(*bvh, *names);

    // Kept exactly as it was. A caller asking for a POSE has not told us they
    // know this is an animation, so frame 0 of a walk cycle stays an error
    // rather than a plausible wrong pose. `loadBodyPoseFrame` is how a caller
    // says otherwise.
    if (bvh->frameCount != 1) {
        return std::unexpected(PoseUnitsError{
            PoseUnitsErrorKind::FrameCountMismatch, path.string(),
            "a body pose must hold exactly one frame; this file has " +
                std::to_string(bvh->frameCount) + " and is an animation, not a pose"});
    }
    auto pose = frameOf(path, skeleton, *bvh, 0);
    // Same rule as loadBodyPoseFrame; see the note there.
    if (names && pose) alignToSourceRest(*pose, *bvh, skeleton);
    return pose;
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

std::expected<PoseUnits, PoseUnitsError> loadBodyPoseUnits(const std::filesystem::path& path,
                                                           const Skeleton& skeleton,
                                                           const RetargetMap* names) {
    auto opened = foundation::openForRead(path);
    if (!opened) {
        const auto kind = opened.error() == foundation::FileReadErrorKind::NotFound
                              ? PoseUnitsErrorKind::NotFound
                              : PoseUnitsErrorKind::Unreadable;
        return std::unexpected(PoseUnitsError{kind, path.string(), "cannot open"});
    }

    nlohmann::ordered_json doc;
    try {
        *opened >> doc;
    } catch (const nlohmann::json::exception& e) {
        return std::unexpected(
            PoseUnitsError{PoseUnitsErrorKind::Malformed, path.string(), e.what()});
    }
    const auto poses = doc.find("poses");
    if (poses == doc.end() || !poses->is_object()) {
        return std::unexpected(
            PoseUnitsError{PoseUnitsErrorKind::Malformed, path.string(), "no `poses` object"});
    }

    const auto malformed = [&path](std::string what) {
        return std::unexpected(
            PoseUnitsError{PoseUnitsErrorKind::Malformed, path.string(), std::move(what)});
    };

    // Bone name -> index, built once. The asset names its bones per pose, so
    // without this the lookup would be a linear scan of 179 bones per entry.
    std::unordered_map<std::string_view, size_t> boneIndex;
    boneIndex.reserve(skeleton.bones.size());
    for (size_t b = 0; b < skeleton.bones.size(); ++b)
        boneIndex.emplace(skeleton.bones[b].name, b);

    PoseUnits out;
    out.boneCount = skeleton.bones.size();
    out.names.reserve(poses->size());
    out.data.assign(poses->size() * out.boneCount, Mat4::identity());

    size_t unit = 0;
    for (const auto& [poseName, bones] : poses->items()) {
        out.names.push_back(poseName);
        if (!bones.is_object()) {
            return malformed("pose `" + poseName + "` is not an object");
        }
        for (const auto& [rawBone, quat] : bones.items()) {
            // Rejected rather than skipped, the way the sibling table loader
            // rejects a wrong-typed value (`src/rig/RetargetMap.cpp:52-55`). A
            // skipped bone loads a pose that moves LESS than it says while
            // reporting success -- the failure nobody notices for a release.
            if (!quat.is_array() || quat.size() != 4) {
                return malformed("pose `" + poseName + "` bone `" + rawBone +
                                 "`: expected 4 quaternion components, got " +
                                 std::to_string(quat.size()));
            }
            // `is_array() && size() == 4` is NOT enough: four strings pass it
            // and then throw `json::type_error` out of `get<double>()`, past
            // the `std::expected` every caller is written against. MEASURED.
            if (!std::all_of(quat.begin(), quat.end(),
                             [](const auto& c) { return c.is_number(); })) {
                return malformed("pose `" + poseName + "` bone `" + rawBone +
                                 "`: quaternion is not numeric");
            }

            // The table renames the asset's bone to this rig's, exactly as
            // `retargetJoints` renames a BVH's joints. A bone it does not cover
            // keeps its own name and simply fails to match below -- which is
            // the whole reason the table exists: MEASURED, name matching alone
            // leaves 8 of the 61 poses driving nothing at all.
            std::string bone = rawBone;
            if (names != nullptr) {
                if (const auto it = names->toBone.find(bone); it != names->toBone.end()) {
                    bone = it->second;
                }
            }
            const auto at = boneIndex.find(bone);
            if (at == boneIndex.end()) continue;  // this rig has no such bone

            // `[w, x, y, z]`, which is this project's quaternion order --
            // Eigen's `.coeffs()` is `[x, y, z, w]` and mixing them silently
            // produces a plausible wrong rotation.
            const foundation::Quat q{quat[0].get<double>(), quat[1].get<double>(),
                                     quat[2].get<double>(), quat[3].get<double>()};
            out.data[unit * out.boneCount + at->second] = foundation::quaternionMatrix(q);
        }
        ++unit;
    }
    return out;
}

}  // namespace mh::rig
