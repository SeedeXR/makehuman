// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Parity against the Python reference for the shipped base mesh.
//
// The expected values were MEASURED from the reference on 2026-08-29 with
// benchmarks/baseline_python_core.py, which loads data/3dobjs/base.obj through
// legacy-python/core/files3d.py and reports:
//
//     {'verts': 19158, 'faces': 18486, 'uvs': 21334,
//      'verts_per_primitive': 4, 'max_faces': 5}
//
// If any of these change, either the asset changed or the reader diverged --
// both are things a human must look at, which is why they are asserted exactly.

#include "makehuman/core/ObjReader.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <filesystem>

using Catch::Matchers::WithinAbs;
using namespace mh::core;

namespace {
std::filesystem::path baseObjPath() {
    return std::filesystem::path(MH_DATA_DIR) / "3dobjs" / "base.obj";
}
} // namespace

TEST_CASE("base mesh matches the Python reference counts", "[golden][parity][core]") {
    const auto path = baseObjPath();
    if (!std::filesystem::exists(path)) {
        SKIP("base.obj not present at " + path.string());
    }

    const auto mesh = loadObj(path);
    REQUIRE(mesh.has_value());

    // Measured from the reference; see the file header.
    CHECK(mesh->vertexCount() == 19158);
    CHECK(mesh->faceCount()   == 18486);
    CHECK(mesh->uvCount()     == 21334);
    CHECK(mesh->vertsPerPrimitive() == 4);
    CHECK(mesh->hasUV());

    // The base mesh is genuine quads, not degenerate triangles.
    CHECK(mesh->vertsPerFaceForExport() == 4);

    // The reference shrinks MAX_FACES from 8 to the true maximum valence and
    // reported 5 for this mesh (module3d.py:760-770, floored at 4).
    CHECK(mesh->maxValence() == 5);
}

TEST_CASE("base mesh has the expected face groups", "[golden][parity][core]") {
    const auto path = baseObjPath();
    if (!std::filesystem::exists(path)) SKIP("base.obj not present");

    const auto mesh = loadObj(path);
    REQUIRE(mesh.has_value());

    // base.obj contains 172 'g' statements but only 139 DISTINCT group names
    // (helper-*-eyelashes-* each recur 9 times, helper-tights twice). The
    // reference keys a dict by name and calls createFaceGroup only for a name
    // it has not seen (wavefront.py:120-123), so it produces 139 groups, not
    // 172. Ground truth taken by running the reference loader directly:
    //     files3d.loadMesh('data/3dobjs/base.obj') -> 139 groups
    CHECK(mesh->faceGroups().size() == 139);

    // Joint helpers drive skeleton joint positions (skeleton.py:1366-1384) and
    // helper geometry drives proxy fitting; both must survive loading.
    size_t jointGroups = 0;
    size_t helperGroups = 0;
    for (const auto& g : mesh->faceGroups()) {
        if (g.name.starts_with("joint-"))  ++jointGroups;
        if (g.name.starts_with("helper-")) ++helperGroups;
    }
    CHECK(jointGroups == 125);
    CHECK(helperGroups == 13);
}

TEST_CASE("base mesh is a plausible human scale in decimetres",
          "[golden][parity][core][units]") {
    const auto path = baseObjPath();
    if (!std::filesystem::exists(path)) SKIP("base.obj not present");

    const auto mesh = loadObj(path);
    REQUIRE(mesh.has_value());

    // Internal unit is the decimetre (human.py:694-699). The default figure is
    // roughly adult height, so heightCm must land in a sane human range --
    // this catches a unit-scale regression immediately.
    const float cm = mesh->heightCm();
    INFO("height = " << cm << " cm");
    CHECK(cm > 140.0F);
    CHECK(cm < 210.0F);
}

TEST_CASE("every base mesh vertex normal is unit length",
          "[golden][parity][core][normals]") {
    const auto path = baseObjPath();
    if (!std::filesystem::exists(path)) SKIP("base.obj not present");

    const auto mesh = loadObj(path);
    REQUIRE(mesh.has_value());

    // The reference divides without a zero guard (module3d.py:368) and can
    // produce NaN. Ours guards, so every normal must be finite and unit length.
    size_t bad = 0;
    for (const Vec3& n : mesh->vnorm()) {
        const float len = std::sqrt(dot(n, n));
        if (!std::isfinite(len) || std::abs(len - 1.0F) > 1e-4F) ++bad;
    }
    CHECK(bad == 0);
}

TEST_CASE("all face indices are in range", "[golden][parity][core]") {
    const auto path = baseObjPath();
    if (!std::filesystem::exists(path)) SKIP("base.obj not present");

    const auto mesh = loadObj(path);
    REQUIRE(mesh.has_value());

    const auto nVerts = static_cast<uint32_t>(mesh->vertexCount());
    const auto nUVs   = static_cast<uint32_t>(mesh->uvCount());

    size_t badV = 0;
    for (const uint32_t v : mesh->fvert()) if (v >= nVerts) ++badV;
    CHECK(badV == 0);

    size_t badT = 0;
    for (const uint32_t t : mesh->fuvs()) if (t >= nUVs) ++badT;
    CHECK(badT == 0);
}
