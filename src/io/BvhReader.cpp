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
        // The body's EXTENT, not a named joint and not a sum of bone lengths.
        //
        // This used to look for one of six probe joints -- `spine03`, `head`
        // and four more -- and compare the direction to that joint's first
        // child. Two things were wrong with it. Those are THIS rig's bone
        // names, so every file written for another skeleton matched nothing and
        // fell through to Y-up: MEASURED, all three `data/animations/*.bvh`
        // name the old MakeHuman skeleton, match no probe, and were loading
        // unconverted while their own `.mhanim` declares `z_is_up`. And the
        // probe could be confidently wrong even when it did match -- `head`'s
        // first child in this rig is `temporalis02.R`, a sideways face bone
        // whose direction says nothing about which way is up.
        //
        // Extent is naming-independent and has room to spare. MEASURED over
        // every BVH that ships, as max-minus-min of joint POSITION per axis:
        //
        //     walk1 / zombieWalk1   y  2.06   z 16.36   (7.96x)
        //     dance1                y  2.12   z 15.93   (7.50x)
        //     tpose / benchmark     y  3.98   z 16.57   (4.16x)
        //     face-poseunits        y  3.98   z 16.68   (4.19x)
        //
        // Summing |offset| instead -- the first attempt -- measured bone-length
        // noise rather than body extent and gave tpose.bvh 69.47 against 69.57,
        // a margin of 0.14% across 222 joints. Positions also make a root
        // OFFSET carrying world placement harmless: it shifts every joint
        // equally and cancels out of a max-minus-min, where it would have been
        // added straight onto a sum.
        //
        // Z-up is claimed only when Z CLEARLY dominates. Anything ambiguous
        // keeps Y-up, which is the format's convention and what an unrecognised
        // file used to get: a partial rig -- a face-only or hand-only capture --
        // has no meaningful vertical extent, and rotating one on a coin-flip
        // would be worse than leaving it alone.
        // Empty gives 0, so a file with no joints falls to Y-up rather than
        // comparing uninitialised bounds.
        const auto extentAlong = [&out](float foundation::Vec3::* axis) {
            if (out.joints.empty()) return 0.0F;
            float lo = out.joints.front().position.*axis;
            float hi = lo;
            for (const BvhJoint& j : out.joints) {
                lo = std::min(lo, j.position.*axis);
                hi = std::max(hi, j.position.*axis);
            }
            return hi - lo;
        };
        const float extentY = extentAlong(&foundation::Vec3::y);
        const float extentZ = extentAlong(&foundation::Vec3::z);

        // 1.5 sits an order of magnitude below the 4.16x margin of the
        // tightest file that ships, and well above 1.0, so it separates a real
        // Z-up body from a rig that merely leans.
        //
        // SURVIVING MUTATION, recorded rather than dressed up as covered:
        // corrupting one of the four bounds -- reading `position.y` into `loZ`,
        // say -- changes no verdict any test can see. That is the margin doing
        // its job: every real file clears it by 4x or more, so a damaged bound
        // still lands on the right side. It means the bounds are not pinned
        // individually, only their conclusion is. Exposing the extents to pin
        // them would be API nobody else wants.
        constexpr float kDominance = 1.5F;
        zUp                        = extentZ > extentY * kDominance;
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
