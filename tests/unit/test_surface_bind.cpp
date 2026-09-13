// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Binding an authored point to the body: the inverse of `fitProxy`.
//
// THE test here is the round trip. Bind a point, hand the binding to
// `fitProxy` on the same body, and the point must come back. That cannot be
// satisfied by a plausible-looking wrong answer: weights that do not sum to 1,
// a triangle that does not contain the point, or an offset measured from the
// wrong place all move the fitted result somewhere else.

#include "makehuman/core/SurfaceBind.h"

#include "makehuman/core/Mesh.h"
#include "makehuman/core/ObjReader.h"
#include "makehuman/core/Proxy.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <cmath>
#include <filesystem>
#include <numeric>
#include <vector>

using namespace mh::core;
using Catch::Matchers::WithinAbs;
using mh::foundation::Vec3;

namespace {

/// One quad in the XZ plane, so every expected barycentric weight is arithmetic
/// a reader can check by hand.
Mesh makeQuad() {
    Mesh m("quad", 4);
    std::vector<Vec3> coords{{0, 0, 0}, {1, 0, 0}, {1, 0, 1}, {0, 0, 1}};
    std::vector<mh::foundation::Vec2> uvs{{0, 0}, {1, 0}, {1, 1}, {0, 1}};
    REQUIRE(m.setCoords(coords).has_value());
    REQUIRE(m.setUVs(uvs).has_value());
    m.addFaceGroup("g");
    const std::vector<uint32_t> fv{0, 1, 2, 3};
    const std::vector<uint16_t> groups{0};
    REQUIRE(m.setFaces(fv, fv, groups).has_value());
    m.buildAdjacency();
    return m;
}

std::vector<uint32_t> allOf(const Mesh& m) {
    std::vector<uint32_t> all(m.vertexCount());
    std::iota(all.begin(), all.end(), 0U);
    return all;
}

/// Fit a single binding the way the application would.
Vec3 refit(const Mesh& m, const SurfaceBinding& b) {
    Proxy p;
    p.refVerts     = {b.refVerts};
    p.weights      = {b.weights};
    p.offsets      = {b.offset};
    p.maxRefIndex_ = std::max({b.refVerts[0], b.refVerts[1], b.refVerts[2]});
    std::vector<Vec3> out;
    REQUIRE(fitProxy(p, m.coord(), out));
    REQUIRE(out.size() == 1);
    return out[0];
}

}  // namespace

TEST_CASE("a point on the surface binds with no offset", "[core][surfacebind]") {
    const Mesh m = makeQuad();
    const auto b = bindToSurface(m, allOf(m), Vec3{0.25F, 0.0F, 0.25F});
    REQUIRE(b.has_value());

    CHECK_THAT(b->offset.x, WithinAbs(0.0, 1e-5));
    CHECK_THAT(b->offset.y, WithinAbs(0.0, 1e-5));
    CHECK_THAT(b->offset.z, WithinAbs(0.0, 1e-5));
    const float sum = b->weights[0] + b->weights[1] + b->weights[2];
    CHECK_THAT(sum, WithinAbs(1.0, 1e-5));
}

TEST_CASE("a bound point round-trips through fitProxy", "[core][surfacebind]") {
    const Mesh m = makeQuad();
    for (const Vec3 p : {Vec3{0.25F, 0.0F, 0.25F}, Vec3{0.8F, 0.0F, 0.1F}, Vec3{0.5F, 0.0F, 0.5F},
                         Vec3{0.0F, 0.0F, 0.0F}}) {
        const auto b = bindToSurface(m, allOf(m), p);
        REQUIRE(b.has_value());
        const Vec3 back = refit(m, *b);
        INFO("bound (" << p.x << "," << p.y << "," << p.z << ") came back as (" << back.x << ","
                       << back.y << "," << back.z << ")");
        CHECK_THAT(back.x, WithinAbs(static_cast<double>(p.x), 1e-4));
        CHECK_THAT(back.y, WithinAbs(static_cast<double>(p.y), 1e-4));
        CHECK_THAT(back.z, WithinAbs(static_cast<double>(p.z), 1e-4));
    }
}

TEST_CASE("a point standing off the surface keeps its standoff", "[core][surfacebind]") {
    const Mesh m = makeQuad();
    // Straight up from the middle of the quad: the weights should be the same
    // as for the point below it, and the whole displacement should land in the
    // offset -- that split is what lets hair ride a morphed body.
    const auto flat  = bindToSurface(m, allOf(m), Vec3{0.25F, 0.0F, 0.25F});
    const auto above = bindToSurface(m, allOf(m), Vec3{0.25F, 0.6F, 0.25F});
    REQUIRE(flat.has_value());
    REQUIRE(above.has_value());

    CHECK_THAT(above->offset.y, WithinAbs(0.6, 1e-4));
    for (size_t k = 0; k < 3; ++k) {
        CHECK(above->refVerts[k] == flat->refVerts[k]);
        CHECK_THAT(above->weights[k], WithinAbs(static_cast<double>(flat->weights[k]), 1e-4));
    }
    CHECK_THAT(refit(m, *above).y, WithinAbs(0.6, 1e-4));
}

TEST_CASE("an empty region binds nothing rather than guessing", "[core][surfacebind]") {
    const Mesh m = makeQuad();
    CHECK_FALSE(bindToSurface(m, std::vector<uint32_t>{}, Vec3{0.25F, 0.0F, 0.25F}).has_value());
}

TEST_CASE("an authored point binds to the real scalp and comes back",
          "[core][surfacebind][regression]") {
    // The case cornrows need: a point that is NOT a base vertex, standing off
    // the scalp, bound and refitted on the shipped mesh.
    const auto mesh = loadObj(std::filesystem::path(MH_DATA_DIR) / "3dobjs" / "base.obj");
    REQUIRE(mesh.has_value());

    const auto body = mesh->findFaceGroup("body");
    REQUIRE(body.has_value());
    const auto fvert    = mesh->fvert();
    const auto fgroup   = mesh->group();
    const size_t stride = mesh->vertsPerPrimitive();
    std::vector<uint8_t> onBody(mesh->vertexCount(), 0U);
    for (size_t f = 0; f < fgroup.size(); ++f) {
        if (fgroup[f] != *body) continue;
        for (size_t c = 0; c < stride; ++c)
            onBody[fvert[f * stride + c]] = 1U;
    }
    std::vector<uint32_t> scalp;
    const auto coords = mesh->coord();
    for (uint32_t v = 0; v < coords.size(); ++v) {
        if (onBody[v] != 0U && coords[v].y > 7.75F) scalp.push_back(v);
    }
    REQUIRE(scalp.size() == 157);

    // Midway between two known cap vertices, lifted 0.3 dm: an authored point
    // by construction, since no base vertex lives there.
    const Vec3 a = coords[5192];
    const Vec3 c = coords[5379];
    const Vec3 authored{(a.x + c.x) * 0.5F, (a.y + c.y) * 0.5F + 0.3F, (a.z + c.z) * 0.5F};

    const auto b = bindToSurface(*mesh, scalp, authored);
    REQUIRE(b.has_value());
    for (const uint32_t v : b->refVerts) {
        INFO("bound to base vertex " << v);
        CHECK(std::find(scalp.begin(), scalp.end(), v) != scalp.end());
    }

    // THE assertion that says "nearest", and the round trip is NOT it: binding
    // to ANY triangle plus the residual offset round-trips exactly, so a
    // mutation that kept the FARTHEST triangle passed every other test here.
    // What nearest actually buys is a SMALL offset -- the authored standoff and
    // nothing more. Bind to a far triangle and the offset becomes head-sized,
    // so the weights ride the wrong part of the mesh and the hair moves wrongly
    // when the body morphs. Authored standoff here is 0.3 dm.
    const float standoff = std::sqrt(b->offset.x * b->offset.x + b->offset.y * b->offset.y +
                                     b->offset.z * b->offset.z);
    INFO("offset magnitude " << standoff << " dm (authored standoff is 0.3)");
    CHECK(standoff < 0.45F);

    std::vector<Vec3> out;
    Proxy p;
    p.refVerts     = {b->refVerts};
    p.weights      = {b->weights};
    p.offsets      = {b->offset};
    p.maxRefIndex_ = std::max({b->refVerts[0], b->refVerts[1], b->refVerts[2]});
    REQUIRE(fitProxy(p, coords, out));
    INFO("authored y " << authored.y << " came back as " << out[0].y);
    CHECK_THAT(out[0].x, WithinAbs(static_cast<double>(authored.x), 1e-3));
    CHECK_THAT(out[0].y, WithinAbs(static_cast<double>(authored.y), 1e-3));
    CHECK_THAT(out[0].z, WithinAbs(static_cast<double>(authored.z), 1e-3));
}

TEST_CASE("a point beyond the edge clamps instead of extrapolating", "[core][surfacebind]") {
    const Mesh m = makeQuad();
    // Well outside the quad in the plane. The honest answer is the closest
    // point ON the triangle; the tempting one is the unclamped plane
    // projection, whose weights fall outside 0..1 and which `fitProxy` would
    // extrapolate into a position nobody authored. A mutation that removed one
    // clamp branch passed the rest of this file, which is why this exists.
    // Two points, deliberately in DIFFERENT Voronoi regions of the triangle:
    // (3,0,-2) is nearest corner (1,0,0), and (-2,0,-2) is nearest the origin
    // corner. One point exercises one branch, and a mutation that deleted the
    // origin-corner branch survived a test that only used the first.
    for (const Vec3 far : {Vec3{3.0F, 0.0F, -2.0F}, Vec3{-2.0F, 0.0F, -2.0F}}) {
        const auto b = bindToSurface(m, allOf(m), far);
        REQUIRE(b.has_value());

        float sum = 0.0F;
        for (const float w : b->weights) {
            INFO("weight " << w);
            CHECK(w >= -1e-5F);
            CHECK(w <= 1.0F + 1e-5F);
            sum += w;
        }
        CHECK_THAT(sum, WithinAbs(1.0, 1e-5));

        // The surface part must land ON the quad -- corner (1,0,0) is the nearest
        // point of it to (3,0,-2) -- with the rest carried as offset.
        const Vec3 surface{refit(m, *b).x - b->offset.x, refit(m, *b).y - b->offset.y,
                           refit(m, *b).z - b->offset.z};
        INFO("surface part (" << surface.x << "," << surface.y << "," << surface.z << ")");
        CHECK(surface.x <= 1.0F + 1e-4F);
        CHECK(surface.x >= -1e-4F);
        CHECK(surface.z >= -1e-4F);
        CHECK(surface.z <= 1.0F + 1e-4F);
    }
}
