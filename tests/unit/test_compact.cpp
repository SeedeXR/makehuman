// SPDX-License-Identifier: Apache-2.0
//
// A third of every export was vertices nothing referenced.
//
// `RenderMesh::setFaceMask` filters the INDEX buffer and leaves the vertex
// buffer alone on purpose: the renderer uploads it once and toggling a mask has
// to stay cheap. Export inherited that. Measured on a default character's GLB:
// **21,833 body vertices, 14,517 referenced, 7,316 dead** -- 33.5% of every
// position, normal, UV, tangent, joint and weight written.
//
// And it is not only size: a consumer that bounds the vertex buffer sees the
// hidden helper cages. Blender reads the exported body as 1.6940 m where the
// visible mesh is 1.6594 m.

#include "makehuman/core/Mesh.h"
#include "makehuman/core/ObjReader.h"
#include "makehuman/core/RenderMesh.h"
#include "makehuman/io/Compact.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <filesystem>
#include <set>
#include <string>
#include <vector>

using namespace mh;

TEST_CASE("compaction drops exactly the vertices no triangle names", "[io][compact]") {
    // Four vertices, one triangle: vertex 2 is unreachable.
    const std::vector<foundation::Vec3> coord{{0, 0, 0}, {1, 0, 0}, {9, 9, 9}, {0, 1, 0}};
    const std::vector<foundation::Vec2> uv{{0, 0}, {1, 0}, {0.5F, 0.5F}, {0, 1}};
    const std::vector<uint32_t> index{0, 1, 3};
    const foundation::RenderView in{coord, uv, {}, {}, index};

    const auto out = io::compactUnusedVertices(in);
    REQUIRE(out.coord.size() == 3);
    CHECK(out.dropped() == 1);
    CHECK(out.remap[2] == io::CompactedMesh::kDropped);

    // Order preserved, so the survivors are 0,1,3 in that order.
    CHECK(out.remap[0] == 0);
    CHECK(out.remap[1] == 1);
    CHECK(out.remap[3] == 2);
    CHECK(out.index == std::vector<uint32_t>{0, 1, 2});
    CHECK(out.texco.size() == 3);
    CHECK(out.texco[2].y == 1.0F);  // the UV followed its vertex

    // Absent attribute arrays stay absent rather than being invented.
    CHECK(out.vnorm.empty());
    CHECK(out.vtang.empty());
}

TEST_CASE("compaction leaves a tight mesh untouched", "[io][compact]") {
    const std::vector<foundation::Vec3> coord{{0, 0, 0}, {1, 0, 0}, {0, 1, 0}};
    const std::vector<uint32_t> index{0, 1, 2};
    const foundation::RenderView in{coord, {}, {}, {}, index};

    const auto out = io::compactUnusedVertices(in);
    CHECK(out.dropped() == 0);
    CHECK(out.coord.size() == 3);
    CHECK(out.index == index);
}

TEST_CASE("the masked body mesh really is a third dead weight", "[io][compact][golden]") {
    const auto mesh = core::loadObj(std::filesystem::path(MH_DATA_DIR) / "3dobjs" / "base.obj");
    REQUIRE(mesh.has_value());
    auto rm = core::RenderMesh::build(*mesh);
    REQUIRE(rm.setFaceMask(*mesh, mesh->staticFaceMask()));

    const auto out = io::compactUnusedVertices(rm.view());
    INFO("before " << rm.view().vertexCount() << " after " << out.coord.size());
    CHECK(rm.view().vertexCount() == 21833);
    CHECK(out.coord.size() == 14517);
    CHECK(out.dropped() == 7316);

    // Every index must still be in range, and the triangle count unchanged --
    // compaction removes vertices, never geometry.
    CHECK(out.index.size() == rm.view().indexCount());
    for (const uint32_t v : out.index)
        REQUIRE(v < out.coord.size());

    // ...and every surviving vertex must still be USED. Dropping too few would
    // pass the range check above while leaving the waste in place.
    std::set<uint32_t> used(out.index.begin(), out.index.end());
    CHECK(used.size() == out.coord.size());

    // The bounding box shrinks to the visible mesh: this is the 1.6940 -> 1.6594
    // Blender measured, in decimetres.
    const auto extent = [](std::span<const foundation::Vec3> vs) {
        float lo = vs[0].y;
        float hi = vs[0].y;
        for (const auto& v : vs) {
            lo = std::min(lo, v.y);
            hi = std::max(hi, v.y);
        }
        return hi - lo;
    };
    const float before = extent(rm.view().coord);
    const float after  = extent(out.coord);
    INFO("y extent before " << before << " after " << after);
    CHECK(after < before);
}

// The skin's joints and weights are per RENDER vertex, so they have to move
// with the vertices or every vertex past the first dropped one is weighted to
// the wrong bone -- a file that loads, poses, and deforms wrongly.
TEST_CASE("skin attributes follow the vertices they belong to", "[io][compact][skin]") {
    const std::vector<std::string> names{"root", "a"};
    const std::vector<int32_t> parents{-1, 0};
    const std::vector<foundation::Mat4> rest(2);
    // Four vertices, 2 influences each. Vertex 2 will be dropped.
    const std::vector<uint32_t> joints{0, 1, 1, 0, 9, 9, 0, 1};
    const std::vector<float> weights{1.0F, 0.0F, 0.5F, 0.5F, 0.7F, 0.3F, 0.25F, 0.75F};
    const foundation::SkinView skin{names, parents, rest, joints, weights, 2};

    const std::vector<uint32_t> remap{0, 1, io::CompactedMesh::kDropped, 2};
    const auto [j, w] = io::compactSkinAttributes(skin, remap, 3);

    REQUIRE(j.size() == 6);
    REQUIRE(w.size() == 6);
    // Vertex 3's influences must land at slot 2, not at slot 3.
    CHECK(j[4] == 0);
    CHECK(j[5] == 1);
    CHECK(w[4] == 0.25F);
    CHECK(w[5] == 0.75F);
    // ...and the dropped vertex's (9,9) must appear nowhere.
    CHECK(std::find(j.begin(), j.end(), 9U) == j.end());
}
