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
#include "makehuman/foundation/Transform.h"
#include "makehuman/io/FbxWriter.h"

#include <catch2/catch_test_macros.hpp>

#include <array>
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

namespace {

/// A two-joint skin over the quad, with the child MOVED by the pose.
///
/// Rest and pose deliberately differ: that difference is the whole feature, and
/// a writer that collapsed them would still produce a valid file -- just a
/// statue, which is what assimp's FBX writer produces and why this one exists.
struct SkinnedQuad {
    std::vector<std::string> names{"root", "tip"};
    std::vector<int32_t> parents{-1, 0};
    std::vector<foundation::Mat4> rest;
    std::vector<foundation::Mat4> pose;
    std::vector<uint32_t> joints;
    std::vector<float> weights;

    SkinnedQuad() {
        auto childRest    = foundation::Mat4::identity();
        childRest.m[1][3] = 2.0F;
        rest              = {foundation::Mat4::identity(), childRest};
        auto childPose    = childRest;
        // The pose moves the child in x AND y. Moving it in x alone leaves the
        // rest and posed y equal, so a writer that put the POSED matrix into
        // TransformLink would still emit the rest y and pass -- which is
        // exactly what the first version of this fixture allowed.
        childPose.m[0][3] = 5.0F;
        childPose.m[1][3] = 3.0F;
        pose              = {foundation::Mat4::identity(), childPose};
    }

    [[nodiscard]] foundation::SkinView view(size_t vertices) {
        joints.assign(vertices * 4, 0);
        weights.assign(vertices * 4, 0.0F);
        for (size_t i = 0; i < vertices; ++i)
            weights[i * 4] = 1.0F;
        return foundation::SkinView{.jointNames   = names,
                                    .jointParents = parents,
                                    .globalRest   = rest,
                                    .globalPose   = pose,
                                    .joints       = joints,
                                    .weights      = weights,
                                    .influences   = 4};
    }
};

}  // namespace

TEST_CASE("a skinned mesh carries joints, clusters and a bind pose", "[io][fbx][skin]") {
    const auto out = tempFbx("skin");
    const auto m   = quad();
    const auto rm  = core::RenderMesh::build(m);
    SkinnedQuad sk;
    const auto skin = sk.view(rm.view().vertexCount());
    REQUIRE(skin.valid());
    REQUIRE(io::writeFbx(out, rm.view(), {}, nullptr, &skin).has_value());

    const auto b = readAll(out);
    const std::string blob(reinterpret_cast<const char*>(b.data()), b.size());
    for (const char* needed : {// A joint is a Model of subtype LimbNode plus a NodeAttribute
                               // flagged as a Skeleton; without the flag Maya imports transforms
                               // rather than a rig.
                               "LimbNode", "TypeFlags", "Skeleton",
                               // The deformer and its per-joint clusters.
                               "Deformer", "Cluster", "Indexes", "Weights", "TransformLink",
                               // The bind pose, which is what assimp omits -- Maya then computes
                               // one from the CURRENT pose and the rig arrives baked.
                               "BindPose", "NbPoseNodes", "PoseNode"}) {
        INFO(needed);
        CHECK(blob.find(needed) != std::string::npos);
    }
    // Both joints, by name.
    CHECK(blob.find("root") != std::string::npos);
    CHECK(blob.find("tip") != std::string::npos);

    std::error_code ec;
    std::filesystem::remove(out, ec);
}

TEST_CASE("TransformLink is the BIND pose, not the current one", "[io][fbx][skin]") {
    // The single most important number in the file. `TransformLink` is the
    // joint's global at BIND time while the joint's Model node carries the
    // CURRENT transform; a consumer computes pose * inverse(bind) from the two.
    // Write the same matrix into both and the deformation is the identity --
    // a rig that exists, deforms nothing, and looks correct in a screenshot.
    const auto out = tempFbx("bind");
    // A TINY quad, spanning 0..0.7 -- so at the default centimetre scale its
    // coordinates are 0..7 and cannot collide with the rig's numbers below.
    // The ordinary quad spans 0..3, which scales to 0, 20 and 30 -- exactly the
    // values this test looks for, and it passed the bind/pose mutations by
    // finding the MESH's coordinates instead of the rig's.
    core::Mesh m("tiny", 4);
    REQUIRE(m.setCoords({{0, 0, 0}, {0.7F, 0, 0}, {0.7F, 0, 0.7F}, {0, 0, 0.7F}}).has_value());
    m.addFaceGroup("g");
    REQUIRE(m.setFaces({0, 1, 2, 3}, {}, {0}).has_value());
    m.buildAdjacency();
    m.calcNormals();
    const auto rm = core::RenderMesh::build(m);
    SkinnedQuad sk;
    const auto skin = sk.view(rm.view().vertexCount());
    REQUIRE(io::writeFbx(out, rm.view(), {}, nullptr, &skin).has_value());

    const auto b = readAll(out);
    const std::string blob(reinterpret_cast<const char*>(b.data()), b.size());
    const auto bytesOf = [](double v) {
        std::string bytes(sizeof(double), '\0');
        std::memcpy(bytes.data(), &v, sizeof(v));
        return bytes;
    };
    // The child's REST y is 2 and its POSED position is (5, 3), in MakeHuman's
    // decimetres, and the default unit is the centimetre. So the file must
    // contain 20 (the bind y, in a TransformLink) AND 50 and 30 (the posed
    // placement, on the joint's node). Collapsing the two -- writing either
    // matrix into both places -- loses one of these numbers.
    CHECK(blob.find(bytesOf(20.0)) != std::string::npos);
    CHECK(blob.find(bytesOf(50.0)) != std::string::npos);
    CHECK(blob.find(bytesOf(30.0)) != std::string::npos);

    // And the cluster's `Transform` is the INVERSE bind, not identity. Read out
    // of Maya's own numbers: for a joint at (0, -1, 0) it writes TransformLink
    // (0, -1, 0) and Transform (0, +1, 0). Identity there instead displaces
    // every vertex by its joint's bind position -- measured on an UNPOSED rig,
    // whose deformation must be exactly the identity, as Maya evaluating the
    // body to 247 x 334 x 269 cm instead of leaving it at 105 x 166 x 43.
    CHECK(blob.find(bytesOf(-20.0)) != std::string::npos);

    std::error_code ec;
    std::filesystem::remove(out, ec);
}

TEST_CASE("no skin means no deformer", "[io][fbx][skin]") {
    // A Deformer with no clusters is a rig that binds nothing, and a DCC shows
    // it as a skeleton the mesh ignores.
    const auto out = tempFbx("noskin");
    const auto m   = quad();
    REQUIRE(io::writeFbx(out, core::RenderMesh::build(m).view()).has_value());
    const auto b = readAll(out);
    const std::string blob(reinterpret_cast<const char*>(b.data()), b.size());
    CHECK(blob.find("BindPose") == std::string::npos);
    CHECK(blob.find("LimbNode") == std::string::npos);

    std::error_code ec;
    std::filesystem::remove(out, ec);
}

TEST_CASE("blend shapes become a BlendShape deformer", "[io][fbx][morph]") {
    // Read out of Maya's own output: three objects per target -- a
    // `Geometry`/`Shape` holding the deltas, a `Deformer`/`BlendShapeChannel`
    // holding the weight, and one `Deformer`/`BlendShape` on the geometry that
    // owns the channels.
    const auto out = tempFbx("morph");
    const auto m   = quad();
    const auto rm  = core::RenderMesh::build(m);

    std::vector<foundation::Vec3> deltas(rm.view().vertexCount(), foundation::Vec3{0, 0, 0});
    deltas[1] = {0.0F, 0.4F, 0.0F};
    const std::array<foundation::MorphTarget, 1> targets{
        foundation::MorphTarget{"nose-base-up", deltas}};
    REQUIRE(io::writeFbx(out, rm.view(), {}, nullptr, nullptr, targets).has_value());

    const auto b = readAll(out);
    const std::string blob(reinterpret_cast<const char*>(b.data()), b.size());
    for (const char* needed :
         {"BlendShape", "BlendShapeChannel", "Shape", "DeformPercent", "FullWeights",
          // The target's name has to survive: a DCC lists blend shapes by name
          // and an unnamed one is unusable however correct its deltas are.
          "nose-base-up"}) {
        INFO(needed);
        CHECK(blob.find(needed) != std::string::npos);
    }

    std::error_code ec;
    std::filesystem::remove(out, ec);
}

TEST_CASE("a blend shape stores only the vertices it moves", "[io][fbx][morph]") {
    // SPARSE, like Maya's own: the shape carries an `Indexes` array and only the
    // deltas for those vertices. The expression targets move a few hundred
    // vertices of 21,833, so a dense shape would be two orders of magnitude of
    // zeros -- and 34 of them.
    const auto out = tempFbx("morph_sparse");
    const auto m   = quad();
    const auto rm  = core::RenderMesh::build(m);

    std::vector<foundation::Vec3> deltas(rm.view().vertexCount(), foundation::Vec3{0, 0, 0});
    deltas[1] = {0.0F, 0.4F, 0.0F};  // exactly one vertex moves
    const std::array<foundation::MorphTarget, 1> targets{foundation::MorphTarget{"one", deltas}};
    REQUIRE(io::writeFbx(out, rm.view(), {}, nullptr, nullptr, targets).has_value());

    const auto b = readAll(out);
    const std::string blob(reinterpret_cast<const char*>(b.data()), b.size());
    const size_t at = blob.find("Indexes");
    REQUIRE(at != std::string::npos);
    size_t o = at + std::strlen("Indexes");
    REQUIRE(b[o] == 'i');
    // One moved vertex, not four. A dense writer would say 4 here and be
    // "correct" in every other respect.
    CHECK(u32At(b, o + 1) == 1);

    std::error_code ec;
    std::filesystem::remove(out, ec);
}

TEST_CASE("a blend shape delta is a displacement, not a point", "[io][fbx][morph]") {
    // With feetOnGround the mesh is lifted by a ground offset. A DELTA must not
    // be: it is a displacement, and adding the offset to it shifts the whole
    // body once per active target -- 34 times over on a full expression export.
    // The default is feetOnGround FALSE, so a test that leaves it alone cannot
    // see this at all, which is why the mutation survived the first pass.
    const auto out = tempFbx("morph_ground");
    core::Mesh m("raised", 4);
    // Sitting at y = 1, so levelling it moves the mesh by a known -10 cm.
    REQUIRE(m.setCoords({{0, 1, 0}, {0.7F, 1, 0}, {0.7F, 1, 0.7F}, {0, 1, 0.7F}}).has_value());
    m.addFaceGroup("g");
    REQUIRE(m.setFaces({0, 1, 2, 3}, {}, {0}).has_value());
    m.buildAdjacency();
    m.calcNormals();
    const auto rm = core::RenderMesh::build(m);

    std::vector<foundation::Vec3> deltas(rm.view().vertexCount(), foundation::Vec3{0, 0, 0});
    deltas[1] = {0.0F, 0.4F, 0.0F};
    const std::array<foundation::MorphTarget, 1> targets{foundation::MorphTarget{"lift", deltas}};
    io::FbxWriteOptions opt;
    opt.feetOnGround = true;
    REQUIRE(io::writeFbx(out, rm.view(), opt, nullptr, nullptr, targets).has_value());

    const auto b = readAll(out);
    const std::string blob(reinterpret_cast<const char*>(b.data()), b.size());
    const auto bytesOf = [](double v) {
        std::string bytes(sizeof(double), '\0');
        std::memcpy(bytes.data(), &v, sizeof(v));
        return bytes;
    };
    // 0.4 dm scaled to centimetres is 4. The offset would make it -6.
    CHECK(blob.find(bytesOf(4.0)) != std::string::npos);
    CHECK(blob.find(bytesOf(-6.0)) == std::string::npos);

    std::error_code ec;
    std::filesystem::remove(out, ec);
}

TEST_CASE("the channel is named after its target", "[io][fbx][morph]") {
    // The channel is what a DCC LISTS. Naming every channel the same thing
    // still produces a file whose shapes carry the right names -- and a blend
    // shape panel of identical entries. Checked on the channel record itself,
    // because the name also appears on the Shape geometry and a plain search
    // finds that one.
    const auto out = tempFbx("morph_named");
    const auto m   = quad();
    const auto rm  = core::RenderMesh::build(m);
    std::vector<foundation::Vec3> deltas(rm.view().vertexCount(), foundation::Vec3{0, 0, 0});
    deltas[1] = {0.0F, 0.4F, 0.0F};
    const std::array<foundation::MorphTarget, 1> targets{
        foundation::MorphTarget{"mouth-open", deltas}};
    REQUIRE(io::writeFbx(out, rm.view(), {}, nullptr, nullptr, targets).has_value());

    const auto b = readAll(out);
    const std::string blob(reinterpret_cast<const char*>(b.data()), b.size());
    // `Name\0\x01SubDeformer` is how an object of that class is spelt, so this
    // pins the name onto the CHANNEL rather than onto the shape.
    const std::string expected = std::string("mouth-open") + '\0' + '\x01' + "SubDeformer";
    CHECK(blob.find(expected) != std::string::npos);

    std::error_code ec;
    std::filesystem::remove(out, ec);
}

TEST_CASE("a target that moves nothing is not written", "[io][fbx][morph]") {
    // An all-zero target is a blend shape a user can drag with no effect. The
    // reference ships some (nose-base-up has 11 literally-zero rows of 305), so
    // this is not hypothetical -- but a WHOLE target of zeros is.
    const auto out = tempFbx("morph_empty");
    const auto m   = quad();
    const auto rm  = core::RenderMesh::build(m);
    const std::vector<foundation::Vec3> zeros(rm.view().vertexCount(), foundation::Vec3{0, 0, 0});
    const std::array<foundation::MorphTarget, 1> targets{foundation::MorphTarget{"still", zeros}};
    REQUIRE(io::writeFbx(out, rm.view(), {}, nullptr, nullptr, targets).has_value());

    const auto b = readAll(out);
    const std::string blob(reinterpret_cast<const char*>(b.data()), b.size());
    CHECK(blob.find("still") == std::string::npos);
    CHECK(blob.find("BlendShapeChannel") == std::string::npos);

    std::error_code ec;
    std::filesystem::remove(out, ec);
}

TEST_CASE("a scene writes every entry as its own mesh", "[io][fbx][scene]") {
    // A dressed character is the body plus every worn proxy. Writing only the
    // first is the failure that looks like success: the file opens, the body is
    // there, and the clothes are simply missing.
    const auto out  = tempFbx("scene");
    const auto body = quad();
    core::Mesh shirt("shirt", 4);
    REQUIRE(shirt.setCoords({{0, 4, 0}, {2, 4, 0}, {2, 4, 3}, {0, 4, 3}}).has_value());
    shirt.addFaceGroup("g");
    REQUIRE(shirt.setFaces({0, 1, 2, 3}, {}, {0}).has_value());
    shirt.buildAdjacency();
    shirt.calcNormals();

    const auto bodyRm  = core::RenderMesh::build(body);
    const auto shirtRm = core::RenderMesh::build(shirt);
    foundation::MaterialDesc skinMat;
    skinMat.name = "Skin";
    foundation::MaterialDesc clothMat;
    clothMat.name = "Cloth";

    const std::array<io::FbxSceneEntry, 2> entries{
        io::FbxSceneEntry{.mesh = bodyRm.view(), .name = "body", .material = &skinMat},
        io::FbxSceneEntry{.mesh = shirtRm.view(), .name = "shirt", .material = &clothMat}};
    const auto r = io::writeFbxScene(out, entries);
    REQUIRE(r.has_value());
    CHECK(r->vertices == bodyRm.view().vertexCount() + shirtRm.view().vertexCount());

    const auto b = readAll(out);
    const std::string blob(reinterpret_cast<const char*>(b.data()), b.size());
    // Both models and both materials, by name.
    CHECK(blob.find("body") != std::string::npos);
    CHECK(blob.find("shirt") != std::string::npos);
    CHECK(blob.find("Skin") != std::string::npos);
    CHECK(blob.find("Cloth") != std::string::npos);
    // Two of each record, not one.
    const auto count = [&blob](const std::string& needle) {
        size_t n = 0;
        for (size_t at = blob.find(needle); at != std::string::npos;
             at        = blob.find(needle, at + 1)) {
            ++n;
        }
        return n;
    };
    CHECK(count("PolygonVertexIndex") == 2);

    // And every record still walks: two entries double the objects and the
    // connections, which is where an id scheme goes wrong.
    size_t topEnd = b.size() - 144;
    while (topEnd > 27 && b[topEnd - 1] == 0)
        --topEnd;
    checkRecords(b, 27, topEnd - 16);

    std::error_code ec;
    std::filesystem::remove(out, ec);
}

TEST_CASE("only the skinned entry gets a deformer", "[io][fbx][scene]") {
    // The body is rigged and the clothes are not. A writer that put the skin on
    // every entry would bind the shirt to the body's joints by index, which
    // deforms it by whatever those joints happen to mean for it.
    const auto out  = tempFbx("scene_skin");
    const auto body = quad();
    const auto rm   = core::RenderMesh::build(body);
    SkinnedQuad sk;
    const auto skin = sk.view(rm.view().vertexCount());

    core::Mesh hat("hat", 4);
    REQUIRE(hat.setCoords({{0, 9, 0}, {1, 9, 0}, {1, 9, 1}, {0, 9, 1}}).has_value());
    hat.addFaceGroup("g");
    REQUIRE(hat.setFaces({0, 1, 2, 3}, {}, {0}).has_value());
    hat.buildAdjacency();
    hat.calcNormals();
    const auto hatRm = core::RenderMesh::build(hat);

    const std::array<io::FbxSceneEntry, 2> entries{
        io::FbxSceneEntry{.mesh = rm.view(), .name = "body", .skin = &skin},
        io::FbxSceneEntry{.mesh = hatRm.view(), .name = "hat"}};
    REQUIRE(io::writeFbxScene(out, entries).has_value());

    const auto b = readAll(out);
    const std::string blob(reinterpret_cast<const char*>(b.data()), b.size());
    const auto count = [&blob](const std::string& needle) {
        size_t n = 0;
        for (size_t at = blob.find(needle); at != std::string::npos;
             at        = blob.find(needle, at + 1)) {
            ++n;
        }
        return n;
    };
    // One bind pose OBJECT for the whole file, not one per entry.
    //
    // Counted by the object-name marker, not by the word: "BindPose" appears
    // twice per Pose -- once as the object's subtype and once in its `Type`
    // child -- so counting the word says 2 for a single correct pose.
    const std::string poseObject = std::string("") + '\0' + '\x01' + "Pose";
    CHECK(count(poseObject) == 1);

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
