// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Every writer, at every unit, measured from the file it produced.
//
// This exists because the reference gets it wrong. fbx_binary.py:736 hardcodes
//     scale_factor = 10.0
// with the correct `10.0/config.scale` commented out on the line ABOVE it, so
// every non-decimetre FBX export is off by the configured scale
// (project_context.md 8). A character that is 1.7 m in one exporter and 17 m in
// another is the single most common interchange failure, and it is invisible
// until someone opens the file in a tool that respects units.

#include "makehuman/core/ObjReader.h"
#include "makehuman/core/RenderMesh.h"
#include "makehuman/io/GltfWriter.h"
#include "makehuman/io/ObjWriter.h"
#include "makehuman/io/SceneIO.h"
#include "makehuman/io/UsdWriter.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <limits>
#include <string>
#include <vector>

using namespace mh;

namespace {

/// The base mesh's height in its native units. Captured from the reference:
/// bbox y spans -8.4488 .. 8.4967 (tests/golden/mesh/MANIFEST.json).
constexpr double kHeightDm = 16.9455;

struct UnitCase {
    io::Unit unit;
    const char* name;
    double factor;
};

/// unitScale() values, restated here independently so a bug in unitScale
/// cannot make this test agree with itself.
constexpr std::array<UnitCase, 4> kUnits{{
    {io::Unit::Decimeter, "dm", 1.0},
    {io::Unit::Meter, "m", 0.1},
    {io::Unit::Centimeter, "cm", 10.0},
    {io::Unit::Inch, "in", 1.0 / 0.254},
}};

core::Mesh baseMesh() {
    auto m = core::loadObj(std::filesystem::path(MH_DATA_DIR) / "3dobjs" / "base.obj");
    REQUIRE(m.has_value());
    return std::move(*m);
}

std::string readAll(const std::filesystem::path& p) {
    std::ifstream in(p, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

std::filesystem::path tmp(const std::string& stem, const std::string& ext) {
    return std::filesystem::temp_directory_path() / ("mh_units_" + stem + ext);
}

/// Height measured from the OBJ's own `v` lines -- parsed back out of the file,
/// not taken from the writer's return value.
double objHeight(const std::filesystem::path& p) {
    std::ifstream in(p);
    std::string line;
    double lo = std::numeric_limits<double>::infinity();
    double hi = -lo;
    while (std::getline(in, line)) {
        if (line.size() < 2 || line[0] != 'v' || line[1] != ' ') continue;
        char* end = nullptr;
        std::strtod(line.c_str() + 2, &end);  // x
        const double y = std::strtod(end, &end);
        lo             = std::min(lo, y);
        hi             = std::max(hi, y);
    }
    return hi - lo;
}

/// glTF records POSITION min/max in the JSON, so the height is readable without
/// decoding the binary chunk.
double gltfHeight(const std::filesystem::path& p) {
    const std::string b         = readAll(p);
    const std::string minMarker = "\"min\":[";
    const auto minAt            = b.find(minMarker);
    REQUIRE(minAt != std::string::npos);
    const char* q = b.c_str() + minAt + minMarker.size();
    char* end     = nullptr;
    std::strtod(q, &end);                            // min x
    const double minY = std::strtod(end + 1, &end);  // min y

    const std::string maxMarker = "\"max\":[";
    const auto maxAt            = b.find(maxMarker);
    REQUIRE(maxAt != std::string::npos);
    q = b.c_str() + maxAt + maxMarker.size();
    std::strtod(q, &end);
    const double maxY = std::strtod(end + 1, &end);
    return maxY - minY;
}

double usdHeight(const std::filesystem::path& p) {
    const std::string t        = readAll(p);
    const std::string exMarker = "float3[] extent = [";
    const auto exAt            = t.find(exMarker);
    REQUIRE(exAt != std::string::npos);
    const auto from = exAt + exMarker.size();
    const auto end  = t.find("]\n", from);
    REQUIRE(end != std::string::npos);
    const std::string ex = t.substr(from, end - from);

    std::vector<double> nums;
    const char* q = ex.c_str();
    while (*q != 0 && nums.size() < 6) {
        if ((*q >= '0' && *q <= '9') || *q == '-') {
            char* e = nullptr;
            nums.push_back(std::strtod(q, &e));
            q = e;
        } else {
            ++q;
        }
    }
    REQUIRE(nums.size() == 6);
    return nums[4] - nums[1];
}

/// FBX is binary, so it is measured by reading it back through assimp.
double fbxHeight(const std::filesystem::path& p) {
    const auto back = io::importMesh(p);
    REQUIRE(back.has_value());
    double lo = std::numeric_limits<double>::infinity();
    double hi = -lo;
    for (const auto& v : back->mesh.coord) {
        lo = std::min(lo, static_cast<double>(v.y));
        hi = std::max(hi, static_cast<double>(v.y));
    }
    return hi - lo;
}

}  // namespace

TEST_CASE("OBJ exports at the requested unit", "[units][io]") {
    const auto mesh = baseMesh();
    for (const auto& c : kUnits) {
        CAPTURE(c.name);
        io::ObjWriteOptions o;
        o.unit = c.unit;

        const auto out = tmp(c.name, ".obj");
        REQUIRE(io::writeObj(out, mesh.view(), o).has_value());

        const double want = kHeightDm * c.factor;
        const double got  = objHeight(out);
        INFO("got " << got << " want " << want);
        CHECK(std::abs(got - want) < want * 1e-4);

        std::error_code ec;
        std::filesystem::remove(out, ec);
    }
}

TEST_CASE("glTF exports at the requested unit", "[units][io]") {
    const auto mesh = baseMesh();
    const auto rm   = core::RenderMesh::build(mesh);
    for (const auto& c : kUnits) {
        CAPTURE(c.name);
        io::GltfWriteOptions o;
        o.unit = c.unit;

        const auto out = tmp(c.name, ".glb");
        REQUIRE(io::writeGlb(out, rm.view(), o).has_value());

        const double want = kHeightDm * c.factor;
        const double got  = gltfHeight(out);
        INFO("got " << got << " want " << want);
        CHECK(std::abs(got - want) < want * 1e-4);

        std::error_code ec;
        std::filesystem::remove(out, ec);
    }
}

TEST_CASE("USD exports at the requested unit", "[units][io]") {
    const auto mesh = baseMesh();
    const auto rm   = core::RenderMesh::build(mesh);
    for (const auto& c : kUnits) {
        CAPTURE(c.name);
        io::UsdWriteOptions o;
        o.unit = c.unit;

        const auto out = tmp(c.name, ".usda");
        REQUIRE(io::writeUsda(out, rm.view(), o).has_value());

        const double want = kHeightDm * c.factor;
        const double got  = usdHeight(out);
        INFO("got " << got << " want " << want);
        CHECK(std::abs(got - want) < want * 1e-4);

        std::error_code ec;
        std::filesystem::remove(out, ec);
    }
}

// The reference's defect lives exactly here: fbx_binary.py hardcodes a factor
// of 10 regardless of the configured unit, so only the decimetre case is right.
// All four must be.
TEST_CASE("FBX exports at the requested unit", "[units][io]") {
    const auto mesh = baseMesh();
    const auto rm   = core::RenderMesh::build(mesh);
    for (const auto& c : kUnits) {
        CAPTURE(c.name);
        io::SceneExportOptions o;
        o.unit = c.unit;

        const auto out = tmp(c.name, ".fbx");
        REQUIRE(io::exportScene(out, rm.view(), io::SceneFormat::FbxBinary, o).has_value());

        const double want = kHeightDm * c.factor;
        const double got  = fbxHeight(out);
        INFO("got " << got << " want " << want);
        CHECK(std::abs(got - want) < want * 1e-3);  // through a binary round trip

        std::error_code ec;
        std::filesystem::remove(out, ec);
    }
}

// Every writer at the SAME unit must agree with every other. This is the
// property a user actually depends on: the same character imported into two
// tools should be the same size.
TEST_CASE("all writers agree at the same unit", "[units][io]") {
    const auto mesh = baseMesh();
    const auto rm   = core::RenderMesh::build(mesh);

    for (const auto& c : kUnits) {
        CAPTURE(c.name);
        io::ObjWriteOptions oo;
        oo.unit = c.unit;
        io::GltfWriteOptions go;
        go.unit = c.unit;
        io::UsdWriteOptions uo;
        uo.unit = c.unit;

        const auto po = tmp(std::string("agree_") + c.name, ".obj");
        const auto pg = tmp(std::string("agree_") + c.name, ".glb");
        const auto pu = tmp(std::string("agree_") + c.name, ".usda");
        REQUIRE(io::writeObj(po, mesh.view(), oo).has_value());
        REQUIRE(io::writeGlb(pg, rm.view(), go).has_value());
        REQUIRE(io::writeUsda(pu, rm.view(), uo).has_value());

        const double a = objHeight(po);
        const double b = gltfHeight(pg);
        const double d = usdHeight(pu);
        INFO("obj " << a << " gltf " << b << " usd " << d);
        CHECK(std::abs(a - b) < a * 1e-4);
        CHECK(std::abs(a - d) < a * 1e-4);

        std::error_code ec;
        std::filesystem::remove(po, ec);
        std::filesystem::remove(pg, ec);
        std::filesystem::remove(pu, ec);
    }
}

// --- The unit contract on IMPORT --------------------------------------------
//
// Import returns coordinates in the FILE's units and never converts them. That
// is deliberate: `fbxHeight` above measures a file precisely because import
// does not rescale. But a caller feeding an import into the app must convert,
// and before `metersPerUnit` existed there was nothing to convert BY.
//
// The failure is concrete: our own GLB round-trips a 16.9455 dm human back as
// 1.6946, because glTF is metres. Used as-is that is a 17 cm doll -- the same
// 10x class of error recorded against the reference's FBX.
TEST_CASE("import reports what a file unit is worth", "[units][io][import]") {
    const core::Mesh m = baseMesh();
    const auto rm      = core::RenderMesh::build(m);

    double sourceHeight = 0.0;
    {
        double lo = std::numeric_limits<double>::infinity();
        double hi = -lo;
        for (const auto& v : m.coord()) {
            lo = std::min(lo, static_cast<double>(v.y));
            hi = std::max(hi, static_cast<double>(v.y));
        }
        sourceHeight = hi - lo;
    }
    INFO("source height " << sourceHeight << " dm");
    REQUIRE(sourceHeight > 16.0);  // a real human in decimetres

    const auto out = std::filesystem::temp_directory_path() / "mh_unit_import.glb";
    const std::vector<io::GltfSceneEntry> scene{{rm.view(), "body", nullptr}};
    REQUIRE(io::writeGlbScene(out, scene).has_value());

    const auto back = io::importScene(out);
    REQUIRE(back.has_value());

    // glTF is metres by specification and carries no unit metadata, so this is
    // the only source of truth there is.
    CHECK(back->metersPerUnit == Catch::Approx(1.0));

    double imported = 0.0;
    {
        double lo = std::numeric_limits<double>::infinity();
        double hi = -lo;
        for (const auto& v : back->meshes[0].mesh.coord) {
            lo = std::min(lo, static_cast<double>(v.y));
            hi = std::max(hi, static_cast<double>(v.y));
        }
        imported = hi - lo;
    }

    // Raw, the numbers ARE ten times smaller -- that is the trap, pinned.
    INFO("imported height " << imported << " file units");
    CHECK(imported == Catch::Approx(sourceHeight / 10.0).epsilon(0.001));

    // And the contract makes it recoverable: decimetres = units * m/unit * 10.
    const double decimetres = imported * back->metersPerUnit * 10.0;
    CHECK(decimetres == Catch::Approx(sourceHeight).epsilon(0.001));

    std::error_code ec;
    std::filesystem::remove(out, ec);
}

TEST_CASE("an unitless format admits it", "[units][io][import]") {
    // OBJ and STL genuinely carry no unit. Reporting a made-up 1.0 would let a
    // caller convert confidently and wrongly; 0 says "you decide".
    const core::Mesh m = baseMesh();
    const auto rm      = core::RenderMesh::build(m);
    const auto out     = std::filesystem::temp_directory_path() / "mh_unit_import.stl";
    REQUIRE(io::exportScene(out, rm.view(), io::SceneFormat::StlBinary).has_value());

    const auto back = io::importScene(out);
    REQUIRE(back.has_value());
    CHECK(back->metersPerUnit == 0.0);

    std::error_code ec;
    std::filesystem::remove(out, ec);
}
