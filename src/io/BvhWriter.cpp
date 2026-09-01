// SPDX-License-Identifier: Apache-2.0
#include "makehuman/io/BvhWriter.h"

#include "makehuman/foundation/Transform.h"

#include <cmath>
#include <fstream>
#include <functional>
#include <numbers>
#include <string>
#include <vector>

namespace mh::io {
namespace {

using foundation::Mat4;

constexpr double kRadToDeg = 180.0 / std::numbers::pi;

/// The channel sequence that re-reads as @p order.
///
/// BvhReader builds the order string by PREPENDING each rotation letter as it
/// reads the channels, so the order string is the channel sequence reversed.
/// Inverting that here is what keeps read -> write -> read stable; copying the
/// source channel names instead would break on any file the reader converted
/// from Z-up.
std::vector<Channel> channelsFor(const foundation::EulerOrder& order) {
    const std::string_view name = foundation::eulerOrderName(order);
    std::vector<Channel> out;
    if (name.size() < 4) return out;  // not one of the 24
    // Skip the leading frame letter ('s' or 'r'), then walk backwards.
    for (size_t i = name.size(); i-- > 1;) {
        switch (name[i]) {
            case 'x': out.push_back(Channel::Xrotation); break;
            case 'y': out.push_back(Channel::Yrotation); break;
            case 'z': out.push_back(Channel::Zrotation); break;
            default: return {};
        }
    }
    return out;
}

std::string_view channelName(Channel c) {
    switch (c) {
        case Channel::Xposition: return "Xposition";
        case Channel::Yposition: return "Yposition";
        case Channel::Zposition: return "Zposition";
        case Channel::Xrotation: return "Xrotation";
        case Channel::Yrotation: return "Yrotation";
        case Channel::Zrotation: return "Zrotation";
    }
    return "Xrotation";
}

bool hasPosition(const BvhJoint& j) {
    for (const Channel c : j.channels) {
        if (c == Channel::Xposition || c == Channel::Yposition || c == Channel::Zposition)
            return true;
    }
    return false;
}

/// Channels as they will be written: positions first (BVH convention), then the
/// rotations implied by rotationOrder.
std::vector<Channel> writtenChannels(const BvhJoint& j) {
    std::vector<Channel> out;
    if (j.endSite) return out;
    if (hasPosition(j)) {
        out.push_back(Channel::Xposition);
        out.push_back(Channel::Yposition);
        out.push_back(Channel::Zposition);
    }
    if (j.hasRotation) {
        const auto rot = channelsFor(j.rotationOrder);
        out.insert(out.end(), rot.begin(), rot.end());
    }
    return out;
}

void indent(std::ostream& os, int depth) {
    for (int i = 0; i < depth; ++i)
        os << "  ";
}

void writeJoint(std::ostream& os, const BvhFile& bvh, size_t index, int depth) {
    const BvhJoint& j = bvh.joints[index];

    indent(os, depth);
    if (j.endSite) {
        os << "End Site\n";
    } else if (j.parent < 0) {
        os << "ROOT " << j.name << '\n';
    } else {
        os << "JOINT " << j.name << '\n';
    }
    indent(os, depth);
    os << "{\n";

    indent(os, depth + 1);
    os << "OFFSET " << j.offset.x << ' ' << j.offset.y << ' ' << j.offset.z << '\n';

    const auto channels = writtenChannels(j);
    if (!j.endSite) {
        indent(os, depth + 1);
        os << "CHANNELS " << channels.size();
        for (const Channel c : channels)
            os << ' ' << channelName(c);
        os << '\n';
    }

    for (size_t k = 0; k < bvh.joints.size(); ++k) {
        if (bvh.joints[k].parent == static_cast<int32_t>(index)) writeJoint(os, bvh, k, depth + 1);
    }

    indent(os, depth);
    os << "}\n";
}

}  // namespace

std::expected<void, BvhError> writeBvh(const std::filesystem::path& path, const BvhFile& bvh) {
    std::ofstream out(path);
    if (!out) {
        return std::unexpected(
            BvhError{BvhErrorKind::Unreadable, path.string(), 0, "cannot open for writing"});
    }
    out.precision(9);

    out << "HIERARCHY\n";
    for (size_t i = 0; i < bvh.joints.size(); ++i) {
        if (bvh.joints[i].parent < 0) writeJoint(out, bvh, i, 0);
    }

    out << "MOTION\n";
    out << "Frames: " << bvh.frameCount << '\n';
    out << "Frame Time: " << bvh.frameTime << '\n';

    // MOTION values follow the HIERARCHY, not the array.
    //
    // The hierarchy is written depth-first from each root; the motion loop used
    // to walk `bvh.joints` in array order. For a file that came from `readBvh`
    // those are the same order, so every round-trip test passed -- but a
    // `BvhFile` built from a skeleton is in the skeleton's own parents-first
    // order, and the two then disagree. The file parses, every joint gets three
    // plausible angles, and they belong to other joints: measured at 0.96 on a
    // matrix element, on the arms of a T-pose.
    std::vector<size_t> hierarchyOrder;
    hierarchyOrder.reserve(bvh.joints.size());
    {
        const std::function<void(size_t)> visit = [&](size_t i) {
            hierarchyOrder.push_back(i);
            for (size_t k = 0; k < bvh.joints.size(); ++k)
                if (bvh.joints[k].parent == static_cast<int32_t>(i)) visit(k);
        };
        for (size_t i = 0; i < bvh.joints.size(); ++i)
            if (bvh.joints[i].parent < 0) visit(i);
    }

    for (size_t f = 0; f < bvh.frameCount; ++f) {
        bool first = true;
        for (const size_t ji : hierarchyOrder) {
            const BvhJoint& j   = bvh.joints[ji];
            const auto channels = writtenChannels(j);
            if (channels.empty()) continue;

            const Mat4& m = (f < j.frames.size()) ? j.frames[f] : Mat4::identity();
            std::array<double, 3> angles{0.0, 0.0, 0.0};
            if (j.hasRotation) angles = foundation::eulerFromMatrix(m, j.rotationOrder);

            // The reader assigns channel values by AXIS IDENTITY, not by
            // position -- Xrotation always lands in ax, Yrotation in ay,
            // Zrotation in az -- and then always calls
            // eulerMatrix(az, ay, ax, order) (BvhReader.cpp:323-337).
            //
            // So eulerFromMatrix's (ai, aj, ak) are (az, ay, ax) in that same
            // fixed sense, and each angle must be written against its own axis's
            // channel. Emitting them positionally instead only agrees when the
            // channels happen to be Z, Y, X, and silently produces a completely
            // different rotation otherwise -- measured at 1.69 on a matrix
            // element before this was fixed.
            const double az = angles[0] * kRadToDeg;
            const double ay = angles[1] * kRadToDeg;
            const double ax = angles[2] * kRadToDeg;
            for (const Channel c : channels) {
                if (!first) out << ' ';
                first = false;
                switch (c) {
                    case Channel::Xposition: out << m.m[0][3]; break;
                    case Channel::Yposition: out << m.m[1][3]; break;
                    case Channel::Zposition: out << m.m[2][3]; break;
                    case Channel::Xrotation: out << ax; break;
                    case Channel::Yrotation: out << ay; break;
                    case Channel::Zrotation: out << az; break;
                }
            }
        }
        out << '\n';
    }

    out.flush();
    if (!out) {
        return std::unexpected(
            BvhError{BvhErrorKind::Unreadable, path.string(), 0, "write failed"});
    }
    return {};
}

}  // namespace mh::io
