// SPDX-License-Identifier: AGPL-3.0-or-later
//
// BVH import, against the reference's own parsed result.
//
// The reader itself is written from the published BVH format and lives in the
// Apache-2.0 io module; this test is the parity harness for it.
//
// Regenerate with:
//     ./.venv-mh/bin/python tools/capture_fixture.py bvh

#include "makehuman/io/BvhReader.h"
#include "makehuman/io/BvhWriter.h"

#include <nlohmann/json.hpp>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <map>
#include <string>
#include <vector>

using namespace mh;

namespace {

std::filesystem::path fixtureDir() {
    return std::filesystem::path(MH_GOLDEN_DIR) / "bvh";
}

std::vector<float> readFloats(const std::filesystem::path& p) {
    std::ifstream in(p, std::ios::binary | std::ios::ate);
    if (!in) return {};
    const auto bytes = static_cast<size_t>(in.tellg());
    in.seekg(0);
    std::vector<float> out(bytes / sizeof(float));
    in.read(reinterpret_cast<char*>(out.data()), static_cast<std::streamsize>(bytes));
    return out;
}

nlohmann::json jointMeta(const std::string& stem) {
    std::ifstream in(fixtureDir() / (stem + "_joints.json"));
    REQUIRE(in);
    return nlohmann::json::parse(in);
}

struct Source {
    const char* path;
    const char* stem;
    size_t joints;
    size_t frames;
};

const std::vector<Source> kSources{
    {"poses/tpose.bvh", "tpose", 222, 1},
    {"poseunits/face-poseunits.bvh", "faceposeunits", 212, 60},
};

io::BvhFile load(const Source& s) {
    auto r = io::readBvh(std::filesystem::path(MH_DATA_DIR) / s.path);
    REQUIRE(r.has_value());
    return std::move(*r);
}

}  // namespace

// The up axis is not recorded in a BVH file, and both MakeHuman pose files are
// Z-up. A reader that skips the detection produces a complete, plausible
// skeleton lying on its side -- so this is checked first and explicitly.
TEST_CASE("both shipped BVH files are detected as Z-up", "[bvh][golden][parity]") {
    for (const auto& s : kSources) {
        CAPTURE(s.path);
        const auto f = load(s);
        CHECK(f.convertedFromZUp);
        CHECK(f.jointCount() == s.joints);
        CHECK(f.frameCount == s.frames);
        CHECK(f.frameTime > 0.0);
    }
}

TEST_CASE("joint hierarchy, offsets and positions match the reference", "[bvh][golden][parity]") {
    for (const auto& s : kSources) {
        CAPTURE(s.path);
        const auto f    = load(s);
        const auto meta = jointMeta(s.stem);
        REQUIRE(meta.size() == f.jointCount());

        const auto offsets   = readFloats(fixtureDir() / (std::string(s.stem) + "_offsets.bin"));
        const auto positions = readFloats(fixtureDir() / (std::string(s.stem) + "_positions.bin"));
        REQUIRE(offsets.size() == f.jointCount() * 3);

        // The reference returns joints breadth-first; this reader returns them
        // in FILE order, which is depth-first and is what the MOTION channel
        // layout follows. Match by name rather than index.
        //
        // End sites cannot be matched that way: the reference names every one
        // of them "End effector", so the names collide. This reader derives
        // "<parent>_end", which is unique and addressable -- a deliberate
        // improvement. They are matched through their parent instead.
        std::map<std::string, size_t> refByName;
        std::map<std::string, size_t> refEndByParent;
        for (size_t i = 0; i < meta.size(); ++i) {
            const auto name = meta[i]["name"].get<std::string>();
            if (meta[i]["isEndConnector"].get<bool>() && name == "End effector") {
                const auto p = meta[i]["parent"];
                if (!p.is_null()) refEndByParent[p.get<std::string>()] = i;
            } else {
                refByName[name] = i;
            }
        }

        size_t matched    = 0;
        size_t endMatched = 0;
        float worstOffset = 0.0F;
        float worstPos    = 0.0F;

        for (size_t i = 0; i < f.joints.size(); ++i) {
            const auto& j = f.joints[i];
            CAPTURE(j.name);

            size_t ref = 0;
            if (j.endSite) {
                REQUIRE(j.parent >= 0);
                const auto parentName = f.joints[static_cast<size_t>(j.parent)].name;
                const auto it         = refEndByParent.find(parentName);
                if (it == refEndByParent.end()) continue;
                ref = it->second;
                ++endMatched;
            } else {
                const auto it = refByName.find(j.name);
                REQUIRE(it != refByName.end());
                ref = it->second;
                ++matched;

                // Parent linkage.
                const auto refParent = meta[ref]["parent"];
                if (j.parent < 0) {
                    CHECK(refParent.is_null());
                } else {
                    CHECK(f.joints[static_cast<size_t>(j.parent)].name ==
                          refParent.get<std::string>());
                }

                // Channels, in order.
                const auto refChannels = meta[ref]["channels"];
                REQUIRE(refChannels.size() == j.channels.size());
            }

            worstOffset = std::max({worstOffset, std::abs(j.offset.x - offsets[ref * 3 + 0]),
                                    std::abs(j.offset.y - offsets[ref * 3 + 1]),
                                    std::abs(j.offset.z - offsets[ref * 3 + 2])});
            worstPos    = std::max({worstPos, std::abs(j.position.x - positions[ref * 3 + 0]),
                                    std::abs(j.position.y - positions[ref * 3 + 1]),
                                    std::abs(j.position.z - positions[ref * 3 + 2])});
        }

        INFO("worst offset delta " << worstOffset << ", worst position delta " << worstPos);
        CHECK(worstOffset < 1e-5F);
        CHECK(worstPos < 1e-5F);
        CHECK(matched + endMatched == f.jointCount());
    }
}

// The rotation order is where the Z-up fix is easiest to get wrong: the file's
// "Xrotation Yrotation Zrotation" means "szyx" normally, but the axis swap
// remaps it to "syzx". Both files use the latter.
TEST_CASE("rotation orders match the reference", "[bvh][golden][parity]") {
    for (const auto& s : kSources) {
        CAPTURE(s.path);
        const auto f    = load(s);
        const auto meta = jointMeta(s.stem);

        std::map<std::string, std::string> refOrder;
        for (const auto& m : meta) {
            if (m["isEndConnector"].get<bool>()) continue;
            refOrder[m["name"].get<std::string>()] = m["rotOrder"].get<std::string>();
        }

        size_t checked = 0;
        for (const auto& j : f.joints) {
            if (j.endSite) continue;
            CAPTURE(j.name);
            const auto it = refOrder.find(j.name);
            REQUIRE(it != refOrder.end());

            if (j.hasRotation) {
                CHECK(foundation::eulerOrderName(j.rotationOrder) == it->second);
                ++checked;
            } else {
                // The reference leaves "s" for a joint with no usable rotation.
                CHECK(it->second == "s");
            }
        }
        CHECK(checked > 100);  // the bulk of the rig really is being compared
    }
}

// The per-frame matrices: everything above feeding into the actual result.
TEST_CASE("per-frame pose matrices match the reference", "[bvh][golden][parity]") {
    for (const auto& s : kSources) {
        CAPTURE(s.path);
        const auto f    = load(s);
        const auto meta = jointMeta(s.stem);
        const auto mats = readFloats(fixtureDir() / (std::string(s.stem) + "_matrices.bin"));
        // (joints, frames, 3, 4)
        REQUIRE(mats.size() == f.jointCount() * s.frames * 12);

        std::map<std::string, size_t> refByName;
        std::map<std::string, size_t> refEndByParent;
        for (size_t i = 0; i < meta.size(); ++i) {
            const auto name = meta[i]["name"].get<std::string>();
            if (meta[i]["isEndConnector"].get<bool>() && name == "End effector") {
                const auto p = meta[i]["parent"];
                if (!p.is_null()) refEndByParent[p.get<std::string>()] = i;
            } else {
                refByName[name] = i;
            }
        }

        float worst     = 0.0F;
        size_t compared = 0;
        for (const auto& j : f.joints) {
            CAPTURE(j.name);
            size_t ref = 0;
            if (j.endSite) {
                const auto it = refEndByParent.find(f.joints[static_cast<size_t>(j.parent)].name);
                if (it == refEndByParent.end()) continue;
                ref = it->second;
            } else {
                ref = refByName.at(j.name);
            }

            for (size_t fr = 0; fr < s.frames; ++fr) {
                const float* want = &mats[(ref * s.frames + fr) * 12];
                for (size_t r = 0; r < 3; ++r) {
                    for (size_t c = 0; c < 4; ++c) {
                        worst = std::max(worst, std::abs(j.frames[fr].m[r][c] - want[r * 4 + c]));
                    }
                }
                ++compared;
            }
        }
        INFO("worst matrix delta " << worst << " over " << compared << " joint-frames");
        CHECK(worst < 1e-5F);
        CHECK(compared > 200);
    }
}

// Only the root may translate by default; a limb that translates detaches from
// its parent.
TEST_CASE("translation is confined to the root by default", "[bvh][parity]") {
    const auto f = load(kSources[0]);

    size_t translating = 0;
    for (const auto& j : f.joints) {
        for (const auto& m : j.frames) {
            if (std::abs(m.m[0][3]) > 1e-6F || std::abs(m.m[1][3]) > 1e-6F ||
                std::abs(m.m[2][3]) > 1e-6F) {
                CAPTURE(j.name);
                CHECK(j.parent < 0);
                ++translating;
                break;
            }
        }
    }
    CHECK(translating >= 1);  // the root really does carry a translation
}

TEST_CASE("a truncated BVH is reported, not crashed on", "[bvh]") {
    const auto p = std::filesystem::temp_directory_path() / "mh_trunc.bvh";
    {
        std::ofstream out(p);
        out << "HIERARCHY\nROOT root\n{\nOFFSET 0 0 0\n"
               "CHANNELS 3 Xrotation Yrotation Zrotation\n}\n"
               "MOTION\nFrames: 4\nFrame Time: 0.04\n0 0 0\n";  // 1 frame, not 4
    }
    const auto r = io::readBvh(p);
    REQUIRE_FALSE(r.has_value());
    CHECK(r.error().kind == io::BvhErrorKind::FrameDataMismatch);

    std::error_code ec;
    std::filesystem::remove(p, ec);
}

TEST_CASE("unbalanced braces are reported", "[bvh]") {
    const auto p = std::filesystem::temp_directory_path() / "mh_brace.bvh";
    {
        std::ofstream out(p);
        out << "HIERARCHY\nROOT root\n{\nOFFSET 0 0 0\n"
               "CHANNELS 3 Xrotation Yrotation Zrotation\n}\n}\nMOTION\n";
    }
    const auto r = io::readBvh(p);
    REQUIRE_FALSE(r.has_value());
    CHECK(r.error().kind == io::BvhErrorKind::Malformed);

    std::error_code ec;
    std::filesystem::remove(p, ec);
}

// --- BVH export -------------------------------------------------------------
//
// The property that matters is read -> write -> read stability: the second read
// must reproduce the first exactly enough that a pose survives the trip. This
// is a stronger check than comparing text, which would only pin our own
// formatting.
//
// Note what round-tripping does NOT preserve: a Z-up source comes back as the
// equivalent Y-up file. readBvh converts on the way in (both shipped MakeHuman
// pose files measure as Z-up), so Y-up is what was actually parsed, and writing
// the original channel names against converted data would re-read with a
// different Euler order. See BvhWriter.h.
namespace {

std::filesystem::path roundTripPath(std::string_view stem) {
    return std::filesystem::temp_directory_path() /
           (std::string("mh_bvh_rt_") + std::string(stem) + ".bvh");
}

void checkRoundTrip(const std::filesystem::path& source, float tolerance) {
    if (!std::filesystem::exists(source)) return;

    const auto first = io::readBvh(source);
    REQUIRE(first.has_value());
    REQUIRE(first->jointCount() > 0);

    const auto out   = roundTripPath(source.stem().string());
    const auto wrote = io::writeBvh(out, *first);
    REQUIRE(wrote.has_value());
    REQUIRE(std::filesystem::exists(out));

    const auto second = io::readBvh(out);
    REQUIRE(second.has_value());

    INFO("round-tripping " << source.filename().string());
    REQUIRE(second->jointCount() == first->jointCount());
    REQUIRE(second->frameCount == first->frameCount);
    CHECK(std::abs(second->frameTime - first->frameTime) < 1e-9);

    for (size_t j = 0; j < first->jointCount(); ++j) {
        const auto& a = first->joints[j];
        const auto& b = second->joints[j];
        INFO("joint " << j << " " << a.name);
        CHECK(b.name == a.name);
        CHECK(b.parent == a.parent);
        CHECK(b.endSite == a.endSite);
        CHECK(std::abs(b.offset.x - a.offset.x) < tolerance);
        CHECK(std::abs(b.offset.y - a.offset.y) < tolerance);
        CHECK(std::abs(b.offset.z - a.offset.z) < tolerance);
        REQUIRE(b.frames.size() == a.frames.size());
        for (size_t f = 0; f < a.frames.size(); ++f) {
            for (size_t r = 0; r < 4; ++r)
                for (size_t c = 0; c < 4; ++c) {
                    INFO("frame " << f << " element [" << r << "][" << c << "]");
                    CHECK(std::abs(b.frames[f].m[r][c] - a.frames[f].m[r][c]) < tolerance);
                }
        }
    }
    std::filesystem::remove(out);
}

}  // namespace

TEST_CASE("a single-frame pose survives a BVH round trip", "[io][bvh][write]") {
    checkRoundTrip(std::filesystem::path(MH_DATA_DIR) / "poses" / "tpose.bvh", 1e-4F);
}

TEST_CASE("the 60-frame face pose units survive a BVH round trip", "[io][bvh][write]") {
    // 212 joints x 60 frames: every joint, every frame, every matrix element.
    checkRoundTrip(std::filesystem::path(MH_DATA_DIR) / "poseunits" / "face-poseunits.bvh", 1e-4F);
}

TEST_CASE("writeBvh reports a path it cannot open", "[io][bvh][write]") {
    io::BvhFile bvh;
    bvh.joints.push_back(io::BvhJoint{.name = "root"});
    bvh.frameCount = 0;
    const auto r   = io::writeBvh("/definitely/not/a/directory/x.bvh", bvh);
    REQUIRE_FALSE(r.has_value());
    CHECK(r.error().kind == io::BvhErrorKind::Unreadable);
}

// Writing is idempotent in MEANING, not in bytes. Generation 2 differs from
// generation 1 by 7 bytes out of 340,964: the angles pass through a float32
// Mat4 between generations, so their low-order digits shift and exact text
// reproduction is unattainable. Byte-identity was the claim I first wrote and
// it was simply wrong.
//
// What does hold, and is what matters: the two generations decode to the same
// transforms. A third party agrees -- Blender imports generation 1 and 2 to the
// same pose (worst delta 0.0 across all 163 bones at frame 30, measured), while
// it puts generation 1 11.19 away from the Z-up original. That gap is the
// conversion readBvh performs on input, not an export error: standard BVH is
// conventionally Y-up, so the written file is the more conformant of the two.
TEST_CASE("writing a BVH is idempotent in meaning", "[io][bvh][write]") {
    const auto source = std::filesystem::path(MH_DATA_DIR) / "poseunits" / "face-poseunits.bvh";
    if (!std::filesystem::exists(source)) return;

    const auto gen1 = roundTripPath("gen1");
    const auto gen2 = roundTripPath("gen2");

    const auto first = io::readBvh(source);
    REQUIRE(first.has_value());
    REQUIRE(io::writeBvh(gen1, *first).has_value());

    const auto readA = io::readBvh(gen1);
    REQUIRE(readA.has_value());
    CHECK_FALSE(readA->convertedFromZUp);  // already Y-up; nothing to convert
    REQUIRE(io::writeBvh(gen2, *readA).has_value());

    const auto readB = io::readBvh(gen2);
    REQUIRE(readB.has_value());
    REQUIRE(readB->jointCount() == readA->jointCount());

    float worst = 0.0F;
    for (size_t j = 0; j < readA->jointCount(); ++j) {
        REQUIRE(readB->joints[j].frames.size() == readA->joints[j].frames.size());
        for (size_t f = 0; f < readA->joints[j].frames.size(); ++f)
            for (size_t r = 0; r < 4; ++r)
                for (size_t c = 0; c < 4; ++c)
                    worst = std::max(worst, std::abs(readB->joints[j].frames[f].m[r][c] -
                                                     readA->joints[j].frames[f].m[r][c]));
    }
    INFO("worst second-generation drift " << worst);
    CHECK(worst < 1e-5F);

    std::filesystem::remove(gen1);
    std::filesystem::remove(gen2);
}
