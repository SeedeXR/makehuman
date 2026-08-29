// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Parity of target parsing and application against the Python reference.
//
// Regenerate with:
//     ./.venv-mh/bin/python tools/capture_fixture.py targets

#include "makehuman/core/ObjReader.h"
#include "makehuman/core/Target.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

using namespace mh::core;

namespace {

std::filesystem::path fixtureDir() {
    return std::filesystem::path(MH_GOLDEN_DIR) / "targets";
}

template <typename T>
std::vector<T> readBlob(const std::filesystem::path& p) {
    std::ifstream in(p, std::ios::binary | std::ios::ate);
    if (!in) return {};
    const auto bytes = static_cast<size_t>(in.tellg());
    in.seekg(0);
    std::vector<T> out(bytes / sizeof(T));
    in.read(reinterpret_cast<char*>(out.data()), static_cast<std::streamsize>(bytes));
    return out;
}

bool present() {
    return std::filesystem::exists(fixtureDir() / "applied_coord.bin");
}

/// The 24 sampled targets, in the order the fixture applied them. Taken from
/// tests/golden/targets/sample.json; the ORDER matters because application is
/// a sequence of additive updates.
std::vector<std::string> sampleOrder() {
    std::vector<std::string> out;
    std::ifstream in(fixtureDir() / "sample.json");
    if (!in) return out;
    std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());

    // Minimal extraction of the "path" values, in file order.
    constexpr std::string_view key = "\"path\": \"";
    size_t pos                     = 0;
    while ((pos = text.find(key, pos)) != std::string::npos) {
        pos += key.size();
        const size_t end = text.find('"', pos);
        if (end == std::string::npos) break;
        out.push_back(text.substr(pos, end - pos));
        pos = end;
    }
    return out;
}

/// Fixture blobs are named after the target path with '/' replaced by '_'.
std::string blobStem(std::string rel) {
    std::ranges::replace(rel, '/', '_');
    const std::string suffix = ".target";
    if (rel.size() > suffix.size() && rel.ends_with(suffix)) {
        rel.resize(rel.size() - suffix.size());
    }
    return rel;
}

}  // namespace

TEST_CASE("parsed target indices and offsets match the reference",
          "[golden][parity][fixture][target]") {
    if (!present()) SKIP("run tools/capture_fixture.py targets first");

    const auto sample = sampleOrder();
    REQUIRE(sample.size() == 24);

    size_t compared = 0;
    for (const std::string& rel : sample) {
        const auto vertsFile = fixtureDir() / (blobStem(rel) + ".verts.bin");
        const auto dataFile  = fixtureDir() / (blobStem(rel) + ".data.bin");
        if (!std::filesystem::exists(vertsFile)) continue;

        // The fixture records paths relative to legacy/python/, which is where
        // data/ is symlinked from; resolve against the real data root.
        const auto path = std::filesystem::path(MH_DATA_DIR).parent_path() / rel;
        const auto t    = loadTarget(path);
        REQUIRE(t.has_value());

        const auto expectedVerts = readBlob<uint32_t>(vertsFile);
        const auto expectedData  = readBlob<float>(dataFile);

        INFO(rel);
        REQUIRE(expectedVerts.size() == t->size());
        CHECK(std::equal(expectedVerts.begin(), expectedVerts.end(), t->verts.begin()));

        REQUIRE(expectedData.size() == t->size() * 3);
        size_t bad = 0;
        for (size_t i = 0; i < t->size(); ++i) {
            if (std::abs(t->offsets[i].x - expectedData[i * 3 + 0]) > 1e-7F) ++bad;
            if (std::abs(t->offsets[i].y - expectedData[i * 3 + 1]) > 1e-7F) ++bad;
            if (std::abs(t->offsets[i].z - expectedData[i * 3 + 2]) > 1e-7F) ++bad;
        }
        CHECK(bad == 0);
        ++compared;
    }
    CHECK(compared == 24);
}

TEST_CASE("applying the sampled target stack matches the reference",
          "[golden][parity][fixture][target]") {
    if (!present()) SKIP("run tools/capture_fixture.py targets first");

    const auto objPath = std::filesystem::path(MH_DATA_DIR) / "3dobjs" / "base.obj";
    if (!std::filesystem::exists(objPath)) SKIP("base.obj not present");

    auto mesh = loadObj(objPath);
    REQUIRE(mesh.has_value());

    // The fixture reset to orig_coord, then applied every sampled target at
    // 0.5 in listed order. Application is additive, so the order matters.
    mesh->resetToOriginal();

    const auto sample = sampleOrder();
    REQUIRE(sample.size() == 24);
    for (const std::string& rel : sample) {
        const auto path = std::filesystem::path(MH_DATA_DIR).parent_path() / rel;
        const auto t    = loadTarget(path);
        REQUIRE(t.has_value());
        applyTarget(*t, *mesh, 0.5F);
    }

    const auto expected = readBlob<float>(fixtureDir() / "applied_coord.bin");
    REQUIRE(expected.size() == mesh->vertexCount() * 3);

    size_t bad  = 0;
    float worst = 0.0F;
    for (size_t i = 0; i < mesh->vertexCount(); ++i) {
        const Vec3& got = mesh->coord()[i];
        const float d =
            std::max({std::abs(got.x - expected[i * 3 + 0]), std::abs(got.y - expected[i * 3 + 1]),
                      std::abs(got.z - expected[i * 3 + 2])});
        worst = std::max(worst, d);
        if (d > 1e-5F) ++bad;
    }
    INFO("worst delta " << worst);
    CHECK(bad == 0);
}
