// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Renders the base mesh offscreen and reports what actually landed in the
// pixels. Separate from the test binary because it writes an image for a human
// to look at; the statistics it prints are what makes "it renders" checkable
// without looking.
#include "makehuman/core/ObjReader.h"
#include "makehuman/core/RenderMesh.h"
#include "makehuman/render/OffscreenRenderer.h"
#include "makehuman/render/Picking.h"

#include <QGuiApplication>
#include <QImage>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <filesystem>

int main(int argc, char** argv) {
    QGuiApplication app(argc, argv);

    const std::filesystem::path shaderDir = (argc > 1) ? argv[1] : "build/shaders";
    const std::filesystem::path outPath   = (argc > 2) ? argv[2] : "render.png";

    auto mesh = mh::core::loadObj(std::filesystem::path(MH_DATA_DIR) / "3dobjs" / "base.obj");
    if (!mesh) {
        std::fprintf(stderr, "mesh: %s\n", mesh.error().message().c_str());
        return 1;
    }
    mesh->buildAdjacency();
    mesh->calcNormals();
    auto rm = mh::core::RenderMesh::build(*mesh);

    // base.obj is 138 parts helper geometry to 1 part body. Drawing it raw
    // produces a figure in a solid skirt with a box over its face.
    const auto visible = mesh->staticFaceMask();
    if (!rm.setFaceMask(*mesh, visible)) {
        std::fprintf(stderr, "could not apply the static face mask\n");
        return 1;
    }
    const size_t shown =
        static_cast<size_t>(std::count(visible.begin(), visible.end(), uint8_t{1}));
    std::printf("faces: %zu of %zu visible (helpers hidden)\n", shown, mesh->faceCount());

    auto r = mh::render::OffscreenRenderer::create(shaderDir);
    if (!r) {
        std::fprintf(stderr, "renderer: %s\n", r.error().message().c_str());
        return 1;
    }
    std::printf("backend: %s\n", (*r)->backendName().c_str());

    mh::render::RenderSettings s;
    s.width     = 640;
    s.height    = 640;
    s.litsphere = std::filesystem::path(MH_DATA_DIR) / "litspheres" / "skinmat_caucasian.png";

    // Optional: focus on the point under a pixel first, the way a double-click
    // does in the viewport. This is what makes the picking maths checkable by
    // LOOKING -- the picked point must end up in the middle of the frame, and
    // no assertion about pan proves that as directly as the image does.
    if (argc > 4) {
        const int px   = std::atoi(argv[3]);
        const int py   = std::atoi(argv[4]);
        const auto hit = mh::render::intersect(
            rm.view(), mh::render::rayThroughPixel(s.camera, px, py, s.width, s.height));
        if (!hit) {
            std::fprintf(stderr, "no hit at %d,%d\n", px, py);
            return 1;
        }
        std::printf("focus: pixel %d,%d hits (%.4f, %.4f, %.4f)\n", px, py,
                    static_cast<double>(hit->x), static_cast<double>(hit->y),
                    static_cast<double>(hit->z));
        mh::render::focusOn(s.camera, *hit);
        std::printf("focus: pan now (%.4f, %.4f)\n", static_cast<double>(s.camera.panX),
                    static_cast<double>(s.camera.panY));
    }

    auto img = (*r)->render(rm.view(), s);
    if (!img) {
        std::fprintf(stderr, "render: %s\n", img.error().message().c_str());
        return 1;
    }

    // Statistics, not a screenshot: a blank frame is the failure mode that
    // looks like success in a log.
    const QColor bg  = QColor::fromRgbF(s.background.x, s.background.y, s.background.z);
    size_t nonBg     = 0;
    size_t total     = 0;
    long long lumSum = 0;
    int minLum       = 255;
    int maxLum       = 0;
    for (int y = 0; y < img->height(); ++y) {
        for (int x = 0; x < img->width(); ++x) {
            const QColor c = img->pixelColor(x, y);
            ++total;
            const int lum = qGray(c.rgb());
            if (std::abs(c.red() - bg.red()) > 6 || std::abs(c.green() - bg.green()) > 6 ||
                std::abs(c.blue() - bg.blue()) > 6) {
                ++nonBg;
                lumSum += lum;
                minLum = std::min(minLum, lum);
                maxLum = std::max(maxLum, lum);
            }
        }
    }

    if (!img->save(QString::fromStdString(outPath.string()))) {
        std::fprintf(stderr, "could not write %s\n", outPath.c_str());
        return 1;
    }

    std::printf("wrote %s (%dx%d)\n", outPath.c_str(), img->width(), img->height());
    std::printf("covered %zu of %zu pixels (%.1f%%)\n", nonBg, total,
                100.0 * static_cast<double>(nonBg) / static_cast<double>(total));
    if (nonBg > 0) {
        std::printf("luminance min=%d max=%d mean=%.1f\n", minLum, maxLum,
                    static_cast<double>(lumSum) / static_cast<double>(nonBg));
    }
    return nonBg > 0 ? 0 : 2;  // nothing drawn is a failure, not a quiet success
}
