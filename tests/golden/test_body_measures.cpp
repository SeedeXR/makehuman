// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Body surface area and the real weight it implies.
//
// The reference estimates weight by INVERTING Mosteller's body-surface-area
// formula: `bsa^2 * 3600 / heightCm` (`legacy/python/apps/human.py:635-638`).
// That is a computation, not a display option, which is why the status line
// showed a percentage for so long -- there was nothing to show.
//
// Two oracles, deliberately. The base mesh numbers come from running the
// reference; a unit cube's area is 6 by arithmetic, and pins Heron's formula
// without reference to anything.

#include "makehuman/core/Mesh.h"
#include "makehuman/core/ObjReader.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <filesystem>

using Catch::Matchers::WithinAbs;
using Catch::Matchers::WithinRel;
using namespace mh;

namespace {

core::Mesh baseMesh() {
    auto m = core::loadObj(std::filesystem::path(MH_DATA_DIR) / "3dobjs" / "base.obj");
    REQUIRE(m.has_value());
    return std::move(*m);
}

}  // namespace

TEST_CASE("a unit cube has a surface area of six", "[core][measure]") {
    // Arithmetic, not the reference: if Heron's formula or the quad split is
    // wrong this fails without anyone needing to agree with us about it.
    // The second argument is verts per PRIMITIVE, not the vertex count. Passing
    // 8 here made every measurement return zero, silently, because a mesh with
    // 8-sided faces is neither triangles nor quads.
    core::Mesh cube("cube", 4);
    REQUIRE(cube.setCoords({{0, 0, 0},
                            {1, 0, 0},
                            {1, 1, 0},
                            {0, 1, 0},
                            {0, 0, 1},
                            {1, 0, 1},
                            {1, 1, 1},
                            {0, 1, 1}})
                .has_value());
    cube.addFaceGroup("body");
    REQUIRE(cube.setFaces({0, 1, 2, 3, 4, 5, 6, 7, 0, 1, 5, 4, 2, 3, 7, 6, 1, 2, 6, 5, 0, 3, 7, 4},
                          {}, {0, 0, 0, 0, 0, 0})
                .has_value());
    CHECK_THAT(static_cast<double>(cube.bodySurfaceArea()), WithinAbs(6.0, 1e-5));
}

TEST_CASE("the base mesh's body area matches the reference", "[core][measure][parity]") {
    // 161.37875366210938 dm^2, from calculateSurface(mesh, vertGroups=['body'])
    // run against this exact base.obj. The 'body' group is 13,378 of the
    // 18,486 faces -- the rest are helper cages and joint cubes, and including
    // them would roughly double the answer.
    const auto mesh = baseMesh();
    CHECK_THAT(static_cast<double>(mesh.bodySurfaceArea()), WithinRel(161.37875366210938, 1e-6));
}

TEST_CASE("the base mesh's weight matches the reference", "[core][measure][parity]") {
    const auto mesh = baseMesh();
    // bsa is the area in SQUARE METRES: dm^2 / 100 (`human.py:630`).
    CHECK_THAT(static_cast<double>(mesh.bodySurfaceArea()) / 100.0,
               WithinRel(1.6137875366210936, 1e-6));
    // ... and height in centimetres, which is the OTHER masked measurement.
    CHECK_THAT(static_cast<double>(mesh.heightCm()), WithinRel(166.5889892578125, 1e-6));
    // 56.27933 kg. Not a plausible-looking number pulled from the formula --
    // this is what the reference prints for the shipped default character.
    CHECK_THAT(static_cast<double>(mesh.weightKg()), WithinRel(56.27933, 1e-5));
}

TEST_CASE("weight follows the surface, not the bounding box", "[core][measure]") {
    // Doubling every coordinate quadruples the area and doubles the height, so
    // the weight goes up by 4^2/2 = 8. Scaling is the one transform where the
    // right answer is arithmetic rather than measurement, and it catches a
    // formula that used height or area in the wrong power.
    auto mesh          = baseMesh();
    const float before = mesh.weightKg();
    std::vector<foundation::Vec3> doubled;
    doubled.reserve(mesh.vertexCount());
    for (const auto& v : mesh.coord())
        doubled.push_back({v.x * 2.0F, v.y * 2.0F, v.z * 2.0F});
    REQUIRE(mesh.setCoords(std::move(doubled)).has_value());
    CHECK_THAT(static_cast<double>(mesh.weightKg() / before), WithinRel(8.0, 1e-4));
}
