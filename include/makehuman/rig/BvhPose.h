// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include "makehuman/io/BvhReader.h"
#include "makehuman/rig/Skeleton.h"

#include <span>

namespace mh::rig {

/// Builds a single-frame `BvhFile` from a skeleton and a pose.
///
/// `io::writeBvh` could always serialise a `BvhFile`; nothing could produce one
/// from a posed character, so the application could read a BVH and never write
/// what it was showing.
///
/// **Conventions taken from the reference** (`shared/bvh.py:369-428`), not
/// invented:
///   * the root carries `Xposition Yposition Zposition Zrotation Xrotation
///     Yrotation`; every other joint carries `Zrotation Xrotation Yrotation`;
///   * a bone with no children gets an `End Site` whose offset is
///     `tail - head`, which is what tells a consumer how long the last bone is;
///   * a joint's offset is its head relative to its parent's head.
///
/// **`dummyJoints` is deliberately NOT done.** The reference inserts an extra
/// `__name` joint wherever a bone's head is not its parent's tail, because
/// tools disagree about where a bone ends when a parent has several children
/// (`bvh.py:374-387`). It is a supported reference mode to omit them
/// (`dummyJoints=False`), it keeps one joint per bone so the file round-trips
/// through `readBvh` onto the same skeleton, and nothing needs the other mode
/// yet.
///
/// @param localPose one transform per bone, in the bone's own rest frame --
///        exactly what `poseToBoneLocal` produces and what BVH channels mean.
///        Empty means the rest pose, which is a valid single-frame BVH.
/// @return a file with `frameCount == 1`. Empty joints if @p localPose is
///         neither empty nor one entry per bone.
[[nodiscard]] io::BvhFile toBvhPose(const Skeleton& skeleton,
                                    std::span<const foundation::Mat4> localPose);

}  // namespace mh::rig
