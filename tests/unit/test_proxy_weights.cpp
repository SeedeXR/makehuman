// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Skin weights for a fitted proxy, derived from the body's.
//
// A proxy vertex is a weighted blend of three base-mesh vertices -- that is
// what `.mhclo` fitting IS -- so its skin weights are the same blend of those
// vertices' skin weights. Without them a live-rig export ships the body posed
// and everything worn where it was: measured 2026-09-07 as eyes protruding from
// their sockets in both our glTF and our FBX.

#include "makehuman/core/Proxy.h"
#include "makehuman/rig/VertexWeights.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <array>
#include <cmath>
#include <filesystem>
#include <vector>

using Catch::Matchers::WithinAbs;
using namespace mh;

namespace {

/// Three body vertices, each bound entirely to a different bone.
rig::CompiledWeights bodyWeights() {
    rig::CompiledWeights w;
    w.influences = 4;
    for (uint32_t v = 0; v < 3; ++v) {
        for (uint8_t i = 0; i < 4; ++i) {
            w.boneIndex.push_back(i == 0 ? v : 0);
            w.weight.push_back(i == 0 ? 1.0F : 0.0F);
        }
    }
    return w;
}

}  // namespace

TEST_CASE("a proxy vertex bound to one body vertex inherits its weights", "[rig][proxy]") {
    const auto body = bodyWeights();
    const std::vector<std::array<uint32_t, 3>> refs{{2, 2, 2}};
    const std::vector<std::array<float, 3>> fit{{1.0F, 0.0F, 0.0F}};

    const auto out = rig::proxyWeights(body, refs, fit);
    REQUIRE(out.vertexCount() == 1);
    CHECK(out.boneIndex[0] == 2);
    CHECK_THAT(static_cast<double>(out.weight[0]), WithinAbs(1.0, 1e-6));
}

TEST_CASE("a proxy vertex between two body vertices blends their bones", "[rig][proxy]") {
    // The whole point: the fitting weights ARE the skinning blend. Half way
    // between a vertex on bone 0 and one on bone 1 is half on each.
    const auto body = bodyWeights();
    const std::vector<std::array<uint32_t, 3>> refs{{0, 1, 1}};
    const std::vector<std::array<float, 3>> fit{{0.5F, 0.5F, 0.0F}};

    const auto out = rig::proxyWeights(body, refs, fit);
    REQUIRE(out.vertexCount() == 1);
    std::vector<std::pair<uint32_t, float>> got;
    for (uint8_t i = 0; i < out.influences; ++i) {
        if (out.weight[i] > 0.0F) got.emplace_back(out.boneIndex[i], out.weight[i]);
    }
    REQUIRE(got.size() == 2);
    CHECK_THAT(static_cast<double>(got[0].second), WithinAbs(0.5, 1e-6));
    CHECK_THAT(static_cast<double>(got[1].second), WithinAbs(0.5, 1e-6));
    CHECK(((got[0].first == 0 && got[1].first == 1) || (got[0].first == 1 && got[1].first == 0)));
}

TEST_CASE("weights come out sorted and summing to one", "[rig][proxy]") {
    // Strongest first, like CompiledWeights everywhere else, and normalised --
    // a renderer that does not normalise shows a seam where a proxy meets the
    // body, and one that does hides the mistake.
    const auto body = bodyWeights();
    const std::vector<std::array<uint32_t, 3>> refs{{0, 1, 2}};
    const std::vector<std::array<float, 3>> fit{{0.2F, 0.5F, 0.3F}};

    const auto out = rig::proxyWeights(body, refs, fit);
    REQUIRE(out.vertexCount() == 1);
    float sum = 0.0F;
    for (uint8_t i = 0; i < out.influences; ++i)
        sum += out.weight[i];
    CHECK_THAT(static_cast<double>(sum), WithinAbs(1.0, 1e-6));
    CHECK_THAT(static_cast<double>(out.weight[0]), WithinAbs(0.5, 1e-6));
    CHECK(out.boneIndex[0] == 1);
    for (uint8_t i = 1; i < out.influences; ++i)
        CHECK(out.weight[i] <= out.weight[i - 1]);
}

TEST_CASE("more bones than slots keeps the strongest and renormalises", "[rig][proxy]") {
    // Three body vertices at four influences each is up to twelve bones on one
    // proxy vertex. glTF and FBX both take four, so the rest have to go -- and
    // dropping them without renormalising shrinks the vertex toward the origin.
    rig::CompiledWeights body;
    body.influences = 4;
    for (uint32_t v = 0; v < 3; ++v) {
        for (uint8_t i = 0; i < 4; ++i) {
            body.boneIndex.push_back((v * 4) + i);
            body.weight.push_back(0.25F);
        }
    }
    const std::vector<std::array<uint32_t, 3>> refs{{0, 1, 2}};
    const std::vector<std::array<float, 3>> fit{{0.5F, 0.3F, 0.2F}};

    const auto out = rig::proxyWeights(body, refs, fit);
    REQUIRE(out.vertexCount() == 1);
    float sum = 0.0F;
    for (uint8_t i = 0; i < out.influences; ++i)
        sum += out.weight[i];
    CHECK_THAT(static_cast<double>(sum), WithinAbs(1.0, 1e-6));
    // The four survivors must be the ones from the strongest ref vertex, which
    // contributes 0.5 / 4 = 0.125 each against 0.075 and 0.05.
    for (uint8_t i = 0; i < 4; ++i)
        CHECK(out.boneIndex[i] < 4);
    CHECK(out.clampedVertices == 1);
}

TEST_CASE("a bone shared by two ref vertices takes ONE slot", "[rig][proxy]") {
    // Adjacent body vertices almost always share bones, so this is the common
    // case rather than an edge one -- and appending duplicates instead of
    // accumulating them spends two of the four slots on one bone. The totals
    // still sum to 1 and LBS still gives the same point, so a weight-sum check
    // cannot see it; what is lost is a slot a genuine third bone needed.
    rig::CompiledWeights body;
    body.influences = 4;
    const std::array<uint32_t, 3> bones{5, 5, 7};
    for (const uint32_t bone : bones) {
        for (uint8_t i = 0; i < 4; ++i) {
            body.boneIndex.push_back(i == 0 ? bone : 0);
            body.weight.push_back(i == 0 ? 1.0F : 0.0F);
        }
    }
    const std::vector<std::array<uint32_t, 3>> refs{{0, 1, 2}};
    const std::vector<std::array<float, 3>> fit{{0.25F, 0.25F, 0.5F}};

    const auto out = rig::proxyWeights(body, refs, fit);
    REQUIRE(out.vertexCount() == 1);
    size_t used = 0;
    for (uint8_t i = 0; i < out.influences; ++i) {
        if (out.weight[i] > 0.0F) ++used;
    }
    // Two bones, not three entries.
    CHECK(used == 2);
    // Bone 7 carries 0.5 and bone 5 the two quarters, so 5 and 7 tie -- the
    // check that matters is that 5 appears once, with the SUM of its shares.
    float five = 0.0F;
    for (uint8_t i = 0; i < out.influences; ++i) {
        if (out.boneIndex[i] == 5 && out.weight[i] > 0.0F) five += out.weight[i];
    }
    CHECK_THAT(static_cast<double>(five), WithinAbs(0.5, 1e-6));
}

TEST_CASE("the shipped eye proxy derives usable weights", "[rig][proxy][parity]") {
    // Synthetic fixtures prove the arithmetic; this proves the arithmetic
    // survives the real fitting data. `high-poly.mhclo` is the eye proxy the
    // application actually wears, and it is the one that visibly detached from
    // a posed body.
    const auto proxy = core::loadProxy(std::filesystem::path(MH_DATA_DIR) / "eyes" / "high-poly" /
                                       "high-poly.mhclo");
    REQUIRE(proxy.has_value());
    REQUIRE(!proxy->refVerts.empty());

    // A stand-in body: every base vertex on its own bone, so a proxy vertex's
    // weights can only come from the vertices it is fitted to.
    const size_t baseVertices = 19158;
    rig::CompiledWeights body;
    body.influences = 4;
    body.boneIndex.resize(baseVertices * 4, 0U);
    body.weight.resize(baseVertices * 4, 0.0F);
    for (size_t v = 0; v < baseVertices; ++v) {
        body.boneIndex[v * 4] = static_cast<uint32_t>(v % 179);  // the shipped rig's bone count
        body.weight[v * 4]    = 1.0F;
    }

    const auto out = rig::proxyWeights(body, proxy->refVerts, proxy->weights);
    REQUIRE(out.vertexCount() == proxy->refVerts.size());
    // Every vertex must be fully weighted. A zero row is a vertex bound to
    // nothing, which a consumer drops to the origin.
    size_t unweighted = 0;
    for (size_t v = 0; v < out.vertexCount(); ++v) {
        float sum = 0.0F;
        for (uint8_t i = 0; i < out.influences; ++i)
            sum += out.weight[(v * out.influences) + i];
        if (std::abs(sum - 1.0F) > 1e-4F) ++unweighted;
    }
    INFO(out.vertexCount() << " proxy vertices, " << unweighted << " not fully weighted");
    CHECK(unweighted == 0);
}

TEST_CASE("mismatched inputs produce nothing rather than nonsense", "[rig][proxy]") {
    // A ref index past the end of the body's weights would read out of bounds.
    // Refusing is the only safe answer: a silently truncated skin binds the
    // remaining vertices to bone 0, which looks like a proxy glued to the hips.
    const auto body = bodyWeights();
    const std::vector<std::array<uint32_t, 3>> refs{{0, 1, 99}};
    const std::vector<std::array<float, 3>> fit{{0.5F, 0.3F, 0.2F}};
    CHECK(rig::proxyWeights(body, refs, fit).vertexCount() == 0);

    // ... and so would a fitting array of a different length.
    const std::vector<std::array<float, 3>> shortFit{};
    CHECK(rig::proxyWeights(body, refs, shortFit).vertexCount() == 0);
}
