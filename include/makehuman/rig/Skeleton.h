// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include "makehuman/foundation/Types.h"

#include <cstdint>
#include <expected>
#include <filesystem>
#include <span>
#include <string>
#include <unordered_map>
#include <vector>

namespace mh::rig {

using foundation::Vec3;

/// One bone. Ported from `legacy/python/shared/skeleton.py:700-760`.
///
/// A bone's geometry is not stored in the file. The file names two *joints*,
/// and each joint is a cloud of base-mesh vertex indices; the joint's position
/// is the **mean** of those vertices (`skeleton.py:428-434`). That is what
/// makes the rig follow the body: change the mesh, recompute the means, and
/// every bone moves with it.
struct Bone {
    std::string name;
    /// Index into Skeleton::bones, or -1 for a root. Always < this bone's own
    /// index, because the bone list is ordered parents-first.
    int32_t parent{-1};

    std::string headJoint;
    std::string tailJoint;

    /// Name of the rotation plane that fixes this bone's roll, or empty when
    /// the file gave a plain number (`rotation_plane`, skeleton.py:127-131).
    std::string planeName;

    /// Filled by updateJoints(); meaningless before that.
    Vec3 head{};
    Vec3 tail{};

    [[nodiscard]] Vec3 direction() const noexcept { return tail - head; }
};

enum class SkeletonErrorKind { NotFound, Unreadable, Malformed, UnreachableBones };

struct SkeletonError {
    SkeletonErrorKind kind{};
    std::string file;
    std::string detail;

    [[nodiscard]] std::string message() const;
};

/// A bone hierarchy plus the joint vertex clouds that position it.
struct Skeleton {
    std::string name{"Skeleton"};
    std::string description;
    int32_t version{1};
    std::filesystem::path weightsFile;

    /// Breadth-first from the roots, matching `getBones()`. Parents always
    /// precede children. See loadSkeleton() for why this is NOT the same as
    /// the order the file lists them in.
    std::vector<Bone> bones;

    /// Joint name -> the base-mesh vertices whose mean is the joint position.
    std::unordered_map<std::string, std::vector<uint32_t>> jointVerts;

    /// Plane name -> the three joints defining it, used for bone roll.
    std::unordered_map<std::string, std::array<std::string, 3>> planes;

    [[nodiscard]] size_t boneCount() const noexcept { return bones.size(); }

    /// Recomputes every bone's head and tail from @p restCoords.
    ///
    /// @param restCoords the body's REST coordinates -- the reference uses
    ///        `getRestposeCoordinates()` (skeleton.py:431), not the posed ones,
    ///        so a skeleton built against posed coordinates is silently wrong.
    /// @return false if any joint references a vertex the mesh does not have.
    bool updateJoints(std::span<const Vec3> restCoords);
};

/// Parses a `.mhskel` (which is JSON).
///
/// **There are two orderings in the reference and they are not the same one.**
/// Getting this wrong is silent: the bone list still looks plausible.
///
/// 1. `fromFile` (`skeleton.py:111-124`) does repeated relaxation over the bone
///    map *in file order* -- each pass appends every bone whose parent is
///    already placed. This is what the reference calls "breadth-first"; it is
///    not. Its result depends on the order keys appear in the file, so the
///    parser must preserve that order (hence `nlohmann::ordered_json`). What it
///    actually determines is the order children are attached to each parent.
///
/// 2. `getBones()` (`__cacheGetBones`) then does a **real** breadth-first walk
///    from the roots with a deque. *That* is the canonical index order: the row
///    order of the rest-matrix arrays, and the order every exporter writes.
///
/// Both are needed, in that sequence -- (1) fixes sibling order, (2) fixes
/// index order. Using (1) alone put 153 of 163 bones in the wrong slot while
/// still producing a valid-looking parents-first list.
///
/// One deliberate divergence: a bone whose parent does not exist is dropped
/// with a warning by the reference (`skeleton.py:122-124`), which carries on
/// with a partial skeleton. We make it an error. A rig that exports missing
/// limbs with nothing in the log is worse than a refusal. The pass-adds-nothing
/// termination check is kept -- it is the reference's guard against a parent
/// cycle, and without it a cycle spins forever.
[[nodiscard]] std::expected<Skeleton, SkeletonError> loadSkeleton(
    const std::filesystem::path& path);

}  // namespace mh::rig
