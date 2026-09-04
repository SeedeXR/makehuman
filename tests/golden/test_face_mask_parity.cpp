// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Face hiding, checked against fixtures captured from the Python reference.
//
// The fixture is synthetic by necessity: none of the four shipped
// .mhclo/.proxy files declares a `delete_verts` block, so there is no asset
// that exercises this path. The masks are applied to the reference's own
// `getFaceMaskForVertices` + `updateIndexBufferFaces`, so the oracle is still
// the reference's behaviour, not a guess about it.
//
// Regenerate with:
//     ./.venv-mh/bin/python tools/capture_fixture.py mask

#include "makehuman/core/Mesh.h"
#include "makehuman/core/ObjReader.h"
#include "makehuman/core/Proxy.h"
#include "makehuman/core/RenderMesh.h"
#include "makehuman/core/Subdivider.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <numeric>
#include <string>
#include <vector>

using namespace mh::core;

namespace {

std::filesystem::path fixtureDir() {
    return std::filesystem::path(MH_GOLDEN_DIR) / "mask";
}

template <typename T>
std::vector<T> readBlob(const std::filesystem::path& p) {
    std::ifstream in(p, std::ios::binary | std::ios::ate);
    if (!in) return {};
    const auto bytes = static_cast<size_t>(in.tellg());
    in.seekg(0);
    std::vector<T> out(bytes / sizeof(T));
    in.read(reinterpret_cast<char*>(out.data()), static_cast<std::streamsize>(bytes));
    return out;
}

Mesh loadBase() {
    auto m = loadObj(std::filesystem::path(MH_DATA_DIR) / "3dobjs" / "base.obj");
    REQUIRE(m.has_value());
    return std::move(*m);
}

const std::vector<std::string> kMasks{"none", "upper", "stride", "all"};

}  // namespace

TEST_CASE("face mask matches the reference for every captured vertex mask",
          "[mask][golden][parity]") {
    const Mesh mesh = loadBase();

    for (const auto& name : kMasks) {
        CAPTURE(name);
        const auto vertMask = readBlob<uint8_t>(fixtureDir() / (name + "_vertmask.bin"));
        const auto expected = readBlob<uint8_t>(fixtureDir() / (name + "_facemask.bin"));
        REQUIRE(vertMask.size() == mesh.vertexCount());
        REQUIRE(expected.size() == mesh.faceCount());

        const auto got = mesh.faceMaskForVisibleVertices(vertMask);
        REQUIRE(got.has_value());
        REQUIRE(got->size() == expected.size());

        size_t mismatches = 0;
        for (size_t f = 0; f < expected.size(); ++f) {
            if ((*got)[f] != expected[f]) ++mismatches;
        }
        CHECK(mismatches == 0);
    }
}

// The rule is "a face survives if ANY corner is visible". Hiding every 7th
// vertex hides 2,737 of 19,158 vertices and must hide ZERO faces. Under the
// inverted rule -- hide a face as soon as one corner is hidden -- almost every
// face would go. This is the test that tells the two apart.
TEST_CASE("scattered hidden vertices hide no faces", "[mask][golden][parity]") {
    const Mesh mesh     = loadBase();
    const auto vertMask = readBlob<uint8_t>(fixtureDir() / "stride_vertmask.bin");
    REQUIRE(vertMask.size() == mesh.vertexCount());

    const size_t hiddenVerts =
        vertMask.size() - static_cast<size_t>(std::accumulate(
                              vertMask.begin(), vertMask.end(), size_t{0},
                              [](size_t acc, uint8_t v) { return acc + (v != 0U ? 1U : 0U); }));
    CHECK(hiddenVerts == 2737);

    const auto got = mesh.faceMaskForVisibleVertices(vertMask);
    REQUIRE(got.has_value());
    const size_t hiddenFaces =
        static_cast<size_t>(std::count(got->begin(), got->end(), uint8_t{0}));
    CHECK(hiddenFaces == 0);
}

TEST_CASE("masked index buffer keeps the reference's visible-face count",
          "[mask][golden][parity]") {
    Mesh mesh      = loadBase();
    RenderMesh rm  = RenderMesh::build(mesh);
    const size_t n = rm.indexCount();

    // Everything below relates quad counts to triangle counts by x6, which
    // holds only if no face is a degenerate quad (those emit 3, not 6). State
    // it as a precondition instead of assuming it.
    REQUIRE(n == mesh.faceCount() * 6);

    for (const auto& name : kMasks) {
        CAPTURE(name);
        const auto vertMask = readBlob<uint8_t>(fixtureDir() / (name + "_vertmask.bin"));
        const auto refIndex = readBlob<uint32_t>(fixtureDir() / (name + "_index.bin"));

        const auto faceMask = mesh.faceMaskForVisibleVertices(vertMask);
        REQUIRE(faceMask.has_value());
        REQUIRE(rm.setFaceMask(mesh, *faceMask));

        // The reference submits quads (4 indices/face); we submit triangles
        // (6 per quad). The base mesh has no degenerate quads, so the visible
        // face count -- the thing both encode -- relates the two exactly.
        REQUIRE(refIndex.size() % 4 == 0);
        const size_t visibleFaces = refIndex.size() / 4;
        CHECK(rm.indexCount() == visibleFaces * 6);

        // Draw ranges, against the reference's own grpix. Both come from the
        // same stable sort of the same visible faces, so each group's range
        // maps exactly.
        //
        // grpix is in FACE units, not index units: the reference's `index` is a
        // 2-D (faces, 4) array and `np.unique(group, return_index=True)`
        // returns row indices (module3d.py:843-858). Verified by summing the
        // counts -- 18,486, which is the face count, not 73,944. We emit 6
        // triangle indices per quad, hence x6.
        //
        // The one divergence is deliberate: when the mask hides everything the
        // reference collapses grpix to zero rows (module3d.py:859-860) while we
        // keep one all-zero entry per group id so callers may always index by
        // group. An all-zero range draws nothing either way.
        const auto refGrpix = readBlob<uint32_t>(fixtureDir() / (name + "_grpix.bin"));
        REQUIRE(refGrpix.size() % 2 == 0);
        const size_t refGroups = refGrpix.size() / 2;

        if (refGroups == 0) {
            for (const auto& g : rm.groupRanges())
                CHECK(g.count == 0);
        } else {
            REQUIRE(rm.groupRanges().size() == refGroups);
            for (size_t gi = 0; gi < refGroups; ++gi) {
                CAPTURE(gi);
                CHECK(rm.groupRanges()[gi].first == refGrpix[gi * 2 + 0] * 6);
                CHECK(rm.groupRanges()[gi].count == refGrpix[gi * 2 + 1] * 6);
            }
        }

        // The ranges must also tile the buffer: no gap, no overlap.
        size_t covered = 0;
        for (const auto& g : rm.groupRanges()) {
            CHECK(static_cast<size_t>(g.first) + g.count <= rm.indexCount());
            covered += g.count;
        }
        CHECK(covered == rm.indexCount());

        // Every emitted index must address a real render vertex.
        for (const uint32_t i : rm.index())
            REQUIRE(i < rm.vertexCount());
    }

    // Clearing the mask restores exactly the unmasked buffer.
    REQUIRE(rm.setFaceMask(mesh, {}));
    CHECK(rm.indexCount() == n);
}

TEST_CASE("masking never touches the vertex buffer", "[mask]") {
    Mesh mesh     = loadBase();
    RenderMesh rm = RenderMesh::build(mesh);

    const size_t verts = rm.vertexCount();
    const std::vector<uint32_t> vmapBefore(rm.vmap().begin(), rm.vmap().end());

    std::vector<uint8_t> faceMask(mesh.faceCount(), 1U);
    faceMask[0] = 0U;
    REQUIRE(rm.setFaceMask(mesh, faceMask));

    // Hidden geometry leaves its vertices in the buffer, as the reference does
    // (module3d.py:842-849 filters r_faces, never vmap). Only the index shrinks.
    CHECK(rm.vertexCount() == verts);
    CHECK(std::equal(vmapBefore.begin(), vmapBefore.end(), rm.vmap().begin()));
    CHECK(rm.indexCount() == (mesh.faceCount() - 1) * 6);
}

TEST_CASE("a mask that does not describe the mesh is refused", "[mask]") {
    Mesh mesh      = loadBase();
    RenderMesh rm  = RenderMesh::build(mesh);
    const size_t n = rm.indexCount();

    const std::vector<uint8_t> tooShort(mesh.faceCount() - 1, 1U);
    CHECK_FALSE(rm.setFaceMask(mesh, tooShort));
    CHECK(rm.indexCount() == n);  // left alone, not rebuilt from a wrong mask

    const std::vector<uint8_t> wrongVerts(mesh.vertexCount() + 1, 1U);
    const auto bad = mesh.faceMaskForVisibleVertices(wrongVerts);
    REQUIRE_FALSE(bad.has_value());
    CHECK(bad.error() == MeshError::MaskSizeMismatch);
}

TEST_CASE("proxy delete_verts accumulate into a body vertex mask", "[mask][proxy]") {
    const Mesh mesh = loadBase();

    Proxy a;
    a.deleteVerts.assign(mesh.vertexCount(), 0U);
    a.deleteVerts[5] = 1U;
    a.deleteVerts[9] = 1U;

    Proxy b;
    b.deleteVerts.assign(mesh.vertexCount(), 0U);
    b.deleteVerts[9]  = 1U;  // overlapping, to prove it is a union not a count
    b.deleteVerts[11] = 1U;

    const std::array<const Proxy*, 2> worn{&a, &b};
    const auto visible = visibleVertexMask(worn, mesh.vertexCount());

    REQUIRE(visible.size() == mesh.vertexCount());
    CHECK(visible[5] == 0U);
    CHECK(visible[9] == 0U);
    CHECK(visible[11] == 0U);
    CHECK(visible[0] == 1U);
    const size_t hidden =
        static_cast<size_t>(std::count(visible.begin(), visible.end(), uint8_t{0}));
    CHECK(hidden == 3);

    // A proxy sized to a larger base mesh must not read past this body.
    Proxy oversized;
    oversized.deleteVerts.assign(mesh.vertexCount() + 100, 1U);
    const std::array<const Proxy*, 1> one{&oversized};
    const auto all = visibleVertexMask(one, mesh.vertexCount());
    CHECK(all.size() == mesh.vertexCount());
    CHECK(std::count(all.begin(), all.end(), uint8_t{0}) == static_cast<long>(mesh.vertexCount()));
}

// The two halves of the body mask were previously computed in different places
// or not at all: `staticFaceMask` reached the render mesh only -- so OBJ export
// shipped the helper cages -- and `visibleVertexMask` had no caller anywhere in
// src/. `bodyFaceMask` is the one place both are answered.
TEST_CASE("bodyFaceMask is the static mask when nothing is worn", "[mask][proxy]") {
    const Mesh mesh = loadBase();

    const auto mask = bodyFaceMask(mesh, mesh, {});
    REQUIRE(mask.has_value());
    CHECK(*mask == mesh.staticFaceMask());

    // The number the GLB writer already ships: 13,378 of 18,486 quads survive,
    // the rest being joint-* and helper-* groups.
    const auto visible = static_cast<size_t>(std::count(mask->begin(), mask->end(), uint8_t{1}));
    CHECK(mesh.faceCount() == 18486);
    CHECK(visible == 13378);
}

TEST_CASE("bodyFaceMask hides the faces a worn proxy deletes", "[mask][proxy]") {
    const Mesh mesh   = loadBase();
    const auto before = mesh.staticFaceMask();

    // Locate a face that is actually drawn, then delete all four of its
    // corners. Picking fixed vertex indices would risk landing in a helper
    // group, where the face is hidden already and the test proves nothing.
    const auto it = std::find(before.begin(), before.end(), uint8_t{1});
    REQUIRE(it != before.end());
    const size_t face = static_cast<size_t>(it - before.begin());

    Proxy p;
    p.deleteVerts.assign(mesh.vertexCount(), 0U);
    for (size_t c = 0; c < 4; ++c)
        p.deleteVerts[mesh.fvert()[face * 4 + c]] = 1U;

    const std::array<const Proxy*, 1> worn{&p};
    const auto mask = bodyFaceMask(mesh, mesh, worn);
    REQUIRE(mask.has_value());

    CHECK((*mask)[face] == 0U);
    // AND, not OR: nothing the static mask hid may come back.
    for (size_t f = 0; f < mask->size(); ++f)
        if (before[f] == 0U) REQUIRE((*mask)[f] == 0U);

    const auto wasVisible = std::count(before.begin(), before.end(), uint8_t{1});
    const auto nowVisible = std::count(mask->begin(), mask->end(), uint8_t{1});
    CHECK(nowVisible < wasVisible);
}

TEST_CASE("bodyFaceMask expands 4:1 onto a subdivided mesh", "[mask][proxy][subdiv]") {
    const Mesh mesh = loadBase();
    auto sd         = Subdivider::build(mesh);
    REQUIRE(sd.has_value());
    const Mesh& sub = sd->mesh();

    // Independent derivation: the subdivided mesh carries its own face groups
    // (each child inherits its parent's), so its own staticFaceMask is computed
    // without any knowledge of the 4:1 layout. If the expansion is wrong, these
    // disagree.
    const auto mask = bodyFaceMask(mesh, sub, {});
    REQUIRE(mask.has_value());
    CHECK(mask->size() == mesh.faceCount() * 4);
    CHECK(*mask == sub.staticFaceMask());

    // With something worn there is no independent oracle, so pin the mapping.
    Proxy p;
    p.deleteVerts.assign(mesh.vertexCount(), 1U);  // hide everything
    const std::array<const Proxy*, 1> worn{&p};
    const auto all = bodyFaceMask(mesh, sub, worn);
    REQUIRE(all.has_value());
    CHECK(std::count(all->begin(), all->end(), uint8_t{0}) ==
          static_cast<long>(mesh.faceCount() * 4));
}

TEST_CASE("bodyFaceMask refuses a mesh that is neither the base nor its subdivision",
          "[mask][proxy]") {
    const Mesh mesh = loadBase();
    Mesh other("other", 4);
    REQUIRE(other.setCoords(std::vector<mh::core::Vec3>(4, mh::core::Vec3{})));
    other.addFaceGroup("body");
    REQUIRE(other.setFaces({0, 1, 2, 3}, {}, {0}));

    const auto mask = bodyFaceMask(mesh, other, {});
    REQUIRE_FALSE(mask.has_value());
    CHECK(mask.error() == MeshError::MaskSizeMismatch);
}

// --- Proxy-on-proxy masking -------------------------------------------------
//
// Clothes hiding CLOTHES, not just clothes hiding body. A jacket's
// `delete_verts` must reach the shirt underneath it as well as the skin, or the
// shirt pokes through wherever the jacket cut a hole.
//
// `transferVertexMaskToProxy` (shared/proxy.py:960-983) remaps a BASE-mesh
// vertex mask onto a proxy through the proxy's own fit. Two rules, and they are
// not the same rule:
//
//   * a proxy vertex fitted to ONE base vertex (weights[1] and [2] both zero)
//     simply copies that vertex's visibility;
//   * an interpolated one is hidden only when at least TWO of its three
//     references are hidden -- `< 2` visible in the reference's arithmetic.
//
// The second rule is the one worth pinning: the natural guess (hide if any
// reference is hidden) erodes a much larger area around every hole.
TEST_CASE("a base-mesh mask transfers onto a proxy by its own fit", "[mask][proxy]") {
    Proxy p;
    // Four proxy vertices over a 10-vertex base:
    //   0: exact, on base vertex 0
    //   1: exact, on base vertex 3
    //   2: interpolated over 0,1,2
    //   3: interpolated over 3,4,5
    p.refVerts = {{0, 0, 0}, {3, 0, 0}, {0, 1, 2}, {3, 4, 5}};
    p.weights  = {{1.0F, 0.0F, 0.0F}, {1.0F, 0.0F, 0.0F}, {0.5F, 0.3F, 0.2F}, {0.5F, 0.3F, 0.2F}};

    std::vector<uint8_t> base(10, 1U);

    SECTION("an exact vertex copies its reference") {
        base[0]      = 0U;
        const auto m = transferVertexMaskToProxy(base, p);
        REQUIRE(m.size() == 4);
        CHECK(m[0] == 0U);  // followed base vertex 0
        CHECK(m[1] == 1U);  // base vertex 3 still visible
    }

    SECTION("one hidden reference of three is not enough") {
        base[0]      = 0U;  // only one of {0,1,2}
        const auto m = transferVertexMaskToProxy(base, p);
        CHECK(m[2] == 1U);  // two references still visible, so it stays
    }

    SECTION("two hidden references of three hide it") {
        base[0]      = 0U;
        base[1]      = 0U;
        const auto m = transferVertexMaskToProxy(base, p);
        CHECK(m[2] == 0U);
        CHECK(m[3] == 1U);  // the other interpolated vertex is untouched
    }

    SECTION("an all-visible base hides nothing") {
        const auto m = transferVertexMaskToProxy(base, p);
        CHECK(std::count(m.begin(), m.end(), uint8_t{1}) == 4);
    }

    SECTION("a reference past the end of the base mask is treated as visible") {
        // Not hypothetical: a proxy fitted to a different base mesh would
        // otherwise read out of bounds. Refusing to hide is the safe answer --
        // showing a vertex that should be hidden is a cosmetic bug; reading
        // past the array is not a bug we get to observe.
        Proxy far;
        far.refVerts = {{999, 0, 0}};
        far.weights  = {{1.0F, 0.0F, 0.0F}};
        const auto m = transferVertexMaskToProxy(base, far);
        REQUIRE(m.size() == 1);
        CHECK(m[0] == 1U);
    }
}

// The ORDER is the whole feature. A proxy is masked by the layers ABOVE it and
// never by itself, so the stack must be walked outermost first -- which is
// `reversed(sorted by z_depth)` in the reference
// (3_libraries_clothes_chooser.py:92-99, 125).
TEST_CASE("worn proxies mask the layers below them, outermost first", "[mask][proxy][layers]") {
    constexpr size_t kBase = 10;

    // An outer jacket (high z_depth) that deletes base vertices 0 and 1.
    Proxy jacket;
    jacket.zDepth = 80;
    jacket.deleteVerts.assign(kBase, 0U);
    jacket.deleteVerts[0] = 1U;
    jacket.deleteVerts[1] = 1U;
    jacket.refVerts       = {{5, 0, 0}};
    jacket.weights        = {{1.0F, 0.0F, 0.0F}};

    // An inner shirt (low z_depth) whose single vertex sits on base vertex 0 --
    // exactly where the jacket cut its hole.
    Proxy shirt;
    shirt.zDepth = 10;
    shirt.deleteVerts.assign(kBase, 0U);
    shirt.refVerts = {{0, 0, 0}};
    shirt.weights  = {{1.0F, 0.0F, 0.0F}};

    const std::array<const Proxy*, 2> worn{&shirt, &jacket};  // NOT in render order
    const WornMasks masks = wornVertexMasks(worn, kBase);

    REQUIRE(masks.perProxy.size() == 2);
    // Results are parallel to the INPUT span, whatever order that was in.
    CHECK(masks.perProxy[0].size() == 1);  // shirt
    CHECK(masks.perProxy[1].size() == 1);  // jacket

    // The shirt is hidden where the jacket deleted the body under it.
    CHECK(masks.perProxy[0][0] == 0U);
    // The jacket is NOT masked by itself, and nothing is above it.
    CHECK(masks.perProxy[1][0] == 1U);

    // And the body still gets the union, exactly as visibleVertexMask says.
    CHECK(masks.body[0] == 0U);
    CHECK(masks.body[1] == 0U);
    CHECK(masks.body[2] == 1U);
    CHECK(masks.body == visibleVertexMask(worn, kBase));
}

// A garment cuts a hole in what is UNDER it, never in itself. The reference
// takes each proxy's mask before folding that proxy's own `delete_verts` into
// the accumulator (3_libraries_clothes_chooser.py:130-141) -- swap those two
// steps and every garment erases itself wherever it deletes body.
//
// Measured: with the jacket's own vertex sitting away from its deletions, that
// swap was invisible to every other test here.
TEST_CASE("a garment never masks itself", "[mask][proxy][layers]") {
    constexpr size_t kBase = 10;

    Proxy jacket;
    jacket.zDepth = 80;
    jacket.deleteVerts.assign(kBase, 0U);
    jacket.deleteVerts[0] = 1U;
    // Its own vertex sits EXACTLY on the body vertex it deletes.
    jacket.refVerts = {{0, 0, 0}};
    jacket.weights  = {{1.0F, 0.0F, 0.0F}};

    const std::array<const Proxy*, 1> worn{&jacket};
    const WornMasks masks = wornVertexMasks(worn, kBase);

    REQUIRE(masks.perProxy[0].size() == 1);
    CHECK(masks.perProxy[0][0] == 1U);  // visible: nothing is above it
    CHECK(masks.body[0] == 0U);         // but the body still loses that vertex
}

TEST_CASE("a lower layer does not mask a higher one", "[mask][proxy][layers]") {
    constexpr size_t kBase = 10;

    // Same two garments, but now the INNER one deletes the body vertex the
    // OUTER one sits on. The jacket must be untouched: it is rendered above.
    Proxy shirt;
    shirt.zDepth = 10;
    shirt.deleteVerts.assign(kBase, 0U);
    shirt.deleteVerts[5] = 1U;
    shirt.refVerts       = {{0, 0, 0}};
    shirt.weights        = {{1.0F, 0.0F, 0.0F}};

    Proxy jacket;
    jacket.zDepth = 80;
    jacket.deleteVerts.assign(kBase, 0U);
    jacket.refVerts = {{5, 0, 0}};
    jacket.weights  = {{1.0F, 0.0F, 0.0F}};

    const std::array<const Proxy*, 2> worn{&shirt, &jacket};
    const WornMasks masks = wornVertexMasks(worn, kBase);

    CHECK(masks.perProxy[1][0] == 1U);  // jacket unmasked by the shirt below it
    CHECK(masks.body[5] == 0U);         // the body still loses that vertex
}

TEST_CASE("equal z_depth is broken deterministically, not by input order",
          "[mask][proxy][layers]") {
    // The reference sorts (z_depth, uuid) pairs, so ties fall to the uuid and
    // the result does not depend on the order the chooser happened to hand
    // them over. Two runs with the inputs swapped must agree.
    constexpr size_t kBase = 6;

    Proxy a;
    a.zDepth = 50;
    a.uuid   = "aaa";
    a.deleteVerts.assign(kBase, 0U);
    a.deleteVerts[0] = 1U;
    a.refVerts       = {{1, 0, 0}};
    a.weights        = {{1.0F, 0.0F, 0.0F}};

    Proxy b;
    b.zDepth = 50;
    b.uuid   = "bbb";
    b.deleteVerts.assign(kBase, 0U);
    b.deleteVerts[1] = 1U;
    b.refVerts       = {{0, 0, 0}};
    b.weights        = {{1.0F, 0.0F, 0.0F}};

    const std::array<const Proxy*, 2> ab{&a, &b};
    const std::array<const Proxy*, 2> ba{&b, &a};
    const WornMasks first  = wornVertexMasks(ab, kBase);
    const WornMasks second = wornVertexMasks(ba, kBase);

    // Same proxy, same answer, whichever slot it arrived in.
    CHECK(first.perProxy[0] == second.perProxy[1]);  // a
    CHECK(first.perProxy[1] == second.perProxy[0]);  // b
    CHECK(first.body == second.body);
}
