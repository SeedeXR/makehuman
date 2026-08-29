// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "makehuman/foundation/Transform.h"
#include "makehuman/foundation/Types.h"

#include <cstdint>
#include <expected>
#include <filesystem>
#include <string>
#include <vector>

namespace mh::io {

/// Biovision Hierarchy (`.bvh`) reader.
///
/// Written from the published format -- HIERARCHY / ROOT / JOINT / End Site /
/// OFFSET / CHANNELS, then MOTION with `Frames:`, `Frame Time:` and one
/// whitespace-separated row of channel values per frame. BVH is an open
/// interchange format, so this is a format reader rather than a translation of
/// anyone's implementation, and it lives on the permissive side of the licence
/// boundary.
///
/// Where the format is genuinely ambiguous, this matches what MakeHuman's
/// assets need, because those are the files it has to read. Those choices are
/// called out individually below.

/// Which channel a column of the MOTION rows carries.
enum class Channel : uint8_t { Xposition, Yposition, Zposition, Xrotation, Yrotation, Zrotation };

struct BvhJoint {
    std::string name;
    int32_t parent{-1};   ///< index into BvhFile::joints, -1 for the root
    bool endSite{false};  ///< "End Site" blocks have an offset but no channels

    foundation::Vec3 offset{};    ///< relative to the parent, after any up-axis fix
    foundation::Vec3 position{};  ///< accumulated model-space rest position

    std::vector<Channel> channels;

    /// The Euler order these channels imply, after any up-axis fix. Empty for a
    /// joint with fewer than three rotation channels.
    foundation::EulerOrder rotationOrder{};
    bool hasRotation{false};

    /// One transform per frame. Rotation from the joint's own channels;
    /// translation only where the file allows it (see TranslationPolicy).
    std::vector<foundation::Mat4> frames;
};

/// Which joints may carry translation.
///
/// BVH permits position channels on any joint, but for a skeletal *pose* only
/// the root's translation is usually meaningful -- letting every joint
/// translate lets a limb detach from its parent. MakeHuman's assets are
/// authored on that assumption.
enum class TranslationPolicy : uint8_t { RootOnly, All, None };

/// How to treat the file's up axis.
///
/// BVH does not record which axis is up, and both conventions are in the wild.
/// `Auto` measures it: it finds a joint from a known humanoid set that has a
/// child, and compares |dy| against |dz| along that bone. A humanoid's spine or
/// leg is longer vertically than in depth, so whichever component dominates
/// names the up axis. Both MakeHuman pose files measure as **Z-up**.
///
/// Getting this wrong does not fail -- it produces a complete, plausible
/// skeleton lying on its side.
enum class UpAxis : uint8_t { Auto, YUp, ZUp };

struct BvhReadOptions {
    UpAxis upAxis{UpAxis::Auto};
    TranslationPolicy translation{TranslationPolicy::RootOnly};
};

struct BvhFile {
    std::vector<BvhJoint> joints;  ///< parents always precede children
    size_t frameCount{};
    double frameTime{};
    bool convertedFromZUp{};  ///< what Auto resolved to

    [[nodiscard]] size_t jointCount() const noexcept { return joints.size(); }
};

enum class BvhErrorKind { NotFound, Unreadable, Malformed, FrameDataMismatch };

struct BvhError {
    BvhErrorKind kind{};
    std::string file;
    uint32_t line{};
    std::string detail;

    [[nodiscard]] std::string message() const;
};

[[nodiscard]] std::expected<BvhFile, BvhError> readBvh(const std::filesystem::path& path,
                                                       const BvhReadOptions& options = {});

}  // namespace mh::io
