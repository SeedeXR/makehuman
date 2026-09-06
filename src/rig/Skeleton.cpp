// SPDX-License-Identifier: AGPL-3.0-or-later
#include "makehuman/rig/Skeleton.h"
#include "makehuman/foundation/FileRead.h"

#include <nlohmann/json.hpp>

#include <cmath>
#include <deque>
#include <fstream>

namespace mh::rig {
namespace {

using json = nlohmann::ordered_json;

/// `rotation_plane` is either a plane name or a number (0 meaning "none").
/// skeleton.py:127-131 also guards the hand-edited `[null,null,null]` form.
std::string planeNameOf(const json& v) {
    if (v.is_string()) return v.get<std::string>();
    return {};
}

}  // namespace

std::string SkeletonError::message() const {
    const char* k = "unknown error";
    switch (kind) {
        case SkeletonErrorKind::NotFound: k = "file not found"; break;
        case SkeletonErrorKind::Unreadable: k = "file unreadable"; break;
        case SkeletonErrorKind::Malformed: k = "malformed skeleton"; break;
        case SkeletonErrorKind::UnreachableBones: k = "bones with an invalid parent"; break;
    }
    std::string m = file + ": " + k;
    if (!detail.empty()) m += " (" + detail + ")";
    return m;
}

bool Skeleton::updateJoints(std::span<const Vec3> restCoords) {
    // A joint's position is the MEAN of its vertex cloud (skeleton.py:428-434).
    // Computed for every joint, not just the ones bones reference: rotation
    // planes name joints that no bone uses as a head or tail.
    jointPos.clear();
    jointPos.reserve(jointVerts.size());

    for (const auto& [joint, verts] : jointVerts) {
        Vec3 sum{};
        for (const uint32_t v : verts) {
            if (v >= restCoords.size()) return false;  // the reference reads unguarded
            sum = sum + restCoords[v];
        }
        jointPos.emplace(joint, sum * (1.0F / static_cast<float>(verts.size())));
    }

    for (Bone& b : bones) {
        const auto h = jointPos.find(b.headJoint);
        const auto t = jointPos.find(b.tailJoint);
        if (h == jointPos.end() || t == jointPos.end()) return false;
        b.head = h->second;
        b.tail = t->second;
    }
    return true;
}

namespace {

float length(const Vec3& v) noexcept {
    return std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
}

/// Returns false (leaving @p out untouched) when the vector is too short to
/// have a direction. The reference's matrix.normalize divides unguarded, which
/// yields inf/nan and propagates into every child matrix.
bool normalized(const Vec3& v, Vec3& out) noexcept {
    const float n = length(v);
    if (!(n > 1e-9F)) return false;
    out = v * (1.0F / n);
    return true;
}

constexpr Vec3 kFallbackNormal{0.0F, 1.0F, 0.0F};

}  // namespace

bool Skeleton::buildRestMatrices() {
    // A plane normal from three joints, counter-clockwise / right-handed:
    //     normalize(cross(normalize(p3 - p2), normalize(p2 - p1)))
    // (skeleton.py get_normal). Note the argument order -- cross(yvec, pvec),
    // not the other way round; swapping it flips every bone's roll by 180
    // degrees, which still produces a valid orthonormal basis.
    const auto planeNormal = [&](const std::string& plane, Vec3& out) -> bool {
        const auto it = planes.find(plane);
        if (it == planes.end()) return false;

        std::array<Vec3, 3> p{};
        for (size_t i = 0; i < 3; ++i) {
            const auto jp = jointPos.find(it->second[i]);
            if (jp == jointPos.end()) return false;
            p[i] = jp->second;
        }
        Vec3 pvec;
        Vec3 yvec;
        if (!normalized(p[1] - p[0], pvec)) return false;
        if (!normalized(p[2] - p[1], yvec)) return false;
        return normalized(foundation::cross(yvec, pvec), out);
    };

    for (Bone& b : bones) {
        Vec3 boneDir;
        if (!normalized(b.direction(), boneDir)) {
            // A zero-length bone is a TIP MARKER, not corrupt data. Mixamo's
            // own 65 bones include HeadTop_End, Left/RightToe_End and a 4th
            // segment on every finger; each sits exactly at its parent's tail
            // and deforms nothing (0 weighted vertices on all 13 in
            // mixamo_superset.mhskel, verified). Head == tail is how "the tip
            // is here" is expressed, so there is no direction to derive.
            //
            // It inherits the parent's basis -- the convention Blender applies
            // to leaf bones -- keeping its own head as the translation. A root
            // with no length has nothing to inherit and stays an error.
            //
            // Rejecting the whole skeleton instead, which is what this used to
            // do, made the 179-bone superset rig unusable for skinning
            // entirely: one tip marker failed and took the other 178 with it.
            if (b.parent < 0) return false;
            foundation::Mat4 g = bones[static_cast<size_t>(b.parent)].matRestGlobal;
            g.m[0][3]          = b.head.x;
            g.m[1][3]          = b.head.y;
            g.m[2][3]          = b.head.z;
            b.matRestGlobal    = g;
            b.length           = 0.0F;
            b.matRestRelative =
                foundation::rigidInverse(bones[static_cast<size_t>(b.parent)].matRestGlobal) * g;
            continue;
        }

        Vec3 normal = kFallbackNormal;
        if (!b.planeName.empty()) {
            Vec3 n;
            if (planeNormal(b.planeName, n)) normal = n;
        }

        // Z perpendicular to (normal, Y); X rebuilt perpendicular to (Y, Z).
        // The seed normal is generally NOT perpendicular to the bone, which is
        // why X is recomputed rather than used directly.
        Vec3 zAxis;
        if (!normalized(foundation::cross(normal, boneDir), zAxis)) {
            // normal parallel to the bone: seed with +Y, or +X if the bone is
            // itself +Y. Any perpendicular is as good as any other here.
            const Vec3 alt =
                (std::abs(boneDir.y) > 0.9F) ? Vec3{1.0F, 0.0F, 0.0F} : kFallbackNormal;
            if (!normalized(foundation::cross(alt, boneDir), zAxis)) return false;
        }
        Vec3 xAxis;
        if (!normalized(foundation::cross(boneDir, zAxis), xAxis)) return false;

        // Axes are COLUMNS; translation is the last column (skeleton.py writes
        // mat[:3,0], mat[:3,1], mat[:3,2] and mat[:3,3]).
        foundation::Mat4 g = foundation::Mat4::identity();
        g.m[0][0]          = xAxis.x;
        g.m[0][1]          = boneDir.x;
        g.m[0][2]          = zAxis.x;
        g.m[0][3]          = b.head.x;
        g.m[1][0]          = xAxis.y;
        g.m[1][1]          = boneDir.y;
        g.m[1][2]          = zAxis.y;
        g.m[1][3]          = b.head.y;
        g.m[2][0]          = xAxis.z;
        g.m[2][1]          = boneDir.z;
        g.m[2][2]          = zAxis.z;
        g.m[2][3]          = b.head.z;

        b.matRestGlobal = g;
        b.length        = length(b.direction());

        // Parents precede children in this list, so the parent's global matrix
        // is already final. rigidInverse is exact here: the basis is orthonormal
        // by construction.
        b.matRestRelative =
            (b.parent < 0)
                ? g
                : foundation::rigidInverse(bones[static_cast<size_t>(b.parent)].matRestGlobal) * g;
    }
    return true;
}

std::expected<Skeleton, SkeletonError> loadSkeleton(const std::filesystem::path& path) {
    // openForRead, not exists()+ifstream: a DIRECTORY satisfies both and
    // then parses as an empty file, so this reader used to accept one.
    // See foundation/FileRead.h for what each reader did before.
    auto opened = foundation::openForRead(path);
    if (!opened) {
        // NotAFile maps to Unreadable rather than NotFound: something IS
        // there, and saying "not found" about a path that exists sends
        // whoever is debugging it looking in the wrong place.
        const auto kind = opened.error() == foundation::FileReadErrorKind::NotFound
                              ? SkeletonErrorKind::NotFound
                              : SkeletonErrorKind::Unreadable;
        return std::unexpected(SkeletonError{kind, path.string(), {}});
    }
    std::ifstream& in = *opened;

    json root;
    try {
        // ordered_json preserves key order, which the bone ordering below
        // depends on. Parsing with exceptions on: this is a trust boundary and
        // a truncated or hand-broken rig must fail loudly.
        root = json::parse(in);
    } catch (const json::parse_error& e) {
        return std::unexpected(
            SkeletonError{SkeletonErrorKind::Malformed, path.string(), e.what()});
    }
    if (!root.is_object() || !root.contains("bones") || !root["bones"].is_object()) {
        return std::unexpected(
            SkeletonError{SkeletonErrorKind::Malformed, path.string(), "no \"bones\" object"});
    }

    Skeleton skel;
    skel.name        = root.value("name", std::string{"Skeleton"});
    skel.description = root.value("description", std::string{});
    skel.version     = root.value("version", 1);
    if (root.contains("weights_file") && root["weights_file"].is_string()) {
        skel.weightsFile = path.parent_path() / root["weights_file"].get<std::string>();
    }

    // `root.value(...)` returns by VALUE, and `.items()` only borrows it. Naming
    // the object keeps it alive for the loop: relying on P2718R0's extension to
    // the whole range expression dangles on toolchains that have not shipped it.
    const json joints = root.value("joints", json::object());
    for (const auto& [jointName, idxs] : joints.items()) {
        if (!idxs.is_array() || idxs.empty()) continue;  // skeleton.py:105-107
        std::vector<uint32_t> v;
        v.reserve(idxs.size());
        for (const auto& i : idxs) {
            if (i.is_number_unsigned()) v.push_back(i.get<uint32_t>());
        }
        if (!v.empty()) skel.jointVerts.emplace(jointName, std::move(v));
    }

    const json planes = root.value("planes", json::object());
    for (const auto& [planeName, planeJoints] : planes.items()) {
        if (!planeJoints.is_array() || planeJoints.size() != 3) continue;
        std::array<std::string, 3> p;
        bool ok = true;
        for (size_t i = 0; i < 3; ++i) {
            if (!planeJoints[i].is_string()) {
                ok = false;
                break;
            }
            p[i] = planeJoints[i].get<std::string>();
        }
        if (ok) skel.planes.emplace(planeName, std::move(p));
    }

    // ---- ordering ---------------------------------------------------------
    // Repeated relaxation over the bone map IN FILE ORDER, exactly as
    // skeleton.py:111-121: each pass appends every not-yet-placed bone whose
    // parent is already placed. `progress` is the reference's `prev_len`
    // anti-deadlock guard -- a parent cycle makes a pass add nothing, and the
    // loop must stop rather than spin.
    const json& boneMap = root["bones"];

    std::vector<std::string> fileOrder;
    fileOrder.reserve(boneMap.size());
    for (const auto& [boneName, unused] : boneMap.items()) {
        (void)unused;
        fileOrder.push_back(boneName);
    }

    std::unordered_map<std::string, int32_t> placed;  // name -> index
    std::vector<std::string> order;
    order.reserve(fileOrder.size());

    bool progress = true;
    while (order.size() != fileOrder.size() && progress) {
        progress = false;
        for (const std::string& boneName : fileOrder) {
            if (placed.contains(boneName)) continue;

            const json& def   = boneMap[boneName];
            const json parent = def.value("parent", json());
            const bool isRoot = parent.is_null() || (parent.is_string() && parent.empty());

            if (isRoot || placed.contains(parent.get<std::string>())) {
                placed.emplace(boneName, static_cast<int32_t>(order.size()));
                order.push_back(boneName);
                progress = true;
            }
        }
    }

    if (order.size() != fileOrder.size()) {
        std::string missing;
        for (const std::string& boneName : fileOrder) {
            if (!placed.contains(boneName)) {
                if (!missing.empty()) missing += ", ";
                missing += boneName;
            }
        }
        // The reference only warns here (skeleton.py:122-124) and carries on
        // with a partial skeleton. A rig that exports missing limbs with
        // nothing in the log is worse than a refusal.
        return std::unexpected(
            SkeletonError{SkeletonErrorKind::UnreachableBones, path.string(), missing});
    }

    // ---- second ordering --------------------------------------------------
    // `order` above is only how bones are ADDED. The canonical index order --
    // what getBones() returns, what the rest-matrix rows are indexed by, and
    // what every exporter writes -- is a REAL breadth-first walk from the
    // roots over each bone's children (skeleton.py:__cacheGetBones, a deque).
    //
    // The two are different, and using the first alone produced 153 of 163
    // bone names in the wrong slot. The first pass still matters: it fixes the
    // order children are appended to each parent, which is the sibling order
    // the BFS then emits.
    std::vector<std::vector<std::string>> childrenOf(order.size());
    std::vector<std::string> roots;
    for (const std::string& boneName : order) {
        const json parent = boneMap[boneName].value("parent", json());
        if (parent.is_string() && !parent.get<std::string>().empty()) {
            childrenOf[static_cast<size_t>(placed.at(parent.get<std::string>()))].push_back(
                boneName);
        } else {
            roots.push_back(boneName);
        }
    }

    std::vector<std::string> bfs;
    bfs.reserve(order.size());
    std::deque<std::string> queue(roots.begin(), roots.end());
    while (!queue.empty()) {
        const std::string boneName = queue.front();
        queue.pop_front();
        bfs.push_back(boneName);
        for (const std::string& child : childrenOf[static_cast<size_t>(placed.at(boneName))]) {
            queue.push_back(child);
        }
    }

    // Parent indices must refer to the FINAL order, not the insertion order.
    std::unordered_map<std::string, int32_t> finalIndex;
    finalIndex.reserve(bfs.size());
    for (size_t i = 0; i < bfs.size(); ++i)
        finalIndex.emplace(bfs[i], static_cast<int32_t>(i));

    skel.bones.reserve(bfs.size());
    for (const std::string& boneName : bfs) {
        const json& def = boneMap[boneName];
        Bone b;
        b.name      = boneName;
        b.headJoint = def.value("head", std::string{});
        b.tailJoint = def.value("tail", std::string{});
        b.planeName = def.contains("rotation_plane") ? planeNameOf(def["rotation_plane"]) : "";

        const json parent = def.value("parent", json());
        b.parent          = (parent.is_string() && !parent.get<std::string>().empty())
                                ? finalIndex.at(parent.get<std::string>())
                                : -1;
        skel.bones.push_back(std::move(b));
    }

    return skel;
}

}  // namespace mh::rig
