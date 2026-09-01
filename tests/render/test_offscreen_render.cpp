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
#include "makehuman/render/SceneResources.h"

#include <rhi/qrhi.h>

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <vector>

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
/// Pixels that differ between two renders of the same size.
size_t differingPixels(const QImage& a, const QImage& b) {
    if (a.size() != b.size()) return SIZE_MAX;
    size_t n = 0;
    for (int y = 0; y < a.height(); ++y)
        for (int x = 0; x < a.width(); ++x)
            if (a.pixelColor(x, y) != b.pixelColor(x, y)) ++n;
    return n;
}

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

/// Fraction of pixels in [x0, x1) that differ from the background. The column
/// range matters for multi-mesh: whole-image coverage cannot tell "both meshes
/// drew" apart from "one mesh drew twice as much".
double coverage(const QImage& img, const render::RenderSettings& s, int x0, int x1) {
    const QColor bg = QColor::fromRgbF(s.background.x, s.background.y, s.background.z);
    size_t hit      = 0;
    for (int y = 0; y < img.height(); ++y) {
        for (int x = x0; x < x1; ++x) {
            const QColor c = img.pixelColor(x, y);
            if (std::abs(c.red() - bg.red()) > 6 || std::abs(c.green() - bg.green()) > 6 ||
                std::abs(c.blue() - bg.blue()) > 6) {
                ++hit;
            }
        }
    }
    return static_cast<double>(hit) / static_cast<double>((x1 - x0) * img.height());
}

double coverage(const QImage& img, const render::RenderSettings& s) {
    return coverage(img, s, 0, img.width());
}

/// The same geometry shifted along X, so two of them can be drawn side by side.
/// Owns only the coordinates; everything else is shared with @p src, which must
/// outlive the result.
struct Shifted {
    std::vector<foundation::Vec3> coord;
    foundation::RenderView view;
};

Shifted shiftedBy(const foundation::RenderView& src, float dx) {
    Shifted out;
    out.coord.assign(src.coord.begin(), src.coord.end());
    for (auto& c : out.coord)
        c.x += dx;
    out.view = foundation::RenderView{out.coord, src.texco, src.vnorm, src.vtang, src.index};
    return out;
}

}  // namespace

// Multi-mesh is what unblocks the eight proxy choosers: a dressed character is
// the body plus every worn proxy, drawn together. Until this works the viewport
// can only ever show a naked body.
TEST_CASE("two meshes both draw", "[render][multimesh]") {
    requireDevice();
    auto r = render::OffscreenRenderer::create(MH_SHADER_DIR);
    REQUIRE(r.has_value());

    const Scene sc  = bodyScene();
    const auto lit  = settings().litsphere;
    const Shifted a = shiftedBy(sc.rm.view(), -6.0F);
    const Shifted b = shiftedBy(sc.rm.view(), 6.0F);

    const auto s = settings();
    const std::vector<render::MeshInstance> both{{a.view, lit}, {b.view, lit}};
    const auto img = (*r)->render(both, s);
    REQUIRE(img.has_value());

    // Each half must carry a body. Drawing only the first mesh, or drawing the
    // second over the first, leaves one half empty -- which a whole-image
    // coverage check would not notice.
    const double left  = coverage(*img, s, 0, img->width() / 2);
    const double right = coverage(*img, s, img->width() / 2, img->width());
    INFO("left " << left << " right " << right);
    CHECK(left > 0.02);
    CHECK(right > 0.02);

    // And together they must exceed either one alone.
    const std::vector<render::MeshInstance> justOne{{a.view, lit}};
    const auto one = (*r)->render(justOne, s);
    REQUIRE(one.has_value());
    CHECK(coverage(*img, s) > coverage(*one, s) * 1.5);
}

// The failure this guards against is subtle and likely: per-mesh textures
// collapsing into one shared binding, so every mesh renders with whichever
// litsphere was uploaded last. Coverage cannot see that -- only colour can.
TEST_CASE("each mesh keeps its own litsphere", "[render][multimesh]") {
    requireDevice();
    auto r = render::OffscreenRenderer::create(MH_SHADER_DIR);
    REQUIRE(r.has_value());

    const auto litDir = std::filesystem::path(MH_DATA_DIR) / "litspheres";
    const auto skin   = litDir / "skinmat_caucasian.png";
    const auto other  = litDir / "skinmat_african.png";
    REQUIRE(std::filesystem::exists(other));

    const Scene sc  = bodyScene();
    const Shifted a = shiftedBy(sc.rm.view(), -6.0F);
    const Shifted b = shiftedBy(sc.rm.view(), 6.0F);
    const auto s    = settings();

    const std::vector<render::MeshInstance> mixed{{a.view, skin}, {b.view, other}};
    const std::vector<render::MeshInstance> same{{a.view, skin}, {b.view, skin}};
    const auto mixedImg = (*r)->render(mixed, s);
    const auto sameImg  = (*r)->render(same, s);
    REQUIRE(mixedImg.has_value());
    REQUIRE(sameImg.has_value());

    // The LEFT body uses the same litsphere in both renders, so it must match;
    // the RIGHT one differs. If the binding were shared, both halves would
    // change together.
    size_t leftDiff  = 0;
    size_t rightDiff = 0;
    for (int y = 0; y < mixedImg->height(); ++y) {
        for (int x = 0; x < mixedImg->width(); ++x) {
            if (mixedImg->pixelColor(x, y) != sameImg->pixelColor(x, y)) {
                (x < mixedImg->width() / 2 ? leftDiff : rightDiff)++;
            }
        }
    }
    INFO("left differing " << leftDiff << " right differing " << rightDiff);
    CHECK(leftDiff == 0);
    CHECK(rightDiff > 500);
}

TEST_CASE("an empty mesh among several is reported", "[render][multimesh]") {
    requireDevice();
    auto r = render::OffscreenRenderer::create(MH_SHADER_DIR);
    REQUIRE(r.has_value());

    const Scene sc = bodyScene();
    const std::vector<render::MeshInstance> withEmpty{
        {sc.rm.view(), settings().litsphere},
        {foundation::RenderView{}, settings().litsphere},
    };
    const auto img = (*r)->render(withEmpty, settings());
    REQUIRE_FALSE(img.has_value());
    CHECK(img.error().kind == render::RenderErrorKind::EmptyMesh);
}

// A batch holds raw pointers and never learns that a resource died. Queueing
// uploads inside the per-mesh loop and then failing on a later mesh therefore
// handed the caller a batch referencing freed buffers -- and ViewportWidget
// submits its batch even when upload fails (ViewportWidget.cpp: the error is
// reported but not returned on), so this crashed in the Metal backend.
//
// The offscreen renderer cannot reproduce it: it abandons the batch on failure.
// So this drives QRhi directly, the way the widget does.
TEST_CASE("a batch submitted after a failed upload does not use freed resources",
          "[render][multimesh]") {
    requireDevice();

    QRhiMetalInitParams params;
    std::unique_ptr<QRhi> rhi(QRhi::create(QRhi::Metal, &params));
    REQUIRE(rhi);

    const QSize size(64, 64);
    std::unique_ptr<QRhiTexture> colour(
        rhi->newTexture(QRhiTexture::RGBA8, size, 1, QRhiTexture::RenderTarget));
    REQUIRE(colour->create());
    std::unique_ptr<QRhiRenderBuffer> depth(
        rhi->newRenderBuffer(QRhiRenderBuffer::DepthStencil, size, 1));
    REQUIRE(depth->create());

    QRhiTextureRenderTargetDescription rtDesc;
    rtDesc.setColorAttachments({QRhiColorAttachment(colour.get())});
    rtDesc.setDepthStencilBuffer(depth.get());
    std::unique_ptr<QRhiTextureRenderTarget> rt(rhi->newTextureRenderTarget(rtDesc));
    std::unique_ptr<QRhiRenderPassDescriptor> rp(rt->newCompatibleRenderPassDescriptor());
    rt->setRenderPassDescriptor(rp.get());
    REQUIRE(rt->create());

    auto scene = render::SceneResources::create(rhi.get(), rp.get(), MH_SHADER_DIR, 1);
    REQUIRE(scene.has_value());

    const Scene sc = bodyScene();
    // Mesh 0 is fine and would have been queued; mesh 1 fails. Order matters:
    // the bug needs a success BEFORE the failure.
    const std::vector<render::MeshInstance> meshes{
        {sc.rm.view(), settings().litsphere},
        {sc.rm.view(), "/definitely/not/a/file.png"},
    };

    QRhiCommandBuffer* cb = nullptr;
    REQUIRE(rhi->beginOffscreenFrame(&cb) == QRhi::FrameOpSuccess);

    QRhiResourceUpdateBatch* u = rhi->nextResourceUpdateBatch();
    const auto ok              = (*scene)->upload(u, meshes);
    CHECK_FALSE(ok.has_value());
    CHECK(ok.error().kind == render::RenderErrorKind::TextureMissing);

    // Submit it anyway. Before the fix this dereferenced freed buffers inside
    // QRhiMetal::enqueueResourceUpdates and crashed under ASan.
    cb->beginPass(rt.get(), Qt::black, {1.0F, 0}, u);
    (*scene)->draw(cb, size);
    cb->endPass();
    rhi->endOffscreenFrame();

    SUCCEED("submitted a batch after a failed upload without touching freed resources");
}

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

// --- Per-mesh diffuse (skin albedo) -----------------------------------------
//
// Until now every mesh sampled ONE shared 1x1 white diffuse, so a skin texture
// could not be displayed at all: `data/skins/default.mhmat` names no texture
// (`shaderConfig diffuse false`) and `data/textures/` holds only
// `texture_notfound.png`. Adding varied human skin tones needs this path first.
//
// No skin texture ships, so the fixture is generated here rather than imported
// -- which also keeps the test independent of any third-party asset licence.
TEST_CASE("a per-mesh diffuse map changes the render", "[render][diffuse]") {
    requireDevice();
    auto r = render::OffscreenRenderer::create(MH_SHADER_DIR);
    REQUIRE(r.has_value());

    const Scene sc = bodyScene();
    const auto s   = settings();

    const auto texPath = std::filesystem::temp_directory_path() / "mh_test_skin.png";
    QImage tex(64, 64, QImage::Format_RGBA8888);
    tex.fill(QColor(40, 200, 90));  // nothing like a litsphere, so it cannot be confused
    REQUIRE(tex.save(QString::fromStdString(texPath.string())));

    const std::vector<render::MeshInstance> plain{{sc.rm.view(), s.litsphere}};
    const std::vector<render::MeshInstance> textured{{sc.rm.view(), s.litsphere, texPath}};

    const auto a = (*r)->render(plain, s);
    const auto b = (*r)->render(textured, s);
    REQUIRE(a.has_value());
    REQUIRE(b.has_value());

    const size_t changed = differingPixels(*a, *b);
    INFO("a diffuse map changes " << changed << " pixels");
    CHECK(changed > 1000);  // the body covers far more than this

    std::filesystem::remove(texPath);
}

TEST_CASE("each mesh keeps its own diffuse map", "[render][diffuse]") {
    requireDevice();
    auto r = render::OffscreenRenderer::create(MH_SHADER_DIR);
    REQUIRE(r.has_value());

    const Scene sc = bodyScene();
    const auto s   = settings();
    const auto one = std::filesystem::temp_directory_path() / "mh_test_skin_a.png";
    const auto two = std::filesystem::temp_directory_path() / "mh_test_skin_b.png";
    QImage ta(32, 32, QImage::Format_RGBA8888);
    ta.fill(QColor(230, 60, 60));
    QImage tb(32, 32, QImage::Format_RGBA8888);
    tb.fill(QColor(60, 60, 230));
    REQUIRE(ta.save(QString::fromStdString(one.string())));
    REQUIRE(tb.save(QString::fromStdString(two.string())));

    // Two meshes, two different skins. If the diffuse were still shared, both
    // would take whichever uploaded last and these would be identical.
    const std::vector<render::MeshInstance> red{{sc.rm.view(), s.litsphere, one}};
    const std::vector<render::MeshInstance> blue{{sc.rm.view(), s.litsphere, two}};
    const auto ra = (*r)->render(red, s);
    const auto rb = (*r)->render(blue, s);
    REQUIRE(ra.has_value());
    REQUIRE(rb.has_value());
    CHECK(differingPixels(*ra, *rb) > 1000);

    std::filesystem::remove(one);
    std::filesystem::remove(two);
}

TEST_CASE("a diffuse map that will not load is reported", "[render][diffuse]") {
    requireDevice();
    auto r = render::OffscreenRenderer::create(MH_SHADER_DIR);
    REQUIRE(r.has_value());

    // Silently falling back to white would render a plausible untextured body
    // and read as a shading bug rather than a missing file.
    const Scene sc = bodyScene();
    const std::vector<render::MeshInstance> bad{
        {sc.rm.view(), settings().litsphere, "/definitely/not/a/skin.png"}};
    const auto img = (*r)->render(bad, settings());
    REQUIRE_FALSE(img.has_value());
    CHECK(img.error().kind == render::RenderErrorKind::TextureMissing);
}

// --- In-memory litsphere: the autoBlendSkin path ----------------------------
//
// A blended skin tone (`core::blendEthnicLitsphere`) has no file behind it: it
// is computed per character from the three ethnic litspheres. Writing it to a
// temp file just to hand back a path would put disk I/O on the slider-drag
// path, so `MeshInstance` takes decoded RGBA directly.
TEST_CASE("an in-memory litsphere renders and overrides the path", "[render][blendtone]") {
    requireDevice();
    auto r = render::OffscreenRenderer::create(MH_SHADER_DIR);
    REQUIRE(r.has_value());

    const Scene sc = bodyScene();
    const auto s   = settings();

    // A flat tone, nothing like the real litsphere, so "the path was used
    // anyway" cannot pass.
    constexpr int kW = 32;
    constexpr int kH = 32;
    std::vector<uint8_t> tone(static_cast<size_t>(kW) * kH * 4);
    for (size_t i = 0; i < tone.size(); i += 4) {
        tone[i]     = 30;
        tone[i + 1] = 220;
        tone[i + 2] = 120;
        tone[i + 3] = 255;
    }

    render::MeshInstance inMemory;
    inMemory.mesh            = sc.rm.view();
    inMemory.litsphere       = s.litsphere;  // present, and must be IGNORED
    inMemory.litsphereRgba   = tone;
    inMemory.litsphereWidth  = kW;
    inMemory.litsphereHeight = kH;

    const std::vector<render::MeshInstance> fromPath{{sc.rm.view(), s.litsphere}};
    const std::vector<render::MeshInstance> fromMemory{inMemory};

    const auto a = (*r)->render(fromPath, s);
    const auto b = (*r)->render(fromMemory, s);
    REQUIRE(a.has_value());
    REQUIRE(b.has_value());

    const size_t changed = differingPixels(*a, *b);
    INFO("in-memory litsphere changes " << changed << " pixels");
    CHECK(changed > 1000);
}

TEST_CASE("an in-memory litsphere must match its declared size", "[render][blendtone]") {
    requireDevice();
    auto r = render::OffscreenRenderer::create(MH_SHADER_DIR);
    REQUIRE(r.has_value());

    // Reading past the buffer would be a heap overflow, so this is refused
    // rather than trusted. ASan would catch it in the suite; a user would not.
    const Scene sc = bodyScene();
    std::vector<uint8_t> tooSmall(16, 200);

    render::MeshInstance bad;
    bad.mesh            = sc.rm.view();
    bad.litsphereRgba   = tooSmall;
    bad.litsphereWidth  = 64;
    bad.litsphereHeight = 64;

    const std::vector<render::MeshInstance> one{bad};
    const auto img = (*r)->render(one, settings());
    REQUIRE_FALSE(img.has_value());
    CHECK(img.error().kind == render::RenderErrorKind::TextureMissing);
}

// --- Normal maps: where surface detail lives --------------------------------
//
// Pores, wrinkles and fine skin structure are carried by a tangent-space normal
// map, not by the albedo. The mesh already has correct tangents (Lengyel, with
// the reference's three bugs fixed), they were simply never uploaded.
TEST_CASE("a normal map changes shading", "[render][normalmap]") {
    requireDevice();
    auto r = render::OffscreenRenderer::create(MH_SHADER_DIR);
    REQUIRE(r.has_value());

    const Scene sc = bodyScene();
    const auto s   = settings();

    // A map that leans hard off-axis, so the effect cannot be lost in noise.
    const auto path = std::filesystem::temp_directory_path() / "mh_test_normal.png";
    QImage nm(64, 64, QImage::Format_RGBA8888);
    nm.fill(QColor(230, 40, 200));
    REQUIRE(nm.save(QString::fromStdString(path.string())));

    render::MeshInstance mapped;
    mapped.mesh      = sc.rm.view();
    mapped.litsphere = s.litsphere;
    mapped.normalMap = path;

    const std::vector<render::MeshInstance> plain{{sc.rm.view(), s.litsphere}};
    const std::vector<render::MeshInstance> bumpy{mapped};

    const auto a = (*r)->render(plain, s);
    const auto b = (*r)->render(bumpy, s);
    REQUIRE(a.has_value());
    REQUIRE(b.has_value());
    const size_t changed = differingPixels(*a, *b);
    INFO("a normal map changes " << changed << " pixels");
    CHECK(changed > 1000);

    std::filesystem::remove(path);
}

TEST_CASE("normal map intensity scales the effect", "[render][normalmap]") {
    requireDevice();
    auto r = render::OffscreenRenderer::create(MH_SHADER_DIR);
    REQUIRE(r.has_value());

    const Scene sc  = bodyScene();
    const auto s    = settings();
    const auto path = std::filesystem::temp_directory_path() / "mh_test_normal_i.png";
    QImage nm(64, 64, QImage::Format_RGBA8888);
    nm.fill(QColor(230, 40, 200));
    REQUIRE(nm.save(QString::fromStdString(path.string())));

    // Intensity is the reference's `normalmapIntensity` uniform. At a very low
    // value the perturbation nearly vanishes, so full strength must differ from
    // it -- otherwise the uniform is not reaching the shader at all.
    render::MeshInstance strong;
    strong.mesh               = sc.rm.view();
    strong.litsphere          = s.litsphere;
    strong.normalMap          = path;
    strong.normalMapIntensity = 1.0F;

    render::MeshInstance weak = strong;
    weak.normalMapIntensity   = 0.01F;

    const std::vector<render::MeshInstance> hi{strong};
    const std::vector<render::MeshInstance> lo{weak};
    const auto a = (*r)->render(hi, s);
    const auto b = (*r)->render(lo, s);
    REQUIRE(a.has_value());
    REQUIRE(b.has_value());
    const size_t changed = differingPixels(*a, *b);
    INFO("intensity 1.0 vs 0.01 changes " << changed << " pixels");
    CHECK(changed > 1000);

    std::filesystem::remove(path);
}

TEST_CASE("a normal map that will not load is reported", "[render][normalmap]") {
    requireDevice();
    auto r = render::OffscreenRenderer::create(MH_SHADER_DIR);
    REQUIRE(r.has_value());

    const Scene sc = bodyScene();
    render::MeshInstance bad;
    bad.mesh      = sc.rm.view();
    bad.litsphere = settings().litsphere;
    bad.normalMap = "/definitely/not/a/normal.png";

    const std::vector<render::MeshInstance> one{bad};
    const auto img = (*r)->render(one, settings());
    REQUIRE_FALSE(img.has_value());
    CHECK(img.error().kind == render::RenderErrorKind::TextureMissing);
}

// --- Ambient occlusion ------------------------------------------------------
//
// The reference multiplies an AO map over the result AFTER the additive term
// (litsphere_fragment_shader.txt:103-105), not into `shading`. That ordering is
// preserved: folding it in earlier would also scale the additive contribution.
TEST_CASE("an AO map darkens the render", "[render][aomap]") {
    requireDevice();
    auto r = render::OffscreenRenderer::create(MH_SHADER_DIR);
    REQUIRE(r.has_value());

    const Scene sc  = bodyScene();
    const auto s    = settings();
    const auto path = std::filesystem::temp_directory_path() / "mh_test_ao.png";
    QImage ao(32, 32, QImage::Format_RGBA8888);
    ao.fill(QColor(60, 60, 60));  // heavy occlusion, so the effect is unambiguous
    REQUIRE(ao.save(QString::fromStdString(path.string())));

    render::MeshInstance occluded;
    occluded.mesh      = sc.rm.view();
    occluded.litsphere = s.litsphere;
    occluded.aoMap     = path;

    const std::vector<render::MeshInstance> plain{{sc.rm.view(), s.litsphere}};
    const std::vector<render::MeshInstance> dark{occluded};
    const auto a = (*r)->render(plain, s);
    const auto b = (*r)->render(dark, s);
    REQUIRE(a.has_value());
    REQUIRE(b.has_value());
    CHECK(differingPixels(*a, *b) > 1000);

    // AO only ever DARKENS: every body pixel must be no brighter than before.
    // A test that merely saw "the image changed" would pass on a map that lit
    // the model up, which is not what multiplying by an occlusion term does.
    size_t brighter = 0;
    for (int y = 0; y < a->height(); ++y) {
        for (int x = 0; x < a->width(); ++x) {
            const QColor pa = a->pixelColor(x, y);
            const QColor pb = b->pixelColor(x, y);
            if (pb.red() > pa.red() + 1 || pb.green() > pa.green() + 1 ||
                pb.blue() > pa.blue() + 1) {
                ++brighter;
            }
        }
    }
    INFO("pixels that got brighter under AO: " << brighter);
    CHECK(brighter == 0);

    std::filesystem::remove(path);
}

TEST_CASE("an AO map that will not load is reported", "[render][aomap]") {
    requireDevice();
    auto r = render::OffscreenRenderer::create(MH_SHADER_DIR);
    REQUIRE(r.has_value());

    const Scene sc = bodyScene();
    render::MeshInstance bad;
    bad.mesh      = sc.rm.view();
    bad.litsphere = settings().litsphere;
    bad.aoMap     = "/definitely/not/an/ao.png";
    const std::vector<render::MeshInstance> one{bad};
    const auto img = (*r)->render(one, settings());
    REQUIRE_FALSE(img.has_value());
    CHECK(img.error().kind == render::RenderErrorKind::TextureMissing);
}
