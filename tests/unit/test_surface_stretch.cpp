// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Per-vertex stretch, on meshes whose answer is known by construction rather
// than captured from anything. There is no reference implementation to compare
// against: the Python has no tension, no correctives and no stretch measure, so
// every number here is one the geometry forces.
#include "makehuman/core/SurfaceStretch.h"

#include "makehuman/core/Mesh.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <vector>

using Catch::Matchers::WithinAbs;
using namespace mh;

namespace {

/// A `w` by `h` grid of unit quads in the z = 0 plane.
core::Mesh grid(size_t w, size_t h) {
    core::Mesh m("grid");
    std::vector<core::Vec3> coords;
    for (size_t y = 0; y <= h; ++y) {
        for (size_t x = 0; x <= w; ++x) {
            coords.push_back({static_cast<float>(x), static_cast<float>(y), 0.0F});
        }
    }
    REQUIRE(m.setCoords(std::move(coords)).has_value());
    std::vector<uint32_t> fv;
    const auto at = [w](size_t x, size_t y) { return static_cast<uint32_t>(y * (w + 1) + x); };
    for (size_t y = 0; y < h; ++y) {
        for (size_t x = 0; x < w; ++x) {
            fv.push_back(at(x, y));
            fv.push_back(at(x + 1, y));
            fv.push_back(at(x + 1, y + 1));
            fv.push_back(at(x, y + 1));
        }
    }
    REQUIRE(m.setFaces(std::move(fv), {}, {}).has_value());
    return m;
}

std::vector<core::Vec3> scaled(const core::Mesh& m, float k) {
    std::vector<core::Vec3> out;
    for (const auto& c : m.coord())
        out.push_back({c.x * k, c.y * k, c.z * k});
    return out;
}

}  // namespace

TEST_CASE("an unmoved surface has stretch exactly 1", "[stretch]") {
    // The resting case, and the one every vertex of an unposed character takes.
    // Anything but 1.0 here would tension a character that is not moving.
    const auto m = grid(3, 3);
    const std::vector<core::Vec3> same(m.coord().begin(), m.coord().end());
    const auto s = core::surfaceStretch(m, same);
    REQUIRE(s.size() == m.vertexCount());
    for (const float v : s)
        CHECK_THAT(v, WithinAbs(1.0, 1e-6));
}

TEST_CASE("a uniformly scaled surface stretches by the scale", "[stretch]") {
    // Every edge grows by k, so every mean of edge ratios is k. This is the
    // property that makes the measure a LENGTH ratio and not an area one: a
    // doubled plane reads 2, not 4.
    const auto m = grid(3, 3);
    for (const float k : {0.5F, 2.0F, 3.0F}) {
        const auto s = core::surfaceStretch(m, scaled(m, k));
        REQUIRE(s.size() == m.vertexCount());
        for (const float v : s)
            CHECK_THAT(v, WithinAbs(static_cast<double>(k), 1e-5));
    }
}

TEST_CASE("compression reads below 1 and stretch above it", "[stretch]") {
    // The sign convention the shader keys on. Getting it backwards would
    // brighten a crease and darken a stretch, which is exactly inverted.
    const auto m        = grid(2, 2);
    const auto squeezed = core::surfaceStretch(m, scaled(m, 0.5F));
    const auto pulled   = core::surfaceStretch(m, scaled(m, 2.0F));
    REQUIRE(!squeezed.empty());
    REQUIRE(!pulled.empty());
    CHECK(squeezed[0] < 1.0F);
    CHECK(pulled[0] > 1.0F);
}

TEST_CASE("stretch is local, not global", "[stretch]") {
    // Moving ONE vertex must tension its neighbourhood and leave the far side
    // alone. A measure that averaged over the mesh would pass every test above
    // and be useless for a wrinkle.
    const auto m = grid(3, 3);
    std::vector<core::Vec3> posed(m.coord().begin(), m.coord().end());
    posed[0]     = {-2.0F, -2.0F, 0.0F};  // drag a corner away
    const auto s = core::surfaceStretch(m, posed);
    REQUIRE(s.size() == m.vertexCount());
    CHECK(s[0] > 1.5F);                                        // the moved corner
    CHECK_THAT(s[m.vertexCount() - 1], WithinAbs(1.0, 1e-6));  // the far corner
}

TEST_CASE("a posed buffer of the wrong length is refused", "[stretch]") {
    // Returning a short buffer would be read past its end by the interleaver,
    // and a silently padded one would tension the wrong vertices.
    const auto m = grid(2, 2);
    CHECK(core::surfaceStretch(m, std::vector<core::Vec3>{}).empty());
    CHECK(core::surfaceStretch(m, scaled(m, 1.0F)).size() == m.vertexCount());
    std::vector<core::Vec3> shortBuf(m.vertexCount() - 1, core::Vec3{});
    CHECK(core::surfaceStretch(m, shortBuf).empty());
}

TEST_CASE("a vertex with no incident edge reads 1, not 0", "[stretch]") {
    // The base mesh carries loose geometry -- helper cages meet at coincident
    // vertices and some are referenced by no face at all. Zero would read as
    // total compression and light them as a deep crease.
    core::Mesh m("loose");
    std::vector<core::Vec3> coords{{0, 0, 0}, {1, 0, 0}, {1, 1, 0}, {0, 1, 0}, {5, 5, 5}};
    REQUIRE(m.setCoords(std::move(coords)).has_value());
    REQUIRE(m.setFaces({0, 1, 2, 3}, {}, {}).has_value());
    const auto s = core::surfaceStretch(m, scaled(m, 2.0F));
    REQUIRE(s.size() == 5);
    CHECK_THAT(s[0], WithinAbs(2.0, 1e-5));
    CHECK_THAT(s[4], WithinAbs(1.0, 1e-6));  // the loose one
}
