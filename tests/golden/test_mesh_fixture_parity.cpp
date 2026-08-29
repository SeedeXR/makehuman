// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Byte-level parity against fixtures captured from the Python reference by
// tools/capture_fixture.py. This is the real oracle: it compares our loader's
// output against what the reference actually produced, vertex by vertex.
//
// Regenerate with:
//     ./.venv-mh/bin/python tools/capture_fixture.py mesh

#include "makehuman/core/ObjReader.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <vector>

using namespace mh::core;

namespace {

std::filesystem::path fixtureDir() {
    return std::filesystem::path(MH_GOLDEN_DIR) / "mesh";
}

/// Reads a little-endian blob captured by tools/capture_fixture.py.
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

/// Positions accumulate through a sparse float32 pipeline; 1e-5 mesh units is
/// the tolerance stated in memory/test.md section 3.3.
constexpr float kPosTol = 1e-5F;
/// Normals are normalised after an area-weighted sum.
constexpr float kNormTol = 1e-4F;

bool fixturesPresent() {
    return std::filesystem::exists(fixtureDir() / "coord.bin");
}

}  // namespace

TEST_CASE("vertex positions match the Python reference exactly",
          "[golden][parity][fixture][core]") {
    if (!fixturesPresent()) SKIP("run tools/capture_fixture.py mesh first");

    const auto mesh = loadObj(std::filesystem::path(MH_DATA_DIR) / "3dobjs" / "base.obj");
    REQUIRE(mesh.has_value());

    const auto expected = readBlob<float>(fixtureDir() / "coord.bin");
    REQUIRE(expected.size() == mesh->vertexCount() * 3);

    size_t mismatches = 0;
    float worst       = 0.0F;
    size_t worstIdx   = 0;
    for (size_t i = 0; i < mesh->vertexCount(); ++i) {
        const Vec3& got = mesh->coord()[i];
        const float dx  = std::abs(got.x - expected[i * 3 + 0]);
        const float dy  = std::abs(got.y - expected[i * 3 + 1]);
        const float dz  = std::abs(got.z - expected[i * 3 + 2]);
        const float d   = std::max({dx, dy, dz});
        if (d > worst) {
            worst    = d;
            worstIdx = i;
        }
        if (d > kPosTol) ++mismatches;
    }
    INFO("worst delta " << worst << " at vertex " << worstIdx);
    CHECK(mismatches == 0);
}

TEST_CASE("face vertex indices match the Python reference exactly",
          "[golden][parity][fixture][core]") {
    if (!fixturesPresent()) SKIP("run tools/capture_fixture.py mesh first");

    const auto mesh = loadObj(std::filesystem::path(MH_DATA_DIR) / "3dobjs" / "base.obj");
    REQUIRE(mesh.has_value());

    const auto expected = readBlob<uint32_t>(fixtureDir() / "fvert.bin");
    REQUIRE(expected.size() == mesh->fvert().size());
    CHECK(std::equal(expected.begin(), expected.end(), mesh->fvert().begin()));
}

TEST_CASE("face UV indices match the Python reference exactly", "[golden][parity][fixture][core]") {
    if (!fixturesPresent()) SKIP("run tools/capture_fixture.py mesh first");
    if (!std::filesystem::exists(fixtureDir() / "fuvs.bin")) SKIP("no fuvs fixture");

    const auto mesh = loadObj(std::filesystem::path(MH_DATA_DIR) / "3dobjs" / "base.obj");
    REQUIRE(mesh.has_value());

    // The dual index space is the property under test: fuvs is NOT parallel in
    // value to fvert (module3d.py:627 vs :629), so an implementation that
    // conflated them would pass every count check and fail here.
    const auto expected = readBlob<uint32_t>(fixtureDir() / "fuvs.bin");
    REQUIRE(expected.size() == mesh->fuvs().size());
    CHECK(std::equal(expected.begin(), expected.end(), mesh->fuvs().begin()));
}

TEST_CASE("UV coordinates match the Python reference exactly", "[golden][parity][fixture][core]") {
    if (!fixturesPresent()) SKIP("run tools/capture_fixture.py mesh first");
    if (!std::filesystem::exists(fixtureDir() / "texco.bin")) SKIP("no texco fixture");

    const auto mesh = loadObj(std::filesystem::path(MH_DATA_DIR) / "3dobjs" / "base.obj");
    REQUIRE(mesh.has_value());

    const auto expected = readBlob<float>(fixtureDir() / "texco.bin");
    REQUIRE(expected.size() == mesh->uvCount() * 2);

    size_t mismatches = 0;
    for (size_t i = 0; i < mesh->uvCount(); ++i) {
        if (std::abs(mesh->texco()[i].x - expected[i * 2 + 0]) > kPosTol) ++mismatches;
        if (std::abs(mesh->texco()[i].y - expected[i * 2 + 1]) > kPosTol) ++mismatches;
    }
    CHECK(mismatches == 0);
}

TEST_CASE("vertex normals match the Python reference within tolerance",
          "[golden][parity][fixture][core][normals]") {
    if (!fixturesPresent()) SKIP("run tools/capture_fixture.py mesh first");

    const auto mesh = loadObj(std::filesystem::path(MH_DATA_DIR) / "3dobjs" / "base.obj");
    REQUIRE(mesh.has_value());

    const auto expected = readBlob<float>(fixtureDir() / "vnorm.bin");
    REQUIRE(expected.size() == mesh->vertexCount() * 3);

    // Compared by angle rather than component-wise: both are unit vectors, so
    // the dot product is the meaningful measure of agreement.
    size_t mismatches = 0;
    float worstDot    = 1.0F;
    for (size_t i = 0; i < mesh->vertexCount(); ++i) {
        const Vec3 ref{expected[i * 3 + 0], expected[i * 3 + 1], expected[i * 3 + 2]};
        const float refLen = std::sqrt(dot(ref, ref));
        if (!std::isfinite(refLen) || refLen < 0.5F) continue;  // reference NaN/zero: skip
        const float d = dot(mesh->vnorm()[i], ref * (1.0F / refLen));
        worstDot      = std::min(worstDot, d);
        if (d < 1.0F - kNormTol) ++mismatches;
    }
    INFO("worst dot product with the reference normal: " << worstDot);
    CHECK(mismatches == 0);
}

TEST_CASE("face group assignment matches the Python reference exactly",
          "[golden][parity][fixture][core]") {
    if (!fixturesPresent()) SKIP("run tools/capture_fixture.py mesh first");

    const auto mesh = loadObj(std::filesystem::path(MH_DATA_DIR) / "3dobjs" / "base.obj");
    REQUIRE(mesh.has_value());

    const auto expected = readBlob<uint16_t>(fixtureDir() / "group.bin");
    REQUIRE(expected.size() == mesh->faceCount());
    CHECK(std::equal(expected.begin(), expected.end(), mesh->group().begin()));
}
