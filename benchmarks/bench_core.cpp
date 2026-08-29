// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Compares mh::core against the measured Python baseline in
// benchmarks/baseline_python.json (see memory/project_context.md section 6).

#include "makehuman/core/ObjReader.h"
#include "makehuman/core/RenderMesh.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

namespace {

using Clock = std::chrono::steady_clock;

struct Result {
    std::string label;
    double medianMs{};
    double baselineMs{};
};

template <typename Fn>
double medianMs(Fn&& fn, int repeat) {
    std::vector<double> samples;
    samples.reserve(static_cast<size_t>(repeat));
    for (int i = 0; i < repeat; ++i) {
        const auto t0 = Clock::now();
        fn();
        const auto t1 = Clock::now();
        samples.push_back(std::chrono::duration<double, std::milli>(t1 - t0).count());
    }
    std::ranges::sort(samples);
    return samples[samples.size() / 2];
}

}  // namespace

int main() {
    const std::filesystem::path base = std::filesystem::path(MH_DATA_DIR) / "3dobjs" / "base.obj";
    if (!std::filesystem::exists(base)) {
        std::fprintf(stderr, "base.obj not found at %s\n", base.string().c_str());
        return 1;
    }

    std::vector<Result> results;

    // Baselines are the medians measured from the Python reference on
    // 2026-08-29 (macOS 26.6.2 arm64, Python 3.14.6, numpy 2.5.1).
    results.push_back({"load base.obj (parse + adjacency + normals)",
                       medianMs([&] { (void)mh::core::loadObj(base); }, 5), 211.8});

    auto mesh = mh::core::loadObj(base);
    if (!mesh) {
        std::fprintf(stderr, "load failed: %s\n", mesh.error().message().c_str());
        return 1;
    }

    results.push_back({"calcNormals full mesh", medianMs([&] { mesh->calcNormals(); }, 20), 5.18});
    results.push_back(
        {"calcFaceNormals only", medianMs([&] { mesh->calcFaceNormals(); }, 50), 0.68});
    results.push_back(
        {"calcVertexNormals only", medianMs([&] { mesh->calcVertexNormals(); }, 50), 1.69});
    results.push_back({"buildAdjacency", medianMs([&] { mesh->buildAdjacency(); }, 10), 0.0});
    results.push_back(
        {"calcVertexTangents", medianMs([&] { mesh->calcVertexTangents(); }, 20), 0.0});

    // The reference's nearest equivalent is updateIndexBuffer (unweld + group
    // sort) at 3.43 ms -- but it does not triangulate, so ours does more work.
    results.push_back({"RenderMesh::build (unweld + triangulate + sort)",
                       medianMs([&] { (void)mh::core::RenderMesh::build(*mesh); }, 10), 3.43});

    auto rm = mh::core::RenderMesh::build(*mesh);
    results.push_back({"RenderMesh::refreshPositions (morph hot path)",
                       medianMs([&] { rm.refreshPositions(*mesh); }, 50), 0.0});

    std::printf("render: %zu verts (unwelded), %zu indices, %zu groups\n", rm.vertexCount(),
                rm.indexCount(), rm.groupRanges().size());
    std::printf("mesh: %zu verts, %zu faces, %zu uvs, maxValence %u\n\n", mesh->vertexCount(),
                mesh->faceCount(), mesh->uvCount(), static_cast<unsigned>(mesh->maxValence()));
    std::printf("%-46s %12s %12s %10s\n", "operation", "C++ (ms)", "python (ms)", "speedup");
    std::printf("%s\n", std::string(84, '-').c_str());
    for (const Result& r : results) {
        if (r.baselineMs > 0.0) {
            std::printf("%-46s %11.2f %11.2f %9.1fx\n", r.label.c_str(), r.medianMs, r.baselineMs,
                        r.baselineMs / r.medianMs);
        } else {
            std::printf("%-46s %11.2f %11s %10s\n", r.label.c_str(), r.medianMs, "-", "-");
        }
    }
    std::printf("\nbaseline: benchmarks/baseline_python.json (2026-08-29)\n");
    return 0;
}
