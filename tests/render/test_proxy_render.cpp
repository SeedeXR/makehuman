// SPDX-License-Identifier: AGPL-3.0-or-later
//
// The body and a worn proxy, rendered together. This is the path the eight
// blocked proxy choosers need: load a proxy, fit it to the body, and draw both
// meshes in one frame with their own materials.

#include "makehuman/core/Material.h"
#include "makehuman/core/Mesh.h"
#include "makehuman/core/ObjReader.h"
#include "makehuman/core/Proxy.h"
#include "makehuman/core/RenderMesh.h"
#include "makehuman/io/ObjWriter.h"
#include "makehuman/render/OffscreenRenderer.h"

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <vector>

using namespace mh;
namespace fs = std::filesystem;

namespace {

void requireDevice() {
    static const bool ok = render::OffscreenRenderer::create(MH_SHADER_DIR).has_value();
    if (!ok) SKIP("no Metal device on this machine -- renderer not exercised");
}

core::Mesh body() {
    auto mesh = core::loadObj(fs::path(MH_DATA_DIR) / "3dobjs" / "base.obj");
    REQUIRE(mesh.has_value());
    mesh->buildAdjacency();
    mesh->calcNormals();
    return std::move(*mesh);
}

/// The whole figure. The camera always looks at the model origin -- there is
/// no pan yet -- and the base mesh straddles it (y -8.45..8.50), so closing in
/// frames the navel and loses the head entirely. Resolution carries the detail
/// instead: eyes are a small target on a full figure.
render::RenderSettings fullFigure() {
    render::RenderSettings s;
    s.width     = 512;
    s.height    = 512;
    s.litsphere = fs::path(MH_DATA_DIR) / "litspheres" / "skinmat_caucasian.png";
    return s;
}

/// Pixels that differ from the clear colour. The tolerance matters: a readback
/// pixel is never bit-exact against a QColor built from floats, so an equality
/// test counts the whole frame as covered.
size_t drawnPixels(const QImage& img, const render::RenderSettings& s) {
    const QColor bg = QColor::fromRgbF(s.background.x, s.background.y, s.background.z);
    size_t n        = 0;
    for (int y = 0; y < img.height(); ++y) {
        for (int x = 0; x < img.width(); ++x) {
            const QColor c = img.pixelColor(x, y);
            if (std::abs(c.red() - bg.red()) > 6 || std::abs(c.green() - bg.green()) > 6 ||
                std::abs(c.blue() - bg.blue()) > 6) {
                ++n;
            }
        }
    }
    return n;
}

size_t differingPixels(const QImage& a, const QImage& b) {
    size_t n = 0;
    for (int y = 0; y < a.height(); ++y) {
        for (int x = 0; x < a.width(); ++x) {
            if (a.pixelColor(x, y) != b.pixelColor(x, y)) ++n;
        }
    }
    return n;
}

}  // namespace

TEST_CASE("a shipped eye proxy loads and fits the body", "[render][proxy]") {
    const core::Mesh mesh = body();

    const auto proxy =
        core::loadProxy(fs::path(MH_DATA_DIR) / "eyes" / "high-poly" / "high-poly.mhclo");
    REQUIRE(proxy.has_value());
    INFO("proxy " << proxy->name << " verts " << proxy->vertexCount() << " obj "
                  << proxy->objFile.string());
    CHECK(proxy->vertexCount() > 0);
    // A proxy fitted to this base mesh must not reference a vertex it lacks.
    CHECK(proxy->maxRefIndex() < mesh.vertexCount());

    std::vector<foundation::Vec3> fitted;
    REQUIRE(core::fitProxy(*proxy, mesh.coord(), fitted));
    REQUIRE(fitted.size() == proxy->vertexCount());

    // The eyes must land in the head, not at the origin or in the feet. The
    // base mesh is ~17 dm tall with the head at the top.
    foundation::Vec3 lo = fitted[0];
    foundation::Vec3 hi = fitted[0];
    for (const auto& p : fitted) {
        lo.x = std::min(lo.x, p.x);
        lo.y = std::min(lo.y, p.y);
        lo.z = std::min(lo.z, p.z);
        hi.x = std::max(hi.x, p.x);
        hi.y = std::max(hi.y, p.y);
        hi.z = std::max(hi.z, p.z);
    }
    INFO("fitted bounds y " << lo.y << ".." << hi.y << " x " << lo.x << ".." << hi.x);
    CHECK(lo.y > 0.0F);
    CHECK(hi.y > 5.0F);
}

TEST_CASE("the body and a worn proxy render together", "[render][proxy]") {
    requireDevice();
    auto r = render::OffscreenRenderer::create(MH_SHADER_DIR);
    REQUIRE(r.has_value());

    core::Mesh mesh = body();
    auto bodyRm     = core::RenderMesh::build(mesh);
    REQUIRE(bodyRm.setFaceMask(mesh, mesh.staticFaceMask()));

    const auto proxy =
        core::loadProxy(fs::path(MH_DATA_DIR) / "eyes" / "high-poly" / "high-poly.mhclo");
    REQUIRE(proxy.has_value());

    auto eyeMesh = core::loadObj(proxy->objFile);
    REQUIRE(eyeMesh.has_value());
    std::vector<foundation::Vec3> fitted;
    REQUIRE(core::fitProxy(*proxy, mesh.coord(), fitted));
    REQUIRE(eyeMesh->setCoords(std::move(fitted)).has_value());
    eyeMesh->buildAdjacency();
    eyeMesh->calcNormals();
    const auto eyeRm = core::RenderMesh::build(*eyeMesh);

    const auto s      = fullFigure();
    const auto eyeLit = fs::path(MH_DATA_DIR) / "litspheres" / "skinmat_eye.png";
    const std::vector<render::MeshInstance> bodyOnly{{bodyRm.view(), s.litsphere}};
    const std::vector<render::MeshInstance> dressed{{bodyRm.view(), s.litsphere},
                                                    {eyeRm.view(), eyeLit}};

    const auto plain    = (*r)->render(bodyOnly, s);
    const auto withEyes = (*r)->render(dressed, s);
    REQUIRE(plain.has_value());
    REQUIRE(withEyes.has_value());

    // The proxy alone, to separate "the proxy does not render" from "the proxy
    // renders but the face occludes most of it".
    const std::vector<render::MeshInstance> eyesOnly{{eyeRm.view(), eyeLit}};
    const auto bare = (*r)->render(eyesOnly, s);
    REQUIRE(bare.has_value());
    const size_t eyePixels = drawnPixels(*bare, s);
    const size_t changed   = differingPixels(*plain, *withEyes);
    // Measured: 64 px alone, 12 px of change when worn -- eyes are a small
    // target on a full figure and the eyelids occlude most of what is left.
    // The bounds sit under those so a proxy that stops rendering, or fits to
    // the origin, fails; they are not tight enough to be brittle.
    INFO("eye proxy alone: " << eyePixels << " px; worn, it changes " << changed << " px");
    CHECK(eyePixels > 20);
    CHECK(changed > 4);
}

// A dressed export must carry the material of each thing worn, not one default
// shared by all of them. The eye proxy names its own `.mhmat`, so the writers
// have something real to distinguish.
TEST_CASE("a worn proxy names its own material", "[render][proxy]") {
    const auto proxy =
        core::loadProxy(fs::path(MH_DATA_DIR) / "eyes" / "high-poly" / "high-poly.mhclo");
    REQUIRE(proxy.has_value());

    // `material ../materials/brown.mhmat` is relative to the .mhclo, so this
    // also pins that the loader resolved it rather than storing it verbatim.
    INFO("materialFile: " << proxy->materialFile.string());
    REQUIRE_FALSE(proxy->materialFile.empty());
    REQUIRE(fs::exists(proxy->materialFile));

    const auto mat = core::loadMaterial(proxy->materialFile);
    REQUIRE(mat.has_value());
    CHECK(mat->desc().name == "Eye_brown");

    // And the body's own material is a different one, so an export carrying
    // both can be told apart.
    const auto skin = core::loadMaterial(fs::path(MH_DATA_DIR) / "skins" / "default.mhmat");
    REQUIRE(skin.has_value());
    CHECK(skin->desc().name == "DefaultSkin");
    CHECK(skin->desc().name != mat->desc().name);
}

// The whole point of per-entry materials: a dressed export must distinguish the
// body from what it wears. Before this the writers accepted a material per
// entry but the app passed none, so everything shared one default -- the path
// was exercised only by synthetic tests.
TEST_CASE("a dressed export carries a material per mesh", "[render][proxy]") {
    core::Mesh mesh = body();
    const auto proxy =
        core::loadProxy(fs::path(MH_DATA_DIR) / "eyes" / "high-poly" / "high-poly.mhclo");
    REQUIRE(proxy.has_value());

    auto eyeMesh = core::loadObj(proxy->objFile);
    REQUIRE(eyeMesh.has_value());
    std::vector<foundation::Vec3> fitted;
    REQUIRE(core::fitProxy(*proxy, mesh.coord(), fitted));
    REQUIRE(eyeMesh->setCoords(std::move(fitted)).has_value());

    const auto skin   = core::loadMaterial(fs::path(MH_DATA_DIR) / "skins" / "default.mhmat");
    const auto eyeMat = core::loadMaterial(proxy->materialFile);
    REQUIRE(skin.has_value());
    REQUIRE(eyeMat.has_value());
    const auto skinDesc = skin->desc();
    const auto eyeDesc  = eyeMat->desc();

    const auto out = fs::temp_directory_path() / "mh_dressed_materials.obj";
    const auto mtl = fs::temp_directory_path() / "mh_dressed_materials.mtl";
    fs::remove(out);
    fs::remove(mtl);
    REQUIRE(io::writeObjScene(out, {{{mesh.view(), "body", &skinDesc, {}},
                                     {eyeMesh->view(), "eyes", &eyeDesc, {}}}})
                .has_value());

    std::ifstream in(mtl);
    std::ostringstream ss;
    ss << in.rdbuf();
    const std::string text = ss.str();
    INFO(text);
    // Two distinct materials, named from the shipped .mhmat files rather than
    // from a default the exporter invented.
    CHECK(text.find("newmtl DefaultSkin") != std::string::npos);
    CHECK(text.find("newmtl Eye_brown") != std::string::npos);

    std::error_code ec;
    fs::remove(out, ec);
    fs::remove(mtl, ec);
}
