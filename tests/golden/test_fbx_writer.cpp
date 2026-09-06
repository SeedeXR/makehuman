// SPDX-License-Identifier: Apache-2.0
//
// Our own FBX binary writer, stage 1: the container.
//
// Written from the published record layout and from reading files Maya's FBX
// SDK and assimp produce -- `tools/fbxdump.py` is the reader used to study
// them. No FBX SDK is linked and no GPL exporter is translated; see
// LICENSING.md 5.1, which already records both tools as validator-only.
//
// These tests check the BYTES, because a structurally invalid FBX is the
// failure that a writer cannot detect about itself. The question "does a real
// DCC open it" is answered by Maya and Blender in
// tools/run_{maya,blender}_validation.sh -- our own reader agreeing with our
// own writer would prove nothing.

#include "makehuman/core/Mesh.h"
#include "makehuman/core/RenderMesh.h"
#include "makehuman/io/FbxWriter.h"

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

using namespace mh;

namespace {

std::vector<uint8_t> readAll(const std::filesystem::path& p) {
    std::ifstream in(p, std::ios::binary | std::ios::ate);
    if (!in) return {};
    const auto n = static_cast<size_t>(in.tellg());
    in.seekg(0);
    std::vector<uint8_t> out(n);
    in.read(reinterpret_cast<char*>(out.data()), static_cast<std::streamsize>(n));
    return out;
}

uint32_t u32At(const std::vector<uint8_t>& b, size_t off) {
    uint32_t v{};
    std::memcpy(&v, b.data() + off, sizeof(v));
    return v;
}

uint64_t u64At(const std::vector<uint8_t>& b, size_t off) {
    uint64_t v{};
    std::memcpy(&v, b.data() + off, sizeof(v));
    return v;
}

std::filesystem::path tempFbx(const char* stem) {
    return std::filesystem::temp_directory_path() / (std::string("mh_fbx_") + stem + ".fbx");
}

core::Mesh quad() {
    core::Mesh m("quad", 4);
    REQUIRE(m.setCoords({{0, 0, 0}, {2, 0, 0}, {2, 0, 3}, {0, 0, 3}}).has_value());
    m.addFaceGroup("g");
    REQUIRE(m.setFaces({0, 1, 2, 3}, {}, {0}).has_value());
    m.buildAdjacency();
    m.calcNormals();
    return m;
}

/// Walks the record tree, returning the top-level node names in order.
///
/// A deliberately strict walk: every record's EndOffset must land exactly on
/// the next record, which is the invariant a hand-written writer gets wrong
/// first and which no amount of valid-looking content makes up for.
std::vector<std::string> topLevelNames(const std::vector<uint8_t>& b) {
    std::vector<std::string> names;
    size_t o = 27;  // 21 magic + 2 unknown + 4 version
    while (o + 25 <= b.size()) {
        const uint64_t end = u64At(b, o);
        if (end == 0) break;  // the NULL terminator record
        REQUIRE(end > o);
        REQUIRE(end <= b.size());
        const uint8_t nameLen = b[o + 24];
        names.emplace_back(reinterpret_cast<const char*>(b.data()) + o + 25, nameLen);
        o = end;
    }
    return names;
}

}  // namespace

TEST_CASE("the FBX header is what a reader looks for", "[io][fbx]") {
    const auto out = tempFbx("header");
    const auto m   = quad();
    REQUIRE(io::writeFbx(out, core::RenderMesh::build(m).view()).has_value());

    const auto b = readAll(out);
    REQUIRE(b.size() > 200);
    CHECK(std::memcmp(b.data(), "Kaydara FBX Binary  \0", 21) == 0);
    // The two bytes after the magic are 0x1A 0x00 in every file both Maya and
    // assimp write.
    CHECK(b[21] == 0x1A);
    CHECK(b[22] == 0x00);
    // 7500 or later, because that is where the record header's three counts
    // became 64-bit. Writing 7400's 32-bit layout under a 7500 version number
    // desynchronises a reader on the very first node.
    CHECK(u32At(b, 23) >= 7500);

    std::error_code ec;
    std::filesystem::remove(out, ec);
}

TEST_CASE("the footer is the shape both Maya and assimp write", "[io][fbx]") {
    // Measured from files those two produce, not recalled: the last 16 bytes
    // are a constant magic, preceded by 120 zeros, the version, and 4 zeros --
    // and that whole 144-byte tail BEGINS on a 16-byte boundary, which is what
    // the padding before it exists to arrange. Aligning the version itself
    // instead, which is the natural misreading, puts the tail four bytes out.
    const auto out = tempFbx("footer");
    const auto m   = quad();
    REQUIRE(io::writeFbx(out, core::RenderMesh::build(m).view()).has_value());

    const auto b                        = readAll(out);
    static constexpr uint8_t kMagic[16] = {0xF8, 0x5A, 0x8C, 0x6A, 0xDE, 0xF5, 0xD9, 0x7E,
                                           0xEC, 0xE9, 0x0C, 0xE3, 0x75, 0x8F, 0x29, 0x0B};
    REQUIRE(b.size() > 200);
    CHECK(std::memcmp(b.data() + b.size() - 16, kMagic, 16) == 0);

    bool zeros = true;
    for (size_t i = b.size() - 136; i < b.size() - 16; ++i)
        zeros = zeros && b[i] == 0;
    CHECK(zeros);

    CHECK(u32At(b, b.size() - 140) == u32At(b, 23));  // the version, again
    CHECK(u32At(b, b.size() - 144) == 0);
    CHECK((b.size() - 144) % 16 == 0);

    std::error_code ec;
    std::filesystem::remove(out, ec);
}

TEST_CASE("the top-level records are the ones a reader requires", "[io][fbx]") {
    const auto out = tempFbx("records");
    const auto m   = quad();
    REQUIRE(io::writeFbx(out, core::RenderMesh::build(m).view()).has_value());

    const auto names = topLevelNames(readAll(out));
    // Order matters to some readers and costs nothing to keep, so it is
    // asserted rather than merely the set. Maya writes exactly this sequence.
    CHECK(names == std::vector<std::string>{"FBXHeaderExtension", "FileId", "CreationTime",
                                            "Creator", "GlobalSettings", "Documents", "References",
                                            "Definitions", "Objects", "Connections"});

    std::error_code ec;
    std::filesystem::remove(out, ec);
}

TEST_CASE("the mesh reaches the file", "[io][fbx]") {
    const auto out = tempFbx("mesh");
    const auto m   = quad();
    const auto r   = io::writeFbx(out, core::RenderMesh::build(m).view());
    REQUIRE(r.has_value());
    CHECK(r->vertices == 4);
    // Two, not one: a RenderView is a triangle list, so the quad arrived here
    // already split. The writer reports what it wrote.
    CHECK(r->polygons == 2);

    const auto b = readAll(out);
    const std::string blob(reinterpret_cast<const char*>(b.data()), b.size());
    // The names an importer keys on. Their absence is the difference between a
    // file that loads empty and one that loads.
    CHECK(blob.find("Vertices") != std::string::npos);
    CHECK(blob.find("PolygonVertexIndex") != std::string::npos);
    CHECK(blob.find("Geometry") != std::string::npos);
    CHECK(blob.find("Connections") != std::string::npos);

    std::error_code ec;
    std::filesystem::remove(out, ec);
}

TEST_CASE("an empty mesh is refused rather than written", "[io][fbx]") {
    // A zero-vertex FBX parses fine and imports as nothing, which is the worst
    // of both: no error and no model.
    const core::Mesh empty("empty", 4);
    const auto out = tempFbx("empty");
    const auto r   = io::writeFbx(out, core::RenderMesh::build(empty).view());
    REQUIRE_FALSE(r.has_value());
    CHECK(r.error().kind == io::FbxWriteErrorKind::EmptyMesh);
}
