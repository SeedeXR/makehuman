// SPDX-License-Identifier: AGPL-3.0-or-later
//
// The base topology hash (owner directive 12.1 and 12.7).
//
// "Topology hash guard, so a base mesh edit fails loudly instead of silently
// corrupting every delta." Correctives, skin weights, blend-shape deltas and
// LOD provenance are all indexed by VERTEX, so they are only meaningful
// against the topology they were authored on. The hash is the identity of that
// topology, written into every export beside the two version numbers.
//
// What it must and must not depend on is the whole design:
//   * NOT positions -- a morphed body is the same topology, and a hash that
//     moved with the sliders would mark every character incompatible with
//     every other.
//   * YES the index arrays, the counts, and the face-group names -- those are
//     what a delta or a face mask is expressed in terms of.

#include "makehuman/core/TopologyHash.h"

#include "makehuman/core/Mesh.h"
#include "makehuman/core/ObjReader.h"

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <vector>

using namespace mh;

namespace {

core::Mesh grid() {
    core::Mesh m("grid", 4);
    REQUIRE(m.setCoords({{0, 0, 0}, {1, 0, 0}, {1, 0, 1}, {0, 0, 1}, {2, 0, 0}, {2, 0, 1}})
                .has_value());
    REQUIRE(m.setUVs({{0, 0}, {1, 0}, {1, 1}, {0, 1}, {2, 0}, {2, 1}}).has_value());
    m.addFaceGroup("body");
    m.addFaceGroup("helper-cage");
    REQUIRE(m.setFaces({0, 1, 2, 3, 1, 4, 5, 2}, {0, 1, 2, 3, 1, 4, 5, 2}, {0, 1}).has_value());
    return m;
}

}  // namespace

TEST_CASE("the topology hash is stable and non-zero", "[core][topology]") {
    const core::Mesh a = grid();
    const core::Mesh b = grid();
    CHECK(core::topologyHash(a) != 0);
    CHECK(core::topologyHash(a) == core::topologyHash(b));
}

TEST_CASE("MOVING vertices does not change the topology hash", "[core][topology]") {
    // The property the whole thing rests on. Every character is the base mesh
    // with different positions; a hash that changed with them would say two
    // bodies of the same mesh were incompatible.
    core::Mesh m          = grid();
    const uint64_t before = core::topologyHash(m);
    std::vector<core::Vec3> moved(m.coord().begin(), m.coord().end());
    for (core::Vec3& v : moved)
        v.y += 0.5F;
    REQUIRE(m.changeCoords(std::move(moved)).has_value());
    CHECK(core::topologyHash(m) == before);
}

TEST_CASE("changing the topology changes the hash", "[core][topology]") {
    const uint64_t base = core::topologyHash(grid());

    SECTION("a different face index") {
        core::Mesh m = grid();
        REQUIRE(m.setFaces({0, 1, 2, 3, 1, 4, 5, 3}, {0, 1, 2, 3, 1, 4, 5, 2}, {0, 1}).has_value());
        CHECK(core::topologyHash(m) != base);
    }
    SECTION("a different UV index") {
        // The UV space is indexed independently of the vertex space, so a mesh
        // with the same faces and different UV wiring is a different target
        // for anything that carries per-corner data.
        core::Mesh m = grid();
        REQUIRE(m.setFaces({0, 1, 2, 3, 1, 4, 5, 2}, {0, 1, 2, 3, 1, 4, 5, 3}, {0, 1}).has_value());
        CHECK(core::topologyHash(m) != base);
    }
    SECTION("a face in a different group") {
        core::Mesh m = grid();
        REQUIRE(m.setFaces({0, 1, 2, 3, 1, 4, 5, 2}, {0, 1, 2, 3, 1, 4, 5, 2}, {0, 0}).has_value());
        CHECK(core::topologyHash(m) != base);
    }
    SECTION("a RENAMED group") {
        // `staticFaceMask` hides groups by NAME, so a rename changes which
        // geometry is visible without touching a single index.
        core::Mesh m("grid", 4);
        REQUIRE(m.setCoords({{0, 0, 0}, {1, 0, 0}, {1, 0, 1}, {0, 0, 1}, {2, 0, 0}, {2, 0, 1}})
                    .has_value());
        REQUIRE(m.setUVs({{0, 0}, {1, 0}, {1, 1}, {0, 1}, {2, 0}, {2, 1}}).has_value());
        m.addFaceGroup("body");
        m.addFaceGroup("joint-cage");  // was helper-cage
        REQUIRE(m.setFaces({0, 1, 2, 3, 1, 4, 5, 2}, {0, 1, 2, 3, 1, 4, 5, 2}, {0, 1}).has_value());
        CHECK(core::topologyHash(m) != base);
    }
    SECTION("more vertices") {
        core::Mesh m = grid();
        std::vector<core::Vec3> more(m.coord().begin(), m.coord().end());
        more.push_back({3, 0, 0});
        REQUIRE(m.setCoords(std::move(more)).has_value());
        CHECK(core::topologyHash(m) != base);
    }
}

TEST_CASE("the base mesh's hash is a fixed number", "[core][topology][slow]") {
    // Written down so that a change to `data/3dobjs/base.obj` fails HERE, with
    // a name, rather than in whatever downstream thing was indexed against it.
    // Update it deliberately and in the same commit as the mesh.
    const auto base = core::loadObj(std::filesystem::path(MH_DATA_DIR) / "3dobjs" / "base.obj");
    REQUIRE(base.has_value());
    CHECK(core::topologyHash(*base) == 0xE38C060123B5D0DBULL);
}
