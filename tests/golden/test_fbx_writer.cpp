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

/// Recursively checks that every record in [@p from, @p to) is well formed.
///
/// A record's content is its header, name and properties; everything after that
/// and before its EndOffset is its child list, which must end with a 25-byte
/// NULL record. A file can be wrong here and still walk correctly at the TOP
/// level -- each record's own EndOffset keeps the outer loop in step -- which
/// is exactly how a missing terminator went unnoticed while Maya imported
/// nothing.
void checkRecords(const std::vector<uint8_t>& b, size_t from, size_t to) {
    size_t o = from;
    while (o + 25 <= to) {
        const uint64_t end     = u64At(b, o);
        const uint64_t nProps  = u64At(b, o + 8);
        const uint64_t propLen = u64At(b, o + 16);
        if (end == 0) {
            // A NULL record ends the list, and must end it exactly.
            REQUIRE(o + 25 == to);
            return;
        }
        REQUIRE(end > o);
        REQUIRE(end <= to);
        const size_t nameLen = b[o + 24];
        const size_t content = o + 25 + nameLen + propLen;
        REQUIRE(content <= end);
        if (content < end) {
            // Everything after the properties is children, terminator included.
            checkRecords(b, content, end);
        } else {
            // No child region at all, which is only allowed when the record
            // carries properties: an empty record still needs its terminator.
            CHECK(nProps > 0);
        }
        o = end;
    }
    REQUIRE(o == to);
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

    // Again for a mesh of a DIFFERENT size. Aligning the wrong field happens to
    // give the right answer whenever the unpadded length lands conveniently,
    // and one file cannot tell the two rules apart.
    const auto other = tempFbx("footer2");
    core::Mesh tri("tri", 3);
    REQUIRE(tri.setCoords({{0, 0, 0}, {1, 0, 0}, {0, 1, 0}}).has_value());
    tri.addFaceGroup("g");
    REQUIRE(tri.setFaces({0, 1, 2}, {}, {0}).has_value());
    tri.buildAdjacency();
    tri.calcNormals();
    REQUIRE(io::writeFbx(other, core::RenderMesh::build(tri).view()).has_value());
    const auto b2 = readAll(other);
    CHECK((b2.size() - 144) % 16 == 0);
    CHECK(std::memcmp(b2.data() + b2.size() - 16, kMagic, 16) == 0);

    std::error_code ec;
    std::filesystem::remove(out, ec);
    std::filesystem::remove(other, ec);
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

TEST_CASE("every record is terminated exactly as the format requires", "[io][fbx]") {
    const auto out = tempFbx("records_deep");
    const auto m   = quad();
    REQUIRE(io::writeFbx(out, core::RenderMesh::build(m).view()).has_value());

    const auto b = readAll(out);
    // The top-level list runs from the header to the footer, and the footer is
    // the last 144 bytes plus whatever padding aligned them.
    size_t topEnd = b.size() - 144;
    while (topEnd > 27 && b[topEnd - 1] == 0)
        --topEnd;
    // Back up over the 16-byte footer id, which is not a record.
    REQUIRE(topEnd > 27 + 16);
    checkRecords(b, 27, topEnd - 16);

    std::error_code ec;
    std::filesystem::remove(out, ec);
}

TEST_CASE("the records Maya refuses the file without are present", "[io][fbx]") {
    // Every name here was learned by Maya rejecting a file that lacked it --
    // importing ZERO meshes, with nothing in its own SDK log. Blender opened
    // each of those same files correctly, so none of this is reachable from one
    // reader alone.
    const auto out = tempFbx("required");
    const auto m   = quad();
    REQUIRE(io::writeFbx(out, core::RenderMesh::build(m).view()).has_value());

    const auto b = readAll(out);
    const std::string blob(reinterpret_cast<const char*>(b.data()), b.size());
    for (const char* needed : {// Spelled in FULL. `DefaultAttributeIn` -- 18 characters, which is
                               // where my own debug dump truncated it -- costs the whole mesh.
                               "DefaultAttributeIndex",
                               // A Model whose Properties70 is empty is discarded.
                               "RotationActive", "InheritType",
                               // A Geometry with no material layer is discarded.
                               "LayerElementMaterial", "Materials",
                               // ... and the material it names has to exist.
                               "Material",
                               // The scene root the objects hang from.
                               "RootNode"}) {
        INFO(needed);
        CHECK(blob.find(needed) != std::string::npos);
    }

    std::error_code ec;
    std::filesystem::remove(out, ec);
}

TEST_CASE("each polygon is closed by a negated index", "[io][fbx]") {
    // FBX has no per-face vertex count: the LAST index of every polygon is
    // stored as `~i`, and that is the only thing separating one face from the
    // next. Without it a reader sees a single polygon spanning the whole mesh,
    // which loads without complaint and looks like a shattered model.
    const auto out = tempFbx("polygons");
    const auto m   = quad();
    REQUIRE(io::writeFbx(out, core::RenderMesh::build(m).view()).has_value());

    const auto b = readAll(out);
    // The array property is `i` followed by count, encoding, byte length.
    const std::string blob(reinterpret_cast<const char*>(b.data()), b.size());
    const size_t at = blob.find("PolygonVertexIndex");
    REQUIRE(at != std::string::npos);
    size_t o = at + std::strlen("PolygonVertexIndex");
    REQUIRE(b[o] == 'i');
    const uint32_t count = u32At(b, o + 1);
    REQUIRE(u32At(b, o + 5) == 0);  // stored raw, not deflated
    o += 13;

    REQUIRE(count == 6);  // one quad -> two triangles -> six corners
    size_t negatives = 0;
    for (uint32_t i = 0; i < count; ++i) {
        const auto v           = static_cast<int32_t>(u32At(b, o + (i * 4)));
        const bool endsPolygon = (i % 3) == 2;
        INFO("index " << i << " = " << v);
        CHECK((v < 0) == endsPolygon);
        negatives += static_cast<size_t>(v < 0);
    }
    CHECK(negatives == 2);

    std::error_code ec;
    std::filesystem::remove(out, ec);
}

TEST_CASE("UVs are written, and NOT flipped", "[io][fbx]") {
    // FBX's UV origin is the LOWER-left, the same as OBJ's -- so V passes
    // through untouched. glTF is the odd one out and flips (session 150 checked
    // all four formats against each other in Blender), and applying that flip
    // here would mirror every texture vertically.
    const auto out = tempFbx("uv");
    core::Mesh m("quad", 4);
    REQUIRE(m.setCoords({{0, 0, 0}, {2, 0, 0}, {2, 0, 3}, {0, 0, 3}}).has_value());
    // 0.25 and 0.60, NOT 0.25 and 0.75. The obvious pair is symmetric about
    // 0.5, so flipping V maps the set onto itself and a membership check passes
    // either way -- which is exactly what happened: the flip mutation survived
    // the first version of this test, and the comment then claimed it could
    // not. 1 - 0.25 = 0.75 and 1 - 0.60 = 0.40, neither of which is a source
    // value.
    REQUIRE(m.setUVs({{0.0F, 0.25F}, {1.0F, 0.25F}, {1.0F, 0.60F}, {0.0F, 0.60F}}).has_value());
    m.addFaceGroup("g");
    REQUIRE(m.setFaces({0, 1, 2, 3}, {0, 1, 2, 3}, {0}).has_value());
    m.buildAdjacency();
    m.calcNormals();

    const auto rm = core::RenderMesh::build(m);
    REQUIRE(io::writeFbx(out, rm.view()).has_value());

    const auto b = readAll(out);
    const std::string blob(reinterpret_cast<const char*>(b.data()), b.size());
    REQUIRE(blob.find("LayerElementUV") != std::string::npos);
    // IndexToDirect, like both Maya and assimp: our per-vertex UVs ARE the
    // direct array and the polygon list is already the index into it, so this
    // form costs nothing and stores each UV once.
    REQUIRE(blob.find("IndexToDirect") != std::string::npos);
    REQUIRE(blob.find("UVIndex") != std::string::npos);

    const auto src = rm.view().texco;
    REQUIRE(src.size() == 4);
    const auto bytesOf = [](double v) {
        std::string bytes(sizeof(double), '\0');
        std::memcpy(bytes.data(), &v, sizeof(v));
        return bytes;
    };
    for (const auto& uv : src) {
        const auto v = static_cast<double>(uv.y);
        INFO("source V " << v);
        // Present unflipped ...
        CHECK(blob.find(bytesOf(v)) != std::string::npos);
        // ... and its flip absent, which is the half that catches the mistake.
        CHECK(blob.find(bytesOf(1.0 - v)) == std::string::npos);
    }

    std::error_code ec;
    std::filesystem::remove(out, ec);
}

TEST_CASE("the material carries the description's colours", "[io][fbx]") {
    const auto out = tempFbx("material");
    const auto m   = quad();
    foundation::MaterialDesc desc;
    desc.name      = "Skin";
    desc.diffuse   = {0.76F, 0.62F, 0.53F};
    desc.specular  = {0.1F, 0.2F, 0.3F};
    desc.shininess = 0.25F;
    REQUIRE(io::writeFbx(out, core::RenderMesh::build(m).view(), {}, &desc).has_value());

    const auto b = readAll(out);
    const std::string blob(reinterpret_cast<const char*>(b.data()), b.size());
    for (const char* p : {"DiffuseColor", "SpecularColor", "ShininessExponent"}) {
        INFO(p);
        CHECK(blob.find(p) != std::string::npos);
    }
    // The VALUE, not just the property name: a writer that emitted the names
    // with defaults behind them would pass the check above.
    const double red = static_cast<double>(desc.diffuse.x);
    std::string bytes(sizeof(double), '\0');
    std::memcpy(bytes.data(), &red, sizeof(red));
    CHECK(blob.find(bytes) != std::string::npos);

    std::error_code ec;
    std::filesystem::remove(out, ec);
}

TEST_CASE("a diffuse texture becomes a Texture and a Video", "[io][fbx]") {
    // Read out of Maya's own output: a file texture is TWO objects -- a Video
    // holding the path and a Texture referring to it by name -- wired with an
    // `OP` connection naming the material property it drives.
    const auto out = tempFbx("texture");
    const auto m   = quad();
    foundation::MaterialDesc desc;
    desc.diffuseTexture = "textures/skin/african_deep.png";
    REQUIRE(io::writeFbx(out, core::RenderMesh::build(m).view(), {}, &desc).has_value());

    const auto b = readAll(out);
    const std::string blob(reinterpret_cast<const char*>(b.data()), b.size());
    CHECK(blob.find("TextureVideoClip") != std::string::npos);
    CHECK(blob.find("RelativeFilename") != std::string::npos);
    CHECK(blob.find("african_deep.png") != std::string::npos);
    // The connection that makes it a DIFFUSE texture rather than an orphan.
    CHECK(blob.find("OP") != std::string::npos);

    std::error_code ec;
    std::filesystem::remove(out, ec);
}

TEST_CASE("no texture means no Texture object", "[io][fbx]") {
    // An empty Texture pointing at nothing is worse than none: a DCC shows a
    // missing-file error for a texture the material never had.
    const auto out = tempFbx("notexture");
    const auto m   = quad();
    foundation::MaterialDesc desc;  // no diffuseTexture
    REQUIRE(io::writeFbx(out, core::RenderMesh::build(m).view(), {}, &desc).has_value());

    const auto b = readAll(out);
    const std::string blob(reinterpret_cast<const char*>(b.data()), b.size());
    CHECK(blob.find("TextureVideoClip") == std::string::npos);

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
