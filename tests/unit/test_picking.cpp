// SPDX-License-Identifier: Apache-2.0
//
// Turning a click into a point on the model.
//
// The reference does this with two GL readbacks: a colour-ID pass giving "is
// the human under the cursor" (`glmodule.py:641 pickMesh`, consumed by
// `gui3d.py:436`), then a depth readback unprojected to a world position
// (`glmodule.py:226`, `camera.py:774 mousePickHumanCenter`). Neither is
// translated here -- this is a CPU ray cast, which needs no readback, no extra
// pass and no round trip off the GPU, and is exact rather than quantised to a
// depth buffer. Moller-Trumbore is published maths, not anyone's authorship.
//
// The one behaviour that IS matched: what the reference does with the result.
// `selectedGroup` in the reference is assigned and never read
// (`mhmain.py:293,433` are its only two occurrences) -- the live consumer is
// click-to-focus, which recentres the view on the picked point.

#include "makehuman/render/Picking.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <cmath>
#include <vector>

using Catch::Matchers::WithinAbs;
using namespace mh;

namespace {

/// A unit quad in the z = 0 plane, spanning x,y in [-1,1], as two triangles.
struct Quad {
    std::vector<foundation::Vec3> coord{
        {-1.0F, -1.0F, 0.0F}, {1.0F, -1.0F, 0.0F}, {1.0F, 1.0F, 0.0F}, {-1.0F, 1.0F, 0.0F}};
    std::vector<uint32_t> index{0, 1, 2, 0, 2, 3};

    [[nodiscard]] foundation::RenderView view() const {
        return foundation::RenderView{coord, {}, {}, {}, index};
    }
};

constexpr int kW = 800;
constexpr int kH = 600;

}  // namespace

TEST_CASE("the centre pixel looks straight down the view axis", "[render][pick]") {
    render::Camera cam;  // no rotation, no pan
    const auto ray = render::rayThroughPixel(cam, kW / 2, kH / 2, kW, kH);

    // The model is what rotates in this port, so with no rotation the eye sits
    // at +Z looking toward the origin, `distance` away.
    CHECK_THAT(static_cast<double>(ray.origin.x), WithinAbs(0.0, 1e-5));
    CHECK_THAT(static_cast<double>(ray.origin.y), WithinAbs(0.0, 1e-5));
    CHECK_THAT(static_cast<double>(ray.origin.z),
               WithinAbs(static_cast<double>(cam.distance), 1e-4));

    CHECK_THAT(static_cast<double>(ray.direction.x), WithinAbs(0.0, 1e-5));
    CHECK_THAT(static_cast<double>(ray.direction.y), WithinAbs(0.0, 1e-5));
    CHECK_THAT(static_cast<double>(ray.direction.z), WithinAbs(-1.0, 1e-5));
}

TEST_CASE("a pixel off centre leans by exactly the field of view", "[render][pick]") {
    render::Camera cam;
    const float aspect  = static_cast<float>(kW) / static_cast<float>(kH);
    const float tanHalf = std::tan(cam.fovY * 0.5F * 3.14159265358979F / 180.0F);

    // The RIGHT edge of the frame. x/(-z) is the tangent of the horizontal
    // half-angle, which is the vertical one scaled by the aspect ratio. Getting
    // that factor wrong is invisible on a square viewport, which is why this
    // one is 800x600.
    const auto right = render::rayThroughPixel(cam, kW, kH / 2, kW, kH);
    CHECK_THAT(static_cast<double>(right.direction.x / -right.direction.z),
               WithinAbs(static_cast<double>(tanHalf * aspect), 1e-5));

    // The TOP edge. Screen y grows downward and world y grows up, so the top of
    // the window must give a POSITIVE y -- the sign that is wrong in every
    // first draft of this function.
    const auto top = render::rayThroughPixel(cam, kW / 2, 0, kW, kH);
    CHECK(top.direction.y > 0.0F);
    CHECK_THAT(static_cast<double>(top.direction.y / -top.direction.z),
               WithinAbs(static_cast<double>(tanHalf), 1e-5));
}

TEST_CASE("a ray hits the quad where the geometry says it should", "[render][pick]") {
    const Quad q;
    // Straight down -Z from +Z: the hit is the origin of the quad's plane.
    const render::Ray straight{{0.3F, -0.4F, 5.0F}, {0.0F, 0.0F, -1.0F}};
    const auto hit = render::intersect(q.view(), straight);
    REQUIRE(hit.has_value());
    CHECK_THAT(static_cast<double>(hit->x), WithinAbs(0.3, 1e-5));
    CHECK_THAT(static_cast<double>(hit->y), WithinAbs(-0.4, 1e-5));
    CHECK_THAT(static_cast<double>(hit->z), WithinAbs(0.0, 1e-5));
}

TEST_CASE("a ray that misses returns nothing", "[render][pick]") {
    const Quad q;
    const render::Ray past{{5.0F, 5.0F, 5.0F}, {0.0F, 0.0F, -1.0F}};
    CHECK_FALSE(render::intersect(q.view(), past).has_value());

    // And a ray pointing AWAY from the geometry must not hit it. A
    // Moller-Trumbore that forgets to reject t < 0 reports the surface behind
    // the camera, so clicking empty space above the head would focus the floor.
    const render::Ray backwards{{0.0F, 0.0F, 5.0F}, {0.0F, 0.0F, 1.0F}};
    CHECK_FALSE(render::intersect(q.view(), backwards).has_value());
}

TEST_CASE("the nearest of several hits wins", "[render][pick]") {
    // Two quads, at z = 2 and z = 0. A ray from z = 5 must return the closer.
    //
    // The NEAR quad is listed FIRST on purpose. With it last, a search that
    // simply keeps whatever it saw most recently returns the right answer by
    // accident and the test proves nothing -- which is exactly what the first
    // version of this did, and the mutation walked straight through it.
    std::vector<foundation::Vec3> coord{
        {-1.0F, -1.0F, 2.0F}, {1.0F, -1.0F, 2.0F}, {1.0F, 1.0F, 2.0F}, {-1.0F, 1.0F, 2.0F},
        {-1.0F, -1.0F, 0.0F}, {1.0F, -1.0F, 0.0F}, {1.0F, 1.0F, 0.0F}, {-1.0F, 1.0F, 0.0F}};
    std::vector<uint32_t> index{0, 1, 2, 0, 2, 3, 4, 5, 6, 4, 6, 7};
    const foundation::RenderView v{coord, {}, {}, {}, index};

    const auto hit = render::intersect(v, {{0.0F, 0.0F, 5.0F}, {0.0F, 0.0F, -1.0F}});
    REQUIRE(hit.has_value());
    CHECK_THAT(static_cast<double>(hit->z), WithinAbs(2.0, 1e-5));
}

TEST_CASE("rotating the model rotates the ray with it", "[render][pick]") {
    // A quad in the x=0 plane -- edge-on to an unrotated camera, facing it
    // after a 90 degree yaw. If the ray were built in eye space and never
    // brought back into the model's frame, this would miss.
    std::vector<foundation::Vec3> coord{
        {0.0F, -1.0F, -1.0F}, {0.0F, -1.0F, 1.0F}, {0.0F, 1.0F, 1.0F}, {0.0F, 1.0F, -1.0F}};
    std::vector<uint32_t> index{0, 1, 2, 0, 2, 3};
    const foundation::RenderView v{coord, {}, {}, {}, index};

    render::Camera cam;
    CHECK_FALSE(
        render::intersect(v, render::rayThroughPixel(cam, kW / 2, kH / 2, kW, kH)).has_value());

    cam.yawDegrees = 90.0F;
    const auto hit = render::intersect(v, render::rayThroughPixel(cam, kW / 2, kH / 2, kW, kH));
    REQUIRE(hit.has_value());
    CHECK_THAT(static_cast<double>(hit->x), WithinAbs(0.0, 1e-4));
}

TEST_CASE("focusing puts the picked point in the middle of the screen", "[render][pick]") {
    const Quad q;
    render::Camera cam;
    cam.distance   = 5.0F;
    cam.yawDegrees = 25.0F;

    // Click off centre, pick, focus, then pick the CENTRE pixel: it must land
    // on the same place. That is the whole contract, and it holds the pan sign
    // and the rotation order together -- either one backwards and the point
    // moves away from the middle rather than to it.
    const int px   = (kW / 2) + 90;
    const int py   = (kH / 2) - 60;
    const auto hit = render::intersect(q.view(), render::rayThroughPixel(cam, px, py, kW, kH));
    REQUIRE(hit.has_value());

    render::focusOn(cam, *hit);

    const auto centred =
        render::intersect(q.view(), render::rayThroughPixel(cam, kW / 2, kH / 2, kW, kH));
    REQUIRE(centred.has_value());
    CHECK_THAT(static_cast<double>(centred->x), WithinAbs(static_cast<double>(hit->x), 1e-4));
    CHECK_THAT(static_cast<double>(centred->y), WithinAbs(static_cast<double>(hit->y), 1e-4));
}

TEST_CASE("focusing does not move the camera in or out", "[render][pick]") {
    // Only pan changes. Distance is the zoom the user set, and stealing it on a
    // double-click would be a surprise.
    const Quad q;
    render::Camera cam;
    cam.distance = 7.5F;
    const auto hit =
        render::intersect(q.view(), render::rayThroughPixel(cam, kW / 3, kH / 3, kW, kH));
    REQUIRE(hit.has_value());

    render::focusOn(cam, *hit);
    CHECK_THAT(static_cast<double>(cam.distance), WithinAbs(7.5, 1e-6));
    CHECK_THAT(static_cast<double>(cam.fovY), WithinAbs(30.0, 1e-6));
}
