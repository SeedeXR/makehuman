// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Catmull-Clark WITH the static face mask -- the path the application actually
// takes. `guicommon.py:433` passes `staticFaceMask` into
// `createSubdivisionObject`, whose docstring says masked faces "are not
// included as geometry in this subdivision object (higher performance)".
//
// The existing subdiv fixture captured the UNMASKED path, so it never exercised
// this: it subdivides all 18,486 faces where the app subdivides 13,378.

#include "makehuman/core/Mesh.h"
#include "makehuman/core/ObjReader.h"
#include "makehuman/core/Subdivider.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <cmath>

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <vector>

using namespace mh;

namespace {

template <typename T>
std::vector<T> readBlob(const std::filesystem::path& p) {
    std::ifstream in(p, std::ios::binary);
    REQUIRE(in);
    in.seekg(0, std::ios::end);
    const auto bytes = static_cast<size_t>(in.tellg());
    in.seekg(0);
    std::vector<T> out(bytes / sizeof(T));
    in.read(reinterpret_cast<char*>(out.data()), static_cast<std::streamsize>(bytes));
    return out;
}

std::filesystem::path fixture(const char* name) {
    return std::filesystem::path(MH_GOLDEN_DIR) / "subdiv_masked" / name;
}

}  // namespace

TEST_CASE("masked subdivision matches the Python reference", "[golden][parity][subdiv][masked]") {
    if (!std::filesystem::exists(fixture("coord.bin"))) SKIP("masked fixture not captured");

    auto mesh = core::loadObj(std::filesystem::path(MH_DATA_DIR) / "3dobjs" / "base.obj");
    REQUIRE(mesh.has_value());

    const auto sub = core::Subdivider::build(*mesh, mesh->staticFaceMask());
    REQUIRE(sub.has_value());
    const core::Mesh& out = sub->mesh();

    const auto coord = readBlob<float>(fixture("coord.bin"));
    const auto fvert = readBlob<uint32_t>(fixture("fvert.bin"));

    // 13,378 visible faces x 4 = 53,512, against 73,944 unmasked.
    INFO("verts " << out.vertexCount() << " faces " << out.faceCount());
    CHECK(out.vertexCount() == coord.size() / 3);
    CHECK(out.faceCount() == fvert.size() / 4);

    size_t badPos = 0;
    for (size_t v = 0; v < out.vertexCount() && v < coord.size() / 3; ++v) {
        const auto& p = out.coord()[v];
        if (std::abs(p.x - coord[v * 3 + 0]) > 1e-4F || std::abs(p.y - coord[v * 3 + 1]) > 1e-4F ||
            std::abs(p.z - coord[v * 3 + 2]) > 1e-4F) {
            ++badPos;
        }
    }
    CHECK(badPos == 0);

    size_t badIdx = 0;
    for (size_t i = 0; i < fvert.size() && i < out.fvert().size(); ++i) {
        if (out.fvert()[i] != fvert[i]) ++badIdx;
    }
    CHECK(badIdx == 0);
}
