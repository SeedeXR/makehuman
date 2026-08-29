// SPDX-License-Identifier: Apache-2.0
#include "makehuman/io/BvhReader.h"

#include "makehuman/foundation/Chars.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <fstream>
#include <numbers>
#include <optional>
#include <sstream>

namespace mh::io {
namespace {

using foundation::EulerOrder;
using foundation::Mat4;
using foundation::Vec3;

constexpr double kDegToRad = std::numbers::pi / 180.0;

std::vector<std::string> tokens(const std::string& line) {
    std::vector<std::string> out;
    std::istringstream in(line);
    std::string t;
    while (in >> t)
        out.push_back(t);
    return out;
}

std::optional<Channel> channelFromName(std::string_view s) {
    if (s == "Xposition") return Channel::Xposition;
    if (s == "Yposition") return Channel::Yposition;
    if (s == "Zposition") return Channel::Zposition;
    if (s == "Xrotation") return Channel::Xrotation;
    if (s == "Yrotation") return Channel::Yrotation;
    if (s == "Zrotation") return Channel::Zrotation;
    return std::nullopt;
}

/// Joint names to measure the up axis against, most reliable first. Each is a
/// bone whose length is dominated by the body's vertical extent.
constexpr std::array<std::string_view, 6> kUpProbeJoints{"spine03",      "spine02",      "spine01",
                                                         "upperleg02.L", "lowerleg02.L", "head"};

}  // namespace

std::string BvhError::message() const {
    const char* k = "unknown error";
    switch (kind) {
        case BvhErrorKind::NotFound: k = "file not found"; break;
        case BvhErrorKind::Unreadable: k = "file unreadable"; break;
        case BvhErrorKind::Malformed: k = "malformed BVH"; break;
        case BvhErrorKind::FrameDataMismatch:
            k = "frame data does not match the channel count";
            break;
    }
    std::string m = file;
    if (line > 0) m += ':' + std::to_string(line);
    m += ": ";
    m += k;
    if (!detail.empty()) m += " (" + detail + ")";
    return m;
}

std::expected<BvhFile, BvhError> readBvh(const std::filesystem::path& path,
                                         const BvhReadOptions& options) {
    std::error_code ec;
    if (!std::filesystem::exists(path, ec)) {
        return std::unexpected(BvhError{BvhErrorKind::NotFound, path.string(), 0, {}});
    }
    std::ifstream in(path);
    if (!in) {
        return std::unexpected(BvhError{BvhErrorKind::Unreadable, path.string(), 0, {}});
    }

    BvhFile out;
    uint32_t lineNo = 0;
    std::string line;

    const auto fail = [&](std::string detail) {
        return std::unexpected(
            BvhError{BvhErrorKind::Malformed, path.string(), lineNo, std::move(detail)});
    };

    // ---- HIERARCHY --------------------------------------------------------
    // Offsets are stored RAW here. The up-axis fix is applied afterwards,
    // because Auto has to measure the raw geometry to decide.
    std::vector<int32_t> stack;
    bool sawHierarchy = false;

    while (std::getline(in, line)) {
        ++lineNo;
        const auto t = tokens(line);
        if (t.empty()) continue;

        if (t[0] == "HIERARCHY") {
            sawHierarchy = true;
        } else if (t[0] == "ROOT" || t[0] == "JOINT") {
            if (t.size() < 2) return fail(t[0] + " without a name");
            BvhJoint j;
            j.name   = t[1];
            j.parent = stack.empty() ? -1 : stack.back();
            out.joints.push_back(std::move(j));
            stack.push_back(static_cast<int32_t>(out.joints.size() - 1));
        } else if (t[0] == "End") {
            BvhJoint j;
            // An End Site is unnamed in the file; give it a derived name so it
            // is addressable and cannot collide with a real joint.
            j.name = (stack.empty() ? std::string{"End"}
                                    : out.joints[static_cast<size_t>(stack.back())].name) +
                     "_end";
            j.parent  = stack.empty() ? -1 : stack.back();
            j.endSite = true;
            out.joints.push_back(std::move(j));
            stack.push_back(static_cast<int32_t>(out.joints.size() - 1));
        } else if (t[0] == "{") {
            // Scope opens with the joint already pushed.
        } else if (t[0] == "}") {
            if (stack.empty()) return fail("unbalanced '}'");
            stack.pop_back();
        } else if (t[0] == "OFFSET") {
            if (stack.empty()) return fail("OFFSET outside a joint");
            if (t.size() < 4) return fail("OFFSET needs three numbers");
            float x = 0;
            float y = 0;
            float z = 0;
            if (!foundation::parseFloat(t[1], x) || !foundation::parseFloat(t[2], y) ||
                !foundation::parseFloat(t[3], z)) {
                return fail("OFFSET is not numeric");
            }
            out.joints[static_cast<size_t>(stack.back())].offset = Vec3{x, y, z};
        } else if (t[0] == "CHANNELS") {
            if (stack.empty()) return fail("CHANNELS outside a joint");
            if (t.size() < 2) return fail("CHANNELS needs a count");
            int n = 0;
            if (!foundation::parseInteger(t[1], n) || n < 0) return fail("bad CHANNELS count");
            if (t.size() < static_cast<size_t>(n) + 2) return fail("too few channel names");

            auto& j = out.joints[static_cast<size_t>(stack.back())];
            for (int c = 0; c < n; ++c) {
                const auto ch = channelFromName(t[static_cast<size_t>(c) + 2]);
                if (!ch) return fail("unknown channel " + t[static_cast<size_t>(c) + 2]);
                j.channels.push_back(*ch);
            }
        } else if (t[0] == "MOTION") {
            break;
        }
    }
    if (!sawHierarchy || out.joints.empty()) return fail("no HIERARCHY");
    if (!stack.empty()) return fail("unbalanced '{'");

    // ---- rest positions, from the raw offsets ------------------------------
    const auto recomputePositions = [&] {
        for (auto& j : out.joints) {
            j.position = (j.parent < 0)
                             ? j.offset
                             : out.joints[static_cast<size_t>(j.parent)].position + j.offset;
        }
    };
    recomputePositions();

    // ---- decide the up axis ------------------------------------------------
    bool zUp = (options.upAxis == UpAxis::ZUp);
    if (options.upAxis == UpAxis::Auto) {
        // Measure a bone that should be dominated by the body's vertical
        // extent, and see which component wins.
        for (const auto& probe : kUpProbeJoints) {
            const auto it = std::ranges::find_if(
                out.joints, [&](const BvhJoint& j) { return j.name == probe; });
            if (it == out.joints.end()) continue;

            const auto idx   = static_cast<int32_t>(std::distance(out.joints.begin(), it));
            const auto child = std::ranges::find_if(
                out.joints, [&](const BvhJoint& j) { return j.parent == idx; });
            if (child == out.joints.end()) continue;  // an end effector proves nothing

            const Vec3 d = child->position - it->position;
            zUp          = std::abs(d.z) > std::abs(d.y);
            break;
        }
    }
    out.convertedFromZUp = zUp;

    if (zUp) {
        // (x, y, z) -> (x, z, -y): a right-handed rotation that carries the
        // file's up axis onto +Y.
        for (auto& j : out.joints) {
            const float y = j.offset.y;
            j.offset.y    = j.offset.z;
            j.offset.z    = -y;
        }
        recomputePositions();
    }

    // ---- channel -> Euler order -------------------------------------------
    // Building the order string by PREPENDING each rotation letter as it is
    // read reverses the channel order, which is what makes "X Y Z" channels
    // mean the "szyx" convention. Under the Z-up fix, Y and Z swap roles, so
    // the same channels become "syzx".
    for (auto& j : out.joints) {
        std::string order;
        int rotations = 0;
        for (const Channel c : j.channels) {
            switch (c) {
                case Channel::Xrotation:
                    order = "x" + order;
                    ++rotations;
                    break;
                case Channel::Yrotation:
                    order = (zUp ? "z" : "y") + order;
                    ++rotations;
                    break;
                case Channel::Zrotation:
                    order = (zUp ? "y" : "z") + order;
                    ++rotations;
                    break;
                default: break;
            }
        }
        if (rotations >= 3) {
            const auto parsed = foundation::eulerOrderFromString("s" + order);
            if (!parsed) return fail("unsupported rotation channel order s" + order);
            j.rotationOrder = *parsed;
            j.hasRotation   = true;
        }
    }

    // ---- MOTION ------------------------------------------------------------
    size_t frames    = 0;
    double frameTime = 0.0;
    bool sawFrames   = false;
    bool sawTime     = false;

    while (std::getline(in, line)) {
        ++lineNo;
        const auto t = tokens(line);
        if (t.empty()) continue;
        if (t[0] == "Frames:") {
            long v = 0;
            if (t.size() < 2 || !foundation::parseInteger(t[1], v) || v < 0) {
                return fail("bad Frames:");
            }
            frames    = static_cast<size_t>(v);
            sawFrames = true;
        } else if (t[0] == "Frame") {
            float v = 0;
            if (t.size() < 3 || !foundation::parseFloat(t[2], v)) return fail("bad Frame Time:");
            frameTime = static_cast<double>(v);
            sawTime   = true;
            break;
        }
    }
    if (!sawFrames || !sawTime) return fail("no Frames:/Frame Time:");

    out.frameCount = frames;
    out.frameTime  = frameTime;

    size_t totalChannels = 0;
    for (const auto& j : out.joints)
        totalChannels += j.channels.size();

    for (auto& j : out.joints)
        j.frames.assign(frames, Mat4::identity());

    for (size_t f = 0; f < frames; ++f) {
        std::vector<float> row;
        row.reserve(totalChannels);
        // A frame's values may wrap across lines; keep reading until the row is
        // full rather than assuming one line per frame.
        while (row.size() < totalChannels && std::getline(in, line)) {
            ++lineNo;
            for (const auto& t : tokens(line)) {
                float v = 0;
                if (!foundation::parseFloat(t, v)) return fail("non-numeric motion value " + t);
                row.push_back(v);
            }
        }
        if (row.size() != totalChannels) {
            return std::unexpected(BvhError{BvhErrorKind::FrameDataMismatch, path.string(), lineNo,
                                            "frame " + std::to_string(f) + " has " +
                                                std::to_string(row.size()) + " of " +
                                                std::to_string(totalChannels) + " channel values"});
        }

        size_t col = 0;
        for (size_t ji = 0; ji < out.joints.size(); ++ji) {
            auto& j = out.joints[ji];

            double ax           = 0.0;
            double ay           = 0.0;
            double az           = 0.0;
            float tx            = 0.0F;
            float ty            = 0.0F;
            float tz            = 0.0F;
            bool anyTranslation = false;

            for (const Channel c : j.channels) {
                const float v = row[col++];
                switch (c) {
                    case Channel::Xposition:
                        tx             = v;
                        anyTranslation = true;
                        break;
                    // Under the Z-up fix the position channels swap the same way
                    // the offsets do, sign included.
                    case Channel::Yposition:
                        if (zUp) {
                            tz = -v;
                        } else {
                            ty = v;
                        }
                        anyTranslation = true;
                        break;
                    case Channel::Zposition:
                        if (zUp) {
                            ty = v;
                        } else {
                            tz = v;
                        }
                        anyTranslation = true;
                        break;
                    case Channel::Xrotation: ax = static_cast<double>(v) * kDegToRad; break;
                    case Channel::Yrotation:
                        ay = static_cast<double>(v) * kDegToRad * (zUp ? -1.0 : 1.0);
                        break;
                    case Channel::Zrotation: az = static_cast<double>(v) * kDegToRad; break;
                }
            }

            Mat4 m = Mat4::identity();
            if (j.hasRotation) {
                // The angles are passed in reverse channel order to match the
                // reversed order string built above.
                m = foundation::eulerMatrix(az, ay, ax, j.rotationOrder);
            }

            const bool mayTranslate =
                options.translation == TranslationPolicy::All ||
                (options.translation == TranslationPolicy::RootOnly && j.parent < 0);
            if (mayTranslate && anyTranslation) {
                m.m[0][3] = tx;
                m.m[1][3] = ty;
                m.m[2][3] = tz;
            }
            j.frames[f] = m;
        }
    }

    return out;
}

}  // namespace mh::io
