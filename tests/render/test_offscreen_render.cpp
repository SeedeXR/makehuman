// SPDX-License-Identifier: AGPL-3.0-or-later
//
// The renderer, checked by measuring pixels rather than by looking at a
// screenshot. A blank frame is the failure that looks like success in a log.
//
// Requires Qt, so this file is only compiled when the renderer is enabled.

#include "makehuman/core/Mesh.h"
#include "makehuman/core/ObjReader.h"
#include "makehuman/core/RenderMesh.h"
#include "makehuman/render/OffscreenRenderer.h"

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <cstdlib>
#include <filesystem>

using namespace mh;

namespace {

struct Scene {
    core::Mesh mesh;
    core::RenderMesh rm;
};

Scene bodyScene() {
    auto mesh = core::loadObj(std::filesystem::path(MH_DATA_DIR) / "3dobjs" / "base.obj");
    REQUIRE(mesh.has_value());
    mesh->buildAdjacency();
    mesh->calcNormals();
    auto rm = core::RenderMesh::build(*mesh);
    REQUIRE(rm.setFaceMask(*mesh, mesh->staticFaceMask()));
    return Scene{std::move(*mesh), std::move(rm)};
}

/// A build machine may have no Metal device at all. That is not a renderer
/// defect, so these skip rather than fail -- but they skip loudly, because a
/// silent pass would hide the renderer never having been exercised.
void requireDevice() {
    static const bool ok = render::OffscreenRenderer::create(MH_SHADER_DIR).has_value();
    if (!ok) SKIP("no Metal device on this machine -- renderer not exercised");
}

render::RenderSettings settings() {
    render::RenderSettings s;
    s.width     = 256;
    s.height    = 256;
    s.litsphere = std::filesystem::path(MH_DATA_DIR) / "litspheres" / "skinmat_caucasian.png";
    return s;
}

double coverage(const QImage& img, const render::RenderSettings& s) {
    const QColor bg = QColor::fromRgbF(s.background.x, s.background.y, s.background.z);
    size_t hit      = 0;
    for (int y = 0; y < img.height(); ++y) {
        for (int x = 0; x < img.width(); ++x) {
            const QColor c = img.pixelColor(x, y);
            if (std::abs(c.red() - bg.red()) > 6 || std::abs(c.green() - bg.green()) > 6 ||
                std::abs(c.blue() - bg.blue()) > 6) {
                ++hit;
            }
        }
    }
    return static_cast<double>(hit) / static_cast<double>(img.width() * img.height());
}

}  // namespace

TEST_CASE("the base mesh renders to a non-blank image", "[render]") {
    requireDevice();
    auto r = render::OffscreenRenderer::create(MH_SHADER_DIR);
    REQUIRE(r.has_value());
    CHECK((*r)->backendName() == "Metal");

    const Scene sc = bodyScene();
    const auto s   = settings();
    const auto img = (*r)->render(sc.rm.view(), s);
    REQUIRE(img.has_value());
    CHECK(img->width() == s.width);
    CHECK(img->height() == s.height);

    // A body at this framing covers roughly 8%. Both bounds matter: 0 means
    // nothing drew, and near-100% means the clear leaked or the mesh filled
    // the frame.
    const double cov = coverage(*img, s);
    INFO("coverage " << cov);
    CHECK(cov > 0.03);
    CHECK(cov < 0.30);
}

// The static mask hides 138 of the 139 face groups. Without it the helper cages
// draw as a solid skirt and a box over the face -- MORE pixels, not fewer,
// which is why coverage discriminates.
TEST_CASE("hiding helper geometry reduces what is drawn", "[render]") {
    requireDevice();
    auto r = render::OffscreenRenderer::create(MH_SHADER_DIR);
    REQUIRE(r.has_value());

    auto mesh = core::loadObj(std::filesystem::path(MH_DATA_DIR) / "3dobjs" / "base.obj");
    REQUIRE(mesh.has_value());
    mesh->buildAdjacency();
    mesh->calcNormals();
    auto rm = core::RenderMesh::build(*mesh);

    const auto s      = settings();
    const auto rawImg = (*r)->render(rm.view(), s);
    REQUIRE(rawImg.has_value());
    const double raw = coverage(*rawImg, s);

    REQUIRE(rm.setFaceMask(*mesh, mesh->staticFaceMask()));
    const auto maskedImg = (*r)->render(rm.view(), s);
    REQUIRE(maskedImg.has_value());
    const double masked = coverage(*maskedImg, s);

    INFO("raw " << raw << " masked " << masked);
    CHECK(masked < raw);
}

// If rotation changed nothing, the uniform buffer would not be reaching the
// shader and every frame would be identical.
TEST_CASE("rotation changes the rendered image", "[render]") {
    requireDevice();
    auto r = render::OffscreenRenderer::create(MH_SHADER_DIR);
    REQUIRE(r.has_value());
    const Scene sc = bodyScene();

    auto s0              = settings();
    auto s1              = settings();
    s1.camera.yawDegrees = 90.0F;

    const auto a = (*r)->render(sc.rm.view(), s0);
    const auto b = (*r)->render(sc.rm.view(), s1);
    REQUIRE(a.has_value());
    REQUIRE(b.has_value());

    size_t differing = 0;
    for (int y = 0; y < a->height(); ++y) {
        for (int x = 0; x < a->width(); ++x) {
            if (a->pixelColor(x, y) != b->pixelColor(x, y)) ++differing;
        }
    }
    INFO("differing pixels " << differing);
    CHECK(differing > 1000);

    // A body seen from the side covers less than one seen from the front.
    CHECK(coverage(*b, s1) < coverage(*a, s0));
}

TEST_CASE("a missing shader directory is reported", "[render]") {
    // create() checks the device before it probes for shaders, so without one
    // this reports NoDevice and the assertion below would fail rather than skip.
    requireDevice();
    const auto r = render::OffscreenRenderer::create("/nonexistent/shaders");
    REQUIRE_FALSE(r.has_value());
    CHECK(r.error().kind == render::RenderErrorKind::ShaderMissing);
}

TEST_CASE("a missing litsphere is reported", "[render]") {
    requireDevice();
    auto r = render::OffscreenRenderer::create(MH_SHADER_DIR);
    REQUIRE(r.has_value());
    const Scene sc = bodyScene();

    auto s         = settings();
    s.litsphere    = "/nonexistent/litsphere.png";
    const auto img = (*r)->render(sc.rm.view(), s);
    REQUIRE_FALSE(img.has_value());
    CHECK(img.error().kind == render::RenderErrorKind::TextureMissing);
}
