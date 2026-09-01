// SPDX-License-Identifier: AGPL-3.0-or-later
#include "makehuman/rig/BvhPose.h"

#include "makehuman/foundation/Transform.h"

namespace mh::rig {

namespace {

/// The rotation order the reference's channel list implies.
///
/// `Zrotation Xrotation Yrotation` is what `bvh.py:411` writes for every joint,
/// and `BvhReader` turns that channel list into an order string by PREPENDING
/// each axis as it reads it (`BvhReader.cpp:206-218`): z, then xz, then yxz.
/// Hence `syxz`.
///
/// Derived here rather than written as a constant somewhere else, because the
/// reader assigns channel values by AXIS IDENTITY and then always calls
/// `eulerMatrix(az, ay, ax, order)` -- so the order and the channel list must
/// agree or the file re-reads as a different pose.
foundation::EulerOrder bvhOrder() {
    const auto o = foundation::eulerOrderFromString("syxz");
    return o ? *o : foundation::EulerOrder{};
}

}  // namespace

io::BvhFile toBvhPose(const Skeleton& skeleton, std::span<const foundation::Mat4> localPose) {
    io::BvhFile out;
    if (skeleton.bones.empty()) return out;
    if (!localPose.empty() && localPose.size() != skeleton.bones.size()) return out;

    // A bone with no children needs an End Site, so the count is known first.
    std::vector<bool> hasChild(skeleton.bones.size(), false);
    for (const Bone& b : skeleton.bones) {
        if (b.parent >= 0) hasChild[static_cast<size_t>(b.parent)] = true;
    }

    // Bone index -> joint index. Bones are already parents-first, and End Sites
    // are appended as they are reached, so a parent's joint always exists by
    // the time a child needs it.
    std::vector<int32_t> jointOf(skeleton.bones.size(), -1);

    for (size_t b = 0; b < skeleton.bones.size(); ++b) {
        const Bone& bone = skeleton.bones[b];

        io::BvhJoint j;
        j.name   = bone.name;
        j.parent = bone.parent >= 0 ? jointOf[static_cast<size_t>(bone.parent)] : -1;

        // Head relative to the PARENT'S HEAD (`bone.getRestOffset()`), not to
        // its tail: the tail is where the reference's dummy joints come in, and
        // this build has none.
        j.offset =
            bone.parent >= 0
                ? foundation::Vec3{bone.head.x -
                                       skeleton.bones[static_cast<size_t>(bone.parent)].head.x,
                                   bone.head.y -
                                       skeleton.bones[static_cast<size_t>(bone.parent)].head.y,
                                   bone.head.z -
                                       skeleton.bones[static_cast<size_t>(bone.parent)].head.z}
                : bone.head;
        j.position = bone.head;

        if (bone.parent < 0) {
            j.channels = {io::Channel::Xposition, io::Channel::Yposition, io::Channel::Zposition,
                          io::Channel::Zrotation, io::Channel::Xrotation, io::Channel::Yrotation};
        } else {
            j.channels = {io::Channel::Zrotation, io::Channel::Xrotation, io::Channel::Yrotation};
        }
        j.rotationOrder = bvhOrder();
        j.hasRotation   = true;
        j.frames.push_back(localPose.empty() ? foundation::Mat4::identity() : localPose[b]);

        jointOf[b] = static_cast<int32_t>(out.joints.size());
        out.joints.push_back(std::move(j));

        if (!hasChild[b]) {
            // `tail - head`, which is what tells a consumer how long the last
            // bone is. Without it a leaf bone has no length at all.
            io::BvhJoint end;
            // The reader's own convention for an End Site, which is unnamed in
            // the file (BvhReader.cpp:109-113). Matching it is what lets a
            // written file compare equal to a read one.
            end.name     = bone.name + "_end";
            end.parent   = jointOf[b];
            end.endSite  = true;
            end.offset   = foundation::Vec3{bone.tail.x - bone.head.x, bone.tail.y - bone.head.y,
                                          bone.tail.z - bone.head.z};
            end.position = bone.tail;
            out.joints.push_back(std::move(end));
        }
    }

    out.frameCount = 1;
    // 1/24 s. BVH requires a frame time and a single-frame pose has no natural
    // one; the reference uses the animation's rate (`bvh.py:428`) and there is
    // no animation here.
    out.frameTime = 1.0 / 24.0;
    return out;
}

}  // namespace mh::rig
