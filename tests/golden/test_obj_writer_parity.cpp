// SPDX-License-Identifier: Apache-2.0
//
// OBJ export compared against the reference's own writeObjFile output.
//
// Regenerate the reference file with the snippet recorded in
// memory/handover_session.md (it needs a material stub, because the reference
// writer reads mesh.object.material for its usemtl line).

#include "makehuman/io/ObjWriter.h"

#include "makehuman/core/ObjReader.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

using Catch::Matchers::WithinAbs;
using namespace mh;

namespace {

struct ObjCounts {
    size_t v{}, vn{}, vt{}, f{};
    std::vector<std::string> firstFace;
    std::string firstVertex;
};

ObjCounts scan(const std::filesystem::path& p) {
    ObjCounts c;
    std::ifstream in(p);
    std::string line;
    while (std::getline(in, line)) {
        if (line.starts_with("v ")) {
            if (c.v == 0) c.firstVertex = line;
            ++c.v;
        } else if (line.starts_with("vn ")) {
            ++c.vn;
        } else if (line.starts_with("vt ")) {
            ++c.vt;
        } else if (line.starts_with("f ")) {
            if (c.f == 0) {
                std::istringstream ss(line);
                std::string tok;
                ss >> tok;  // 'f'
                while (ss >> tok)
                    c.firstFace.push_back(tok);
            }
            ++c.f;
        }
    }
    return c;
}

std::filesystem::path tempObj(const char* stem) {
    return std::filesystem::temp_directory_path() / (std::string("mh_objtest_") + stem + ".obj");
}

}  // namespace

TEST_CASE("exported OBJ has the same element counts as the reference",
          "[golden][parity][io][obj]") {
    const auto ref = std::filesystem::path(MH_GOLDEN_DIR) / "obj" / "base_ref.obj";
    const auto src = std::filesystem::path(MH_DATA_DIR) / "3dobjs" / "base.obj";
    if (!std::filesystem::exists(ref) || !std::filesystem::exists(src)) {
        SKIP("obj fixture not present");
    }

    const auto mesh = core::loadObj(src);
    REQUIRE(mesh.has_value());

    const auto out = tempObj("counts");
    const auto r   = io::writeObj(out, mesh->view());
    REQUIRE(r.has_value());

    const ObjCounts got  = scan(out);
    const ObjCounts want = scan(ref);

    CHECK(got.v == want.v);
    CHECK(got.vn == want.vn);
    CHECK(got.vt == want.vt);
    CHECK(got.f == want.f);
    CHECK(got.firstFace.size() == want.firstFace.size());  // 4 corners: quads

    std::error_code ec;
    std::filesystem::remove(out, ec);
}

TEST_CASE("exported OBJ face indices match the reference exactly", "[golden][parity][io][obj]") {
    // Index bases and the v/vt/vn triple form must agree, or every consumer
    // reads a different mesh.
    const auto ref = std::filesystem::path(MH_GOLDEN_DIR) / "obj" / "base_ref.obj";
    const auto src = std::filesystem::path(MH_DATA_DIR) / "3dobjs" / "base.obj";
    if (!std::filesystem::exists(ref) || !std::filesystem::exists(src)) {
        SKIP("obj fixture not present");
    }

    const auto mesh = core::loadObj(src);
    REQUIRE(mesh.has_value());
    const auto out = tempObj("faces");
    REQUIRE(io::writeObj(out, mesh->view()).has_value());

    std::ifstream a(out);
    std::ifstream b(ref);
    std::string la, lb;
    size_t compared = 0, mismatched = 0;

    // Walk both files' face lines in order.
    const auto nextFace = [](std::ifstream& s, std::string& line) {
        while (std::getline(s, line)) {
            if (line.starts_with("f ")) return true;
        }
        return false;
    };
    while (nextFace(a, la) && nextFace(b, lb)) {
        if (la != lb) {
            if (mismatched < 3) UNSCOPED_INFO("got  " << la << "\nwant " << lb);
            ++mismatched;
        }
        ++compared;
    }
    CHECK(compared == 18486);
    CHECK(mismatched == 0);

    std::error_code ec;
    std::filesystem::remove(out, ec);
}

TEST_CASE("exported vertex positions match the reference", "[golden][parity][io][obj]") {
    const auto ref = std::filesystem::path(MH_GOLDEN_DIR) / "obj" / "base_ref.obj";
    const auto src = std::filesystem::path(MH_DATA_DIR) / "3dobjs" / "base.obj";
    if (!std::filesystem::exists(ref) || !std::filesystem::exists(src)) {
        SKIP("obj fixture not present");
    }

    const auto mesh = core::loadObj(src);
    REQUIRE(mesh.has_value());
    const auto out = tempObj("verts");
    REQUIRE(io::writeObj(out, mesh->view()).has_value());

    // Both are written at 4 decimals, so the text should agree exactly.
    const ObjCounts got  = scan(out);
    const ObjCounts want = scan(ref);
    CHECK(got.firstVertex == want.firstVertex);

    std::error_code ec;
    std::filesystem::remove(out, ec);
}

TEST_CASE("an exported OBJ reads back identically", "[io][obj]") {
    // The strongest single check: our writer and our reader agree on the real
    // 19,158-vertex mesh.
    const auto src = std::filesystem::path(MH_DATA_DIR) / "3dobjs" / "base.obj";
    if (!std::filesystem::exists(src)) SKIP("base.obj not present");

    const auto mesh = core::loadObj(src);
    REQUIRE(mesh.has_value());

    const auto out = tempObj("roundtrip");
    REQUIRE(io::writeObj(out, mesh->view()).has_value());

    const auto back = core::loadObj(out);
    REQUIRE(back.has_value());

    CHECK(back->vertexCount() == mesh->vertexCount());
    CHECK(back->faceCount() == mesh->faceCount());
    CHECK(back->uvCount() == mesh->uvCount());

    float worst = 0.0F;
    for (size_t i = 0; i < mesh->vertexCount(); ++i) {
        const auto& a = mesh->coord()[i];
        const auto& b = back->coord()[i];
        worst = std::max({worst, std::abs(a.x - b.x), std::abs(a.y - b.y), std::abs(a.z - b.z)});
    }
    CHECK(worst < 1e-4F);  // 4 decimals of precision in the file

    std::error_code ec;
    std::filesystem::remove(out, ec);
}

TEST_CASE("unit conversion scales the output", "[io][obj]") {
    // apps/gui/guiexport.py:124-129
    CHECK_THAT(io::unitScale(io::Unit::Decimeter), WithinAbs(1.0, 1e-6));
    CHECK_THAT(io::unitScale(io::Unit::Meter), WithinAbs(0.1, 1e-6));
    CHECK_THAT(io::unitScale(io::Unit::Centimeter), WithinAbs(10.0, 1e-6));
    CHECK_THAT(io::unitScale(io::Unit::Inch), WithinAbs(1.0 / 0.254, 1e-4));

    core::Mesh m("m", 4);
    REQUIRE(m.setCoords({{0, 0, 0}, {1, 0, 0}, {1, 0, 1}, {0, 0, 1}}).has_value());
    m.addFaceGroup("g");
    REQUIRE(m.setFaces({0, 1, 2, 3}, {}, {0}).has_value());
    m.calcNormals();

    io::ObjWriteOptions opt;
    opt.unit       = io::Unit::Centimeter;
    const auto out = tempObj("units");
    REQUIRE(io::writeObj(out, m.view(), opt).has_value());

    const auto back = core::loadObj(out);
    REQUIRE(back.has_value());
    // 1 decimetre becomes 10 centimetres.
    CHECK_THAT(back->coord()[1].x, WithinAbs(10.0, 1e-3));

    std::error_code ec;
    std::filesystem::remove(out, ec);
}

TEST_CASE("feet on ground puts the lowest vertex at zero", "[io][obj]") {
    core::Mesh m("m", 4);
    REQUIRE(m.setCoords({{0, 5, 0}, {1, 5, 0}, {1, 9, 1}, {0, 9, 1}}).has_value());
    m.addFaceGroup("g");
    REQUIRE(m.setFaces({0, 1, 2, 3}, {}, {0}).has_value());
    m.calcNormals();

    io::ObjWriteOptions opt;
    opt.feetOnGround = true;
    const auto out   = tempObj("ground");
    REQUIRE(io::writeObj(out, m.view(), opt).has_value());

    const auto back = core::loadObj(out);
    REQUIRE(back.has_value());
    float lowest = 1e30F;
    for (const auto& v : back->coord())
        lowest = std::min(lowest, v.y);
    CHECK_THAT(lowest, WithinAbs(0.0, 1e-3));

    std::error_code ec;
    std::filesystem::remove(out, ec);
}

TEST_CASE("a face mask omits the masked faces", "[io][obj]") {
    core::Mesh m("m", 4);
    REQUIRE(m.setCoords({{0, 0, 0}, {1, 0, 0}, {1, 0, 1}, {0, 0, 1}}).has_value());
    m.addFaceGroup("g");
    REQUIRE(m.setFaces({0, 1, 2, 3, 0, 1, 2, 3}, {}, {0, 0}).has_value());
    m.calcNormals();

    const std::array<uint8_t, 2> mask{1, 0};
    io::ObjWriteOptions opt;
    opt.faceMask = mask;

    const auto out = tempObj("mask");
    const auto r   = io::writeObj(out, m.view(), opt);
    REQUIRE(r.has_value());
    CHECK(r->faces == 1);
    CHECK(r->skipped == 1);

    std::error_code ec;
    std::filesystem::remove(out, ec);
}

TEST_CASE("a wrong-sized face mask is rejected", "[io][obj]") {
    core::Mesh m("m", 4);
    REQUIRE(m.setCoords({{0, 0, 0}, {1, 0, 0}, {1, 0, 1}, {0, 0, 1}}).has_value());
    m.addFaceGroup("g");
    REQUIRE(m.setFaces({0, 1, 2, 3}, {}, {0}).has_value());

    const std::array<uint8_t, 5> mask{1, 1, 1, 1, 1};
    io::ObjWriteOptions opt;
    opt.faceMask = mask;

    const auto r = io::writeObj(tempObj("badmask"), m.view(), opt);
    REQUIRE_FALSE(r.has_value());
    CHECK(r.error().kind == io::ObjWriteErrorKind::MaskSizeMismatch);
}

TEST_CASE("an empty mesh is rejected rather than writing a broken file", "[io][obj]") {
    const core::Mesh m;
    const auto r = io::writeObj(tempObj("empty"), m.view());
    REQUIRE_FALSE(r.has_value());
    CHECK(r.error().kind == io::ObjWriteErrorKind::EmptyMesh);
    CHECK_FALSE(r.error().message().empty());
}
