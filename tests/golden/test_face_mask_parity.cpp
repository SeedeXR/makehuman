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
