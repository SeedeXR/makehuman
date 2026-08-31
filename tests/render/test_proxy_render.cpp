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

// --- Depth ordering ---------------------------------------------------------
//
// The renderer never hand-rolls a projection: it takes Qt RHI's own
// `clipSpaceCorrMatrix()` (`SceneResources.cpp:273`), which supplies the depth
// convention for whichever backend is live -- [0,1] on Metal, not OpenGL's
// [-1,1]. Nothing tested that, and an inverted or ignored depth range is the
// classic way it breaks: everything still renders, so a "does it draw" test
// passes while the picture is wrong.
//
// The eye proxy is the ideal probe because it sits ENTIRELY INSIDE the skull.
// Viewed from behind, not one of its pixels may reach the image. If depth were
// inverted the eyes would punch straight through the back of the head.
TEST_CASE("geometry inside the head is hidden from behind", "[render][proxy][depth]") {
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

    const auto eyeLit = fs::path(MH_DATA_DIR) / "litspheres" / "skinmat_eye.png";
    const std::vector<render::MeshInstance> bodyOnly{{bodyRm.view(), fullFigure().litsphere}};
    const std::vector<render::MeshInstance> dressed{{bodyRm.view(), fullFigure().litsphere},
                                                    {eyeRm.view(), eyeLit}};

    auto behind              = fullFigure();
    behind.camera.yawDegrees = 180.0F;

    const auto plain    = (*r)->render(bodyOnly, behind);
    const auto withEyes = (*r)->render(dressed, behind);
    REQUIRE(plain.has_value());
    REQUIRE(withEyes.has_value());

    const size_t changed = differingPixels(*plain, *withEyes);
    INFO("from behind, adding the eyes changes " << changed << " pixels");
    CHECK(changed == 0);

    // The same comparison from the FRONT must differ, or the test above would
    // also pass with a renderer that had simply stopped drawing the proxy.
    const auto front      = fullFigure();
    const auto frontPlain = (*r)->render(bodyOnly, front);
    const auto frontEyes  = (*r)->render(dressed, front);
    REQUIRE(frontPlain.has_value());
    REQUIRE(frontEyes.has_value());
    const size_t frontChanged = differingPixels(*frontPlain, *frontEyes);
    INFO("from the front, adding the eyes changes " << frontChanged << " pixels");
    CHECK(frontChanged > 0);
}

// --- Litsphere parity, guarded at the source ---------------------------------
//
// The lit-sphere shader is a translation of the reference's AGPL GLSL. Three
// things in it are load-bearing and each is a single token that a well-meaning
// edit would "clean up":
//
//   * `0.495`, not 0.5, when mapping the normal into litsphere UV space;
//   * the `2.0 - mean(shading)` brightness term, which is not a normalisation;
//   * sampling the RAW interpolated normal -- the reference does NOT
//     renormalize per fragment (`litsphere_fragment_shader.txt:78`).
//
// No rendered-image test can defend these: every variant still produces a
// plausible lit figure, which is exactly why the divergence survived. So the
// guard is a literal source check, the same trade `.mhmat` writing makes
// (project_context.md §8.0).
//
// Measured before choosing parity: renormalizing changes 0.98% of the frame by
// up to 107/255 in a channel.
TEST_CASE("the litsphere shader keeps the reference's exact terms", "[render][litsphere]") {
    const auto path = fs::path(MH_SHADER_SRC_DIR) / "litsphere.frag";
    REQUIRE(fs::exists(path));
    std::ifstream in(path);
    REQUIRE(in);
    const std::string src((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());

    // Strip comments so the header's prose cannot satisfy these checks.
    std::string code;
    code.reserve(src.size());
    for (size_t i = 0; i < src.size();) {
        if (src.compare(i, 2, "//") == 0) {
            while (i < src.size() && src[i] != '\n')
                ++i;
        } else {
            code.push_back(src[i++]);
        }
    }

    INFO("shader body:\n" << code);
    CHECK(code.find("vec3(0.495)") != std::string::npos);
    CHECK(code.find("2.0 - (shading.r + shading.g + shading.b) / 3.0") != std::string::npos);
    // The reference does not renormalize on the no-map path; neither may we.
    //
    // This checks the ASSIGNMENT, not any use of normalize(vNormal): the
    // normal-map branch legitimately normalizes to build an orthonormal TBN
    // basis. A blanket ban was the first version of this test and it started
    // failing the moment normal mapping arrived -- for a correct change.
    CHECK(code.find("normal = normalize(vNormal)") == std::string::npos);
    CHECK(code.find("normal = vNormal") != std::string::npos);
}
