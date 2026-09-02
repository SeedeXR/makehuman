// SPDX-License-Identifier: Apache-2.0
//
// The unit scale and ground offset every writer derives from its options.
//
// These were computed independently in FIVE places -- GltfWriter, SceneIO
// (twice), UsdWriter and ObjWriter -- as the same eight lines. Four were
// character-for-character identical; the fifth (OBJ) differs on purpose and
// keeps its own copy, because OBJ writes only the vertices its kept faces
// reference and must not level by one it dropped.
//
// The invariant worth protecting is not the arithmetic, it is the word ONE:
// a single offset for the whole scene. Levelling each mesh independently drops
// the clothes to the floor beside the body.
#include "makehuman/io/Transform.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <vector>

using namespace mh;
using Catch::Matchers::WithinAbs;

namespace {

/// A one-triangle mesh whose vertices all sit at height @p y.
struct Fixture {
    std::vector<foundation::Vec3> coord;
    std::vector<uint32_t> index{0, 1, 2};

    explicit Fixture(float y) : coord{{0, y, 0}, {1, y, 0}, {0, y, 1}} {}

    [[nodiscard]] foundation::RenderView view() const {
        return foundation::RenderView{coord, {}, {}, {}, index};
    }
};

/// What the writers hand `sceneTransform`: anything with a `.mesh`.
struct Entry {
    foundation::RenderView mesh;
};

}  // namespace

TEST_CASE("unitScale is the reference's own table", "[io][transform]") {
    // apps/gui/guiexport.py:124-129. The internal unit is the DECIMETRE, so
    // decimetre is 1 and everything else converts from it.
    CHECK(io::unitScale(io::Unit::Decimeter) == 1.0F);
    CHECK(io::unitScale(io::Unit::Meter) == 0.1F);
    CHECK(io::unitScale(io::Unit::Centimeter) == 10.0F);
    CHECK_THAT(static_cast<double>(io::unitScale(io::Unit::Inch)), WithinAbs(1.0 / 0.254, 1e-6));
}

TEST_CASE("a transform places a point by scale then offset", "[io][transform]") {
    const io::Transform t{2.0F, 5.0F};
    CHECK(t.placedY(3.0F) == 11.0F);  // 3*2 + 5, not (3+5)*2

    const foundation::Vec3 p = t.place({1.0F, 3.0F, -1.0F});
    CHECK(p.x == 2.0F);   // x and z take the scale...
    CHECK(p.y == 11.0F);  // ...y takes the scale AND the offset
    CHECK(p.z == -2.0F);
}

TEST_CASE("feetOnGround off leaves the offset at zero", "[io][transform]") {
    const Fixture f(-4.0F);
    const std::vector<Entry> scene{{f.view()}};
    const io::Transform t = io::sceneTransform(3.0F, false, scene);
    CHECK(t.scale == 3.0F);
    CHECK(t.groundOffset == 0.0F);
}

TEST_CASE("the ground offset lifts the lowest SCALED point to zero", "[io][transform]") {
    const Fixture f(-4.0F);
    const std::vector<Entry> scene{{f.view()}};

    // The scale is applied BEFORE the minimum is taken. Offsetting by the
    // unscaled minimum would leave the model below the floor by (scale-1)x its
    // depth -- and would look correct at the default scale of 1.
    const io::Transform t = io::sceneTransform(3.0F, true, scene);
    CHECK(t.groundOffset == 12.0F);
    CHECK_THAT(static_cast<double>(t.placedY(-4.0F)), WithinAbs(0.0, 1e-6));
}

TEST_CASE("ONE offset for the whole scene, not one per mesh", "[io][transform]") {
    // This is the bug the shared helper exists to prevent. The body's lowest
    // point is -4; a shirt hanging at -1 must be lifted by the BODY's offset,
    // which leaves it at 3, not levelled to 0 on its own.
    const Fixture body(-4.0F);
    const Fixture shirt(-1.0F);
    const std::vector<Entry> scene{{body.view()}, {shirt.view()}};

    const io::Transform t = io::sceneTransform(1.0F, true, scene);
    CHECK(t.groundOffset == 4.0F);
    CHECK(t.placedY(-4.0F) == 0.0F);  // the body meets the floor
    CHECK(t.placedY(-1.0F) == 3.0F);  // the shirt stays where it was, relatively
}

TEST_CASE("an empty scene is left alone rather than lifted to infinity", "[io][transform]") {
    // `lowest` starts at +infinity, so a scene with no vertices must be
    // detected -- negating it would put every later coordinate at -inf and
    // produce a file of NaNs rather than an empty one.
    const std::vector<Entry> empty;
    const io::Transform t = io::sceneTransform(2.0F, true, empty);
    CHECK(t.groundOffset == 0.0F);

    const Fixture noVerts(0.0F);
    std::vector<foundation::Vec3> none;
    const std::vector<Entry> scene{{foundation::RenderView{none, {}, {}, {}, noVerts.index}}};
    CHECK(io::sceneTransform(2.0F, true, scene).groundOffset == 0.0F);
}

TEST_CASE("the single-mesh overload agrees with a one-entry scene", "[io][transform]") {
    const Fixture f(-2.5F);
    const std::vector<Entry> scene{{f.view()}};
    CHECK(io::meshTransform(4.0F, true, f.view()).groundOffset ==
          io::sceneTransform(4.0F, true, scene).groundOffset);
}
