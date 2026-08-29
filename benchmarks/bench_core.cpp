// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Compares mh::core against the measured Python baseline in
// benchmarks/baseline_python.json (see memory/project_context.md section 6).

#include "makehuman/core/Modifier.h"
#include "makehuman/core/ObjReader.h"
#include "makehuman/core/RenderMesh.h"
#include "makehuman/core/Subdivider.h"
#include "makehuman/core/Target.h"
#include "makehuman/core/TargetIndex.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <span>
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

    // Catmull-Clark: reference build 202.30 ms, update_coords 7.64 ms,
    // calcNormals on the subdivided mesh 20.57 ms.
    results.push_back({"Subdivider::build (Catmull-Clark topology+geometry)",
                       medianMs([&] { (void)mh::core::Subdivider::build(*mesh); }, 5), 202.30});

    auto sd = mh::core::Subdivider::build(*mesh);
    if (sd) {
        results.push_back({"Subdivider::refresh (geometry + normals)",
                           medianMs([&] { sd->refresh(*mesh); }, 10), 7.64 + 20.57});
    }

    // Targets: the reference measured 106.22 ms to text-parse 200, 4.86 ms to
    // apply 200 at 0.5, and 0.04 ms for a single application.
    std::vector<std::filesystem::path> targetPaths;
    {
        const auto dir = std::filesystem::path(MH_DATA_DIR) / "targets";
        if (std::filesystem::exists(dir)) {
            for (const auto& e : std::filesystem::recursive_directory_iterator(dir)) {
                if (e.path().extension() == ".target") targetPaths.push_back(e.path());
            }
            std::ranges::sort(targetPaths);
        }
    }

    std::vector<mh::core::Target> loaded;
    if (targetPaths.size() >= 200) {
        const std::span<const std::filesystem::path> first200{targetPaths.data(), 200};
        results.push_back({"load 200 targets (text parse)",
                           medianMs(
                               [&] {
                                   for (const auto& p : first200)
                                       (void)mh::core::loadTarget(p);
                               },
                               3),
                           106.22});

        for (const auto& p : first200) {
            if (auto t = mh::core::loadTarget(p)) loaded.push_back(std::move(*t));
        }

        results.push_back({"apply 200 targets @0.5 (full stack rebuild)",
                           medianMs(
                               [&] {
                                   mesh->resetToOriginal();
                                   for (const auto& t : loaded)
                                       mh::core::applyTarget(t, *mesh, 0.5F);
                               },
                               20),
                           4.86});

        results.push_back({"apply 1 target (slider delta)",
                           medianMs([&] { mh::core::applyTarget(loaded[0], *mesh, 0.5F); }, 500),
                           0.04});
        mesh->resetToOriginal();

        // The whole shipped set. Measured DIRECTLY from the reference at
        // 3225.63 ms -- not extrapolated from the 200-target figure, which
        // would give ~680 ms and be badly wrong: the first 200 targets hold
        // 196,644 sparse entries while all 1,280 hold 6,147,800 (31x the data
        // for 6.4x the files, because macrodetails alone is 106 MB of 126 MB).
        results.push_back({"load ALL 1280 targets (text parse)",
                           medianMs(
                               [&] {
                                   for (const auto& p : targetPaths)
                                       (void)mh::core::loadTarget(p);
                               },
                               2),
                           3225.63});
    }

    // Index all 1,280 targets by filename. The reference walks the tree with
    // TargetsCrawler; not separately timed there, so no baseline.
    results.push_back({"TargetIndex::build (index 1280 targets)",
                       medianMs([&] { (void)mh::core::TargetIndex::build(MH_DATA_DIR); }, 3), 0.0});

    // The full character rebuild: reset to the morph base and replay the stack.
    // The reference's equivalent is applyAllTargets (human.py:1147-1209).
    {
        const auto tIdx = mh::core::TargetIndex::build(MH_DATA_DIR);
        std::vector<mh::core::Modifier> mods;
        for (const char* f : {"modeling_modifiers.json", "measurement_modifiers.json",
                              "bodyshapes_modifiers.json"}) {
            auto m = mh::core::loadModifiers(std::filesystem::path(MH_DATA_DIR) / "modifiers" / f);
            if (m) mods.insert(mods.end(), m->begin(), m->end());
        }
        if (tIdx.componentCount() > 0 && !mods.empty()) {
            results.push_back(
                {"loadModifiers (291 modifiers)",
                 medianMs(
                     [&] {
                         for (const char* f :
                              {"modeling_modifiers.json", "measurement_modifiers.json",
                               "bodyshapes_modifiers.json"}) {
                             (void)mh::core::loadModifiers(std::filesystem::path(MH_DATA_DIR) /
                                                           "modifiers" / f);
                         }
                     },
                     5),
                 0.0});

            mh::core::Human human(&tIdx, mods);
            results.push_back({"Human::rebuildStack (all 291 modifiers)",
                               medianMs([&] { human.rebuildStack(); }, 50), 0.0});

            mh::core::TargetLibrary lib(MH_DATA_DIR);
            human.applyStack(*mesh, lib);  // warm the target cache
            results.push_back({"Human::applyStack (character rebuild)",
                               medianMs([&] { human.applyStack(*mesh, lib); }, 100), 0.0});
            mesh->resetToOriginal();
        }
    }

    auto rm = mh::core::RenderMesh::build(*mesh);
    results.push_back({"RenderMesh::refreshPositions (morph hot path)",
                       medianMs([&] { rm.refreshPositions(*mesh); }, 50), 0.0});

    if (sd) {
        std::printf("subdiv: %zu verts, %zu faces, %zu edges\n", sd->mesh().vertexCount(),
                    sd->mesh().faceCount(), sd->edgeCount());
    }
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
