// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Every format we can both write and read, exported and read back, in one
// table.
//
// This is the mechanism, not a formality. Round-tripping is what found, in one
// session: an OBJ our own reader refused (`vertex referenced by no face`), a
// Collada material that came back asking for negative roughness, an FBX whose
// shininess arrived as our own struct default, and a GLB that returned a
// naked character because import read only the first mesh. Every one of those
// wrote a file that parsed.
//
// The property asserted is that the CHARACTER survives: same triangles, same
// real-world size. Vertex counts are deliberately not compared -- importers
// weld and split differently, and demanding equality would only pin whichever
// behaviour assimp happens to have.
//
// USD is absent because we have no USD reader at all -- `importScene` is
// assimp-backed and nothing here reads a stage back. `usdchecker` and Blender
// are what validate that side.

#include "makehuman/core/Mesh.h"
#include "makehuman/core/ObjReader.h"
#include "makehuman/core/RenderMesh.h"
#include "makehuman/io/GltfWriter.h"
#include "makehuman/io/ObjWriter.h"
#include "makehuman/io/SceneIO.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

using namespace mh;

namespace {

core::Mesh baseMesh() {
    auto m = core::loadObj(std::filesystem::path(MH_DATA_DIR) / "3dobjs" / "base.obj");
    REQUIRE(m.has_value());
    m->calcNormals();
    return std::move(*m);
}

/// Vertical extent of an imported scene, in the file's own units.
double importedHeight(const io::ImportedScene& s) {
    double lo = 1e30;
    double hi = -1e30;
    for (const auto& m : s.meshes) {
        for (const auto& v : m.mesh.coord) {
            lo = std::min(lo, static_cast<double>(v.y));
            hi = std::max(hi, static_cast<double>(v.y));
        }
    }
    return hi - lo;
}

size_t importedTriangles(const io::ImportedScene& s) {
    size_t n = 0;
    for (const auto& m : s.meshes)
        n += m.mesh.faceCount();
    return n;
}

}  // namespace

TEST_CASE("a character survives a round trip through every readable format", "[io][roundtrip]") {
    const core::Mesh mesh = baseMesh();
    const auto rm         = core::RenderMesh::build(mesh);
    const double wantHeightDm =
        static_cast<double>(mesh.heightCm()) / 10.0;  // heightCm is 10x decimetres
    const size_t wantTriangles = rm.view().triangleCount();
    REQUIRE(wantTriangles > 1000);
    REQUIRE(wantHeightDm > 1.0);

    struct Case {
        const char* ext;
        io::SceneFormat format;
        /// What one file unit is worth in metres, for the formats that do not
        /// say. OBJ and STL are genuinely unitless, so the writer's own default
        /// is the only answer -- which is exactly what `metersPerUnit == 0`
        /// means on the import side.
        double assumedMetersPerUnit;
    };

    const Case cases[] = {
        {".fbx", io::SceneFormat::FbxBinary, 0.0},
        // Collada needs no assumption: it declares its unit and assimp applies
        // it, so the import reports 1.0 and comes back in metres.
        {".dae", io::SceneFormat::Collada, 0.0},
        {".stl", io::SceneFormat::StlBinary, 0.01},  // written at the cm default
        {".3mf", io::SceneFormat::ThreeMf, 0.01},    // ditto
    };

    for (const Case& c : cases) {
        DYNAMIC_SECTION(c.ext) {
            const auto out =
                std::filesystem::temp_directory_path() / (std::string("mh_rt_body") + c.ext);
            std::error_code ec;
            std::filesystem::remove(out, ec);

            const std::vector<io::SceneEntry> scene{{rm.view(), "body", nullptr, nullptr}};
            const auto w = io::exportScene(out, scene, c.format);
            INFO(c.ext << " write: " << (w ? std::string{} : w.error().message()));
            REQUIRE(w.has_value());

            const auto back = io::importScene(out);
            INFO(c.ext << " read: " << (back ? std::string{} : back.error().message()));
            REQUIRE(back.has_value());

            CHECK(importedTriangles(*back) == wantTriangles);

            const double mpu =
                back->metersPerUnit != 0.0 ? back->metersPerUnit : c.assumedMetersPerUnit;
            const double gotDm = importedHeight(*back) * mpu * 10.0;
            INFO(c.ext << " height " << gotDm << " dm, wanted " << wantHeightDm
                       << ", metersPerUnit " << back->metersPerUnit);
            CHECK(gotDm == Catch::Approx(wantHeightDm).epsilon(0.001));

            std::filesystem::remove(out, ec);
        }
    }
}

// Collada DECLARES its unit, and ours declared the wrong one.
//
// assimp's Collada exporter writes `<unit name="meter" meter="1"/>`
// unconditionally, while we handed it centimetre-scaled coordinates -- so the
// file said a head vertex at `155.593674` was **155 metres** up. A
// spec-conforming consumer reads our character as 155 m tall: the same 100x
// class as the reference's FBX defect, in a file we produce.
//
// Checked as agreement between the two, not against a constant: the declared
// metres-per-unit times a coordinate must be the real-world size, whatever unit
// the caller asked for.
TEST_CASE("a Collada file declares the unit it was written in", "[io][roundtrip][units]") {
    const core::Mesh mesh = baseMesh();
    const auto rm         = core::RenderMesh::build(mesh);

    struct Want {
        io::Unit unit;
        const char* name;
        double meters;
    };

    const Want wants[] = {
        {io::Unit::Meter, "meter", 1.0},
        {io::Unit::Centimeter, "centimeter", 0.01},
        {io::Unit::Decimeter, "decimeter", 0.1},
    };

    for (const Want& w : wants) {
        DYNAMIC_SECTION(w.name) {
            const auto out = std::filesystem::temp_directory_path() / "mh_rt_unit.dae";
            std::error_code ec;
            std::filesystem::remove(out, ec);

            io::SceneExportOptions o;
            o.unit = w.unit;
            const std::vector<io::SceneEntry> scene{{rm.view(), "body", nullptr, nullptr}};
            REQUIRE(io::exportScene(out, scene, io::SceneFormat::Collada, o).has_value());

            std::ifstream in(out);
            const std::string t((std::istreambuf_iterator<char>(in)),
                                std::istreambuf_iterator<char>());
            const auto at = t.find("<unit ");
            REQUIRE(at != std::string::npos);
            const std::string decl = t.substr(at, t.find("/>", at) - at + 2);
            INFO("declared: " << decl);
            CHECK(decl.find(std::string("name=\"") + w.name + "\"") != std::string::npos);

            // And the number, read back out of the declaration.
            const auto mAt = decl.find("meter=\"");
            REQUIRE(mAt != std::string::npos);
            const double declared = std::stod(decl.substr(mAt + 7));
            CHECK(declared == Catch::Approx(w.meters).epsilon(1e-9));

            // End to end: assimp's Collada reader APPLIES the declared unit, so
            // an import comes back in metres whatever we wrote it in. That is
            // the proof the declaration and the coordinates agree, and it only
            // became true once the declaration stopped lying -- before, the
            // reader saw `meter="1"` and handed back raw centimetres.
            const auto back = io::importScene(out);
            REQUIRE(back.has_value());
            const double metres = importedHeight(*back);
            INFO("imported height " << metres << " m, metersPerUnit " << back->metersPerUnit);
            CHECK(metres ==
                  Catch::Approx(static_cast<double>(mesh.heightCm()) / 100.0).epsilon(0.001));

            std::filesystem::remove(out, ec);
        }
    }
}

// glTF has its own writer, not assimp's, so it gets its own case rather than a
// row in the table above.
TEST_CASE("a character survives a GLB round trip", "[io][roundtrip][gltf]") {
    const core::Mesh mesh = baseMesh();
    const auto rm         = core::RenderMesh::build(mesh);

    const auto out = std::filesystem::temp_directory_path() / "mh_rt_body.glb";
    std::error_code ec;
    std::filesystem::remove(out, ec);
    REQUIRE(io::writeGlb(out, rm.view()).has_value());

    const auto back = io::importScene(out);
    REQUIRE(back.has_value());
    CHECK(importedTriangles(*back) == rm.view().triangleCount());

    // glTF fixes its unit at the metre by specification, so this is the one
    // format where metersPerUnit is not a guess.
    CHECK(back->metersPerUnit == 1.0);
    const double gotDm = importedHeight(*back) * 10.0;
    INFO("height " << gotDm << " dm");
    CHECK(gotDm == Catch::Approx(static_cast<double>(mesh.heightCm()) / 10.0).epsilon(0.001));

    std::filesystem::remove(out, ec);
}

// OBJ is written by our own writer and read by our own reader, so this is the
// one round trip with no third-party code in it at all -- and it is the one
// that caught a file our reader refused outright.
TEST_CASE("a character survives an OBJ round trip through our own reader", "[io][roundtrip][obj]") {
    const core::Mesh mesh = baseMesh();

    const auto out = std::filesystem::temp_directory_path() / "mh_rt_body.obj";
    std::error_code ec;
    std::filesystem::remove(out, ec);
    // Held in a named vector: `faceMask` is a span, and assigning a temporary
    // to it dangles -- which -Werror,-Wdangling-assignment-gsl caught here.
    const std::vector<uint8_t> visible = mesh.staticFaceMask();
    io::ObjWriteOptions o;
    o.faceMask = visible;  // the app always exports masked
    REQUIRE(io::writeObj(out, mesh.view(), o).has_value());

    const auto back = core::loadObj(out);
    if (!back) INFO(back.error().message());
    REQUIRE(back.has_value());

    // Quads in, quads out: the OBJ path does not triangulate.
    const size_t kept = static_cast<size_t>(std::count(visible.begin(), visible.end(), uint8_t{1}));
    CHECK(back->faceCount() == kept);
    CHECK(back->vertexCount() <= mesh.vertexCount());

    // The VISIBLE height, not the mesh's. `Mesh::heightCm` bounds every vertex
    // including the helper cage, which reaches past the body -- 169.455 cm
    // against the 166.589 a viewer sees. The masked export carries the second,
    // and expecting the first is how I first wrote this test.
    float lo = 1e30F;
    float hi = -1e30F;
    for (size_t f = 0; f < mesh.faceCount(); ++f) {
        if (visible[f] == 0) continue;
        for (size_t c = 0; c < mesh.vertsPerPrimitive(); ++c) {
            const float y = mesh.coord()[mesh.fvert()[f * mesh.vertsPerPrimitive() + c]].y;
            lo            = std::min(lo, y);
            hi            = std::max(hi, y);
        }
    }
    CHECK(back->heightCm() == Catch::Approx((hi - lo) * 10.0F).epsilon(0.001));

    std::filesystem::remove(out, ec);
}
