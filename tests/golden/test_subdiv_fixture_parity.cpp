// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Byte-level parity of Catmull-Clark against the Python reference.
//
// Matching vertex and face COUNTS says nothing about whether the geometry is
// right -- a sign error or a swapped index space produces the same counts. This
// compares the actual positions, UVs and index arrays.
//
// Regenerate with:
//     ./.venv-mh/bin/python tools/capture_fixture.py subdiv

#include "makehuman/core/ObjReader.h"
#include "makehuman/core/Subdivider.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <optional>
#include <vector>

using namespace mh::core;

namespace {

std::filesystem::path fixtureDir() {
    return std::filesystem::path(MH_GOLDEN_DIR) / "subdiv";
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
    return std::filesystem::exists(fixtureDir() / "coord.bin");
}

/// Positions come from a chain of float32 averages; 1e-5 mesh units is the
/// tolerance stated in memory/test.md section 3.3.
constexpr float kPosTol = 1e-5F;

std::optional<Subdivider> subdivideBaseMesh() {
    const auto path = std::filesystem::path(MH_DATA_DIR) / "3dobjs" / "base.obj";
    if (!std::filesystem::exists(path)) return std::nullopt;
    auto mesh = loadObj(path);
    if (!mesh) return std::nullopt;
    auto sd = Subdivider::build(*mesh);
    if (!sd) return std::nullopt;
    return std::move(*sd);
}

}  // namespace

TEST_CASE("subdivided positions match the Python reference", "[golden][parity][fixture][subdiv]") {
    if (!present()) SKIP("run tools/capture_fixture.py subdiv first");
    const auto sd = subdivideBaseMesh();
    REQUIRE(sd.has_value());

    const auto expected = readBlob<float>(fixtureDir() / "coord.bin");
    REQUIRE(expected.size() == sd->mesh().vertexCount() * 3);

    // Reported per block, because a bug in one rule (face points, edge points,
    // repositioned base points) localises to exactly one range.
    size_t badBase = 0;
    size_t badFace = 0;
    size_t badEdge = 0;
    float worst    = 0.0F;

    for (size_t i = 0; i < sd->mesh().vertexCount(); ++i) {
        const Vec3& got = sd->mesh().coord()[i];
        const float d =
            std::max({std::abs(got.x - expected[i * 3 + 0]), std::abs(got.y - expected[i * 3 + 1]),
                      std::abs(got.z - expected[i * 3 + 2])});
        worst = std::max(worst, d);
        if (d > kPosTol) {
            if (i < sd->faceBase()) {
                ++badBase;
            } else if (i < sd->edgeBase()) {
                ++badFace;
            } else {
                ++badEdge;
            }
        }
    }

    INFO("worst delta " << worst);
    CHECK(badBase == 0);  // repositioned base vertices
    CHECK(badFace == 0);  // face points
    CHECK(badEdge == 0);  // edge points
}

TEST_CASE("subdivided face indices match the Python reference exactly",
          "[golden][parity][fixture][subdiv]") {
    if (!present()) SKIP("run tools/capture_fixture.py subdiv first");
    const auto sd = subdivideBaseMesh();
    REQUIRE(sd.has_value());

    const auto expected = readBlob<uint32_t>(fixtureDir() / "fvert.bin");
    REQUIRE(expected.size() == sd->mesh().fvert().size());
    CHECK(std::equal(expected.begin(), expected.end(), sd->mesh().fvert().begin()));
}

TEST_CASE("subdivided UV indices match the Python reference exactly",
          "[golden][parity][fixture][subdiv]") {
    if (!present()) SKIP("run tools/capture_fixture.py subdiv first");
    const auto sd = subdivideBaseMesh();
    REQUIRE(sd.has_value());

    // The dual index space survives subdivision: UV edges are unique in the UV
    // index space, which is NOT the same partition as the position edges.
    const auto expected = readBlob<uint32_t>(fixtureDir() / "fuvs.bin");
    REQUIRE(expected.size() == sd->mesh().fuvs().size());
    CHECK(std::equal(expected.begin(), expected.end(), sd->mesh().fuvs().begin()));
}

TEST_CASE("subdivided UVs match the Python reference", "[golden][parity][fixture][subdiv]") {
    if (!present()) SKIP("run tools/capture_fixture.py subdiv first");
    const auto sd = subdivideBaseMesh();
    REQUIRE(sd.has_value());

    const auto expected = readBlob<float>(fixtureDir() / "texco.bin");
    REQUIRE(expected.size() == sd->mesh().uvCount() * 2);

    size_t bad = 0;
    for (size_t i = 0; i < sd->mesh().uvCount(); ++i) {
        if (std::abs(sd->mesh().texco()[i].x - expected[i * 2 + 0]) > kPosTol) ++bad;
        if (std::abs(sd->mesh().texco()[i].y - expected[i * 2 + 1]) > kPosTol) ++bad;
    }
    CHECK(bad == 0);
}
