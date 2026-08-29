// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Vertex bone weights, checked against the reference's own normalised output.
//
// Regenerate with:
//     ./.venv-mh/bin/python tools/capture_fixture.py weights

#include "makehuman/rig/Skeleton.h"
#include "makehuman/rig/VertexWeights.h"

#include <nlohmann/json.hpp>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

using namespace mh;

namespace {

constexpr size_t kVerts = 19158;

std::filesystem::path fixtureDir() {
    return std::filesystem::path(MH_GOLDEN_DIR) / "weights";
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

rig::VertexWeights loaded() {
    auto w = rig::loadWeights(std::filesystem::path(MH_DATA_DIR) / "rigs" / "default_weights.mhw",
                              kVerts);
    REQUIRE(w.has_value());
    return std::move(*w);
}

rig::Skeleton loadedRig() {
    auto s = rig::loadSkeleton(std::filesystem::path(MH_DATA_DIR) / "rigs" / "default.mhskel");
    REQUIRE(s.has_value());
    return std::move(*s);
}

}  // namespace

// The normalised per-bone lists, bone by bone and entry by entry, against what
// the reference produced. This covers the merge, the normalisation, the sort
// and the threshold in one comparison.
TEST_CASE("normalised per-bone weights match the reference", "[weights][golden][parity]") {
    const rig::VertexWeights vw = loaded();

    std::ifstream namesIn(fixtureDir() / "bone_names.json");
    REQUIRE(namesIn);
    const auto names = nlohmann::json::parse(namesIn);

    const auto verts   = readBlob<uint32_t>(fixtureDir() / "verts.bin");
    const auto weights = readBlob<float>(fixtureDir() / "weights.bin");
    const auto offsets = readBlob<uint32_t>(fixtureDir() / "offsets.bin");

    REQUIRE(names.size() == 139);
    REQUIRE(offsets.size() == names.size() + 1);
    REQUIRE(verts.size() == weights.size());
    REQUIRE(vw.perBone.size() == names.size());

    size_t missingBone    = 0;
    size_t sizeMismatch   = 0;
    size_t vertMismatch   = 0;
    size_t weightMismatch = 0;
    float worst           = 0.0F;

    for (size_t b = 0; b < names.size(); ++b) {
        const auto boneName = names[b].get<std::string>();
        CAPTURE(boneName);

        const auto it = vw.perBone.find(boneName);
        if (it == vw.perBone.end()) {
            ++missingBone;
            continue;
        }
        const size_t first = offsets[b];
        const size_t count = offsets[b + 1] - offsets[b];

        if (it->second.verts.size() != count) {
            ++sizeMismatch;
            continue;
        }
        for (size_t k = 0; k < count; ++k) {
            if (it->second.verts[k] != verts[first + k]) ++vertMismatch;
            const float d = std::abs(it->second.weights[k] - weights[first + k]);
            worst         = std::max(worst, d);
            if (d > 1e-6F) ++weightMismatch;
        }
    }
    INFO("worst weight delta " << worst);
    CHECK(missingBone == 0);
    CHECK(sizeMismatch == 0);
    CHECK(vertMismatch == 0);
    CHECK(weightMismatch == 0);
}

// The header fields are parsed, not just the weights block. Asserted rather
// than left unexercised: a partially-read header is exactly the kind of thing
// that goes unnoticed until a .mhw writer round-trips and loses them.
TEST_CASE("file metadata is read", "[weights][parity]") {
    const rig::VertexWeights vw = loaded();
    CHECK(vw.name == "MakeHuman weights");
    CHECK(vw.version == 110);
    CHECK(vw.description == "Symmetric weights for default makehuman mesh");
    CHECK(vw.vertexCount == kVerts);
}

// The invariant the whole normalisation exists to establish.
TEST_CASE("every vertex's weights sum to one", "[weights][parity]") {
    const rig::VertexWeights vw = loaded();

    std::vector<float> total(kVerts, 0.0F);
    for (const auto& [bone, bw] : vw.perBone) {
        for (size_t k = 0; k < bw.verts.size(); ++k)
            total[bw.verts[k]] += bw.weights[k];
    }

    size_t bad = 0;
    for (size_t v = 0; v < kVerts; ++v) {
        if (std::abs(total[v] - 1.0F) > 1e-4F) ++bad;
    }
    CHECK(bad == 0);  // includes the unweighted vertices bound to root at 1.0
}

// The base rig reaches 12 influences on some vertex, which is why truncation
// exists at all. If this ever reads 4 or less, the compile test below is not
// exercising the path it claims to.
TEST_CASE("the rig really does exceed four influences", "[weights][parity]") {
    const rig::VertexWeights vw = loaded();
    CHECK(vw.maxInfluences() == 12);
}

// The compiled 4-influence form: what glTF's JOINTS_0/WEIGHTS_0 need.
TEST_CASE("compiled 4-influence weights match the reference", "[weights][golden][parity]") {
    const rig::VertexWeights vw = loaded();
    const rig::Skeleton skel    = loadedRig();

    const auto compiled = vw.compile(skel, 4);
    REQUIRE(compiled.vertexCount() == kVerts);

    const auto expBones   = readBlob<uint32_t>(fixtureDir() / "compiled4_bones.bin");
    const auto expWeights = readBlob<float>(fixtureDir() / "compiled4_weights.bin");
    REQUIRE(expBones.size() == kVerts * 4);
    REQUIRE(expWeights.size() == kVerts * 4);

    size_t boneBad   = 0;
    size_t weightBad = 0;
    float worst      = 0.0F;
    for (size_t i = 0; i < kVerts * 4; ++i) {
        // Compare the bone index only where the slot carries real weight: an
        // unused slot is (0, 0) on both sides but a zero-weight slot's index
        // is not meaningful.
        if (expWeights[i] > 0.0F && compiled.boneIndex[i] != expBones[i]) ++boneBad;
        const float d = std::abs(compiled.weight[i] - expWeights[i]);
        worst         = std::max(worst, d);
        if (d > 1e-6F) ++weightBad;
    }
    INFO("worst compiled weight delta " << worst);
    CHECK(boneBad == 0);
    CHECK(weightBad == 0);
}

// Truncation must re-normalise, or every heavily-weighted vertex loses mass.
TEST_CASE("truncated influences are re-normalised", "[weights][parity]") {
    const rig::VertexWeights vw = loaded();
    const rig::Skeleton skel    = loadedRig();
    const auto compiled         = vw.compile(skel, 4);

    size_t bad = 0;
    for (size_t v = 0; v < kVerts; ++v) {
        float sum = 0.0F;
        for (size_t i = 0; i < 4; ++i)
            sum += compiled.weight[v * 4 + i];
        if (std::abs(sum - 1.0F) > 1e-4F) ++bad;
    }
    CHECK(bad == 0);
}

TEST_CASE("a vertex index past the mesh is refused", "[weights]") {
    // The real file indexes a 19,158-vertex body; loading it against a smaller
    // mesh must be an error, not an out-of-bounds write.
    const auto w =
        rig::loadWeights(std::filesystem::path(MH_DATA_DIR) / "rigs" / "default_weights.mhw", 100);
    REQUIRE_FALSE(w.has_value());
    CHECK(w.error().kind == rig::WeightsErrorKind::VertexOutOfRange);
}

TEST_CASE("unweighted vertices bind to the root bone", "[weights][parity]") {
    const auto p = std::filesystem::temp_directory_path() / "mh_sparse.mhw";
    {
        std::ofstream out(p);
        // Vertex 0 weighted; vertices 1 and 2 mentioned nowhere.
        out << R"({"name":"t","weights":{"spine":[[0,2.0]]}})";
    }
    const auto w = rig::loadWeights(p, 3);
    REQUIRE(w.has_value());

    // Vertex 0 normalises 2.0 -> 1.0 (weights are relative, not absolute).
    const auto spine = w->perBone.find("spine");
    REQUIRE(spine != w->perBone.end());
    REQUIRE(spine->second.verts.size() == 1);
    CHECK(spine->second.weights[0] == 1.0F);

    const auto root = w->perBone.find("root");
    REQUIRE(root != w->perBone.end());
    CHECK(root->second.verts == std::vector<uint32_t>{1, 2});
    CHECK(root->second.weights == std::vector<float>{1.0F, 1.0F});

    std::error_code ec;
    std::filesystem::remove(p, ec);
}

TEST_CASE("a vertex listed twice under one bone is merged", "[weights][parity]") {
    const auto p = std::filesystem::temp_directory_path() / "mh_double.mhw";
    {
        std::ofstream out(p);
        out << R"({"name":"t","weights":{"a":[[0,0.25],[0,0.25]],"b":[[0,0.5]]}})";
    }
    const auto w = rig::loadWeights(p, 1);
    REQUIRE(w.has_value());

    // total = 1.0, so a merges to 0.5 and b stays 0.5.
    REQUIRE(w->perBone.at("a").verts.size() == 1);
    CHECK(std::abs(w->perBone.at("a").weights[0] - 0.5F) < 1e-6F);
    CHECK(std::abs(w->perBone.at("b").weights[0] - 0.5F) < 1e-6F);

    std::error_code ec;
    std::filesystem::remove(p, ec);
}
