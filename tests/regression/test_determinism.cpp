// SPDX-License-Identifier: AGPL-3.0-or-later
//
// The headless `parameters -> mesh` contract (memory/todo.md, M10).
//
// M10 samples a parameter vector and expects a body back. That only works if
// the same vector always gives the same body -- and "the same vector" has to
// mean something precise, because a map of slider values has no order and the
// application of them might.
//
// Measured here rather than assumed, and the measurement found the exception:
// 288 of the 291 shipped modifiers are order-free, and the three ETHNICITY
// sliders are not, because they are a normalised triple.

#include "makehuman/core/Mesh.h"
#include "makehuman/core/Modifier.h"
#include "makehuman/core/ObjReader.h"
#include "makehuman/core/SliderLayout.h"
#include "makehuman/core/Target.h"
#include "makehuman/core/TargetIndex.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <map>
#include <random>
#include <string>
#include <utility>
#include <vector>

using namespace mh;
namespace fs = std::filesystem;

namespace {

/// Everything a character needs, loaded once: the targets are 1,280 files and
/// re-reading them per case would dominate the suite.
struct World {
    core::StandardLayout layout;
    core::TargetIndex index;
    core::Mesh base;

    static const World& get() {
        static const World w = [] {
            const fs::path data{MH_DATA_DIR};
            auto layout = core::loadStandardLayout(data / "modifiers");
            REQUIRE(layout.has_value());
            auto base = core::loadObj(data / "3dobjs" / "base.obj");
            REQUIRE(base.has_value());
            return World{.layout = std::move(*layout),
                         .index  = core::TargetIndex::build(data / "targets"),
                         .base   = std::move(*base)};
        }();
        return w;
    }
};

bool isEthnicity(const core::Modifier& m) {
    return m.macroVariable == "African" || m.macroVariable == "Asian" ||
           m.macroVariable == "Caucasian";
}

/// A deterministic pseudo-random value for every modifier, in its own range.
/// A fixed LCG rather than <random>: the point is that the SAME numbers come
/// back on every platform, and the standard library's engines are portable but
/// its distributions are not.
std::vector<std::pair<std::string, float>> everyModifier(bool includeEthnicity) {
    std::vector<std::pair<std::string, float>> out;
    uint32_t seed = 12345;
    for (const core::Modifier& m : World::get().layout.modifiers) {
        seed             = seed * 1664525U + 1013904223U;
        const float unit = static_cast<float>((seed >> 8) & 0xFFFFU) / 65535.0F;
        if (!includeEthnicity && isEthnicity(m)) continue;
        out.emplace_back(m.fullName, m.minValue() + unit * (m.maxValue() - m.minValue()));
    }
    return out;
}

std::vector<core::Vec3> meshFor(const std::vector<std::pair<std::string, float>>& params) {
    const World& w = World::get();
    core::Human human(&w.index, w.layout.modifiers);
    core::TargetLibrary targets(fs::path(MH_DATA_DIR) / "targets");
    for (const auto& [name, value] : params) {
        REQUIRE(human.setModifierValue(name, value));
    }
    core::Mesh mesh = w.base;
    human.applyStack(mesh, targets);
    return {mesh.coord().begin(), mesh.coord().end()};
}

size_t verticesDiffering(const std::vector<core::Vec3>& a, const std::vector<core::Vec3>& b) {
    REQUIRE(a.size() == b.size());
    size_t n = 0;
    for (size_t i = 0; i < a.size(); ++i) {
        if (a[i].x != b[i].x || a[i].y != b[i].y || a[i].z != b[i].z) ++n;
    }
    return n;
}

double worstDistance(const std::vector<core::Vec3>& a, const std::vector<core::Vec3>& b) {
    double worst = 0.0;
    for (size_t i = 0; i < a.size(); ++i) {
        const auto dx = static_cast<double>(a[i].x - b[i].x);
        const auto dy = static_cast<double>(a[i].y - b[i].y);
        const auto dz = static_cast<double>(a[i].z - b[i].z);
        worst         = std::max(worst, std::sqrt(dx * dx + dy * dy + dz * dz));
    }
    return worst;
}

}  // namespace

TEST_CASE("the same parameters give a bit-identical mesh", "[core][determinism][slow]") {
    // Two independently constructed characters, same values, same order. This
    // is the weakest of the three and still worth pinning: it is what a
    // generative pipeline assumes before it assumes anything else.
    const auto params = everyModifier(false);
    REQUIRE(params.size() == 288);
    CHECK(verticesDiffering(meshFor(params), meshFor(params)) == 0);
}

TEST_CASE("the order parameters are SET in does not change the mesh", "[core][determinism][slow]") {
    // 288 modifiers, forward against reversed and against three shuffles. Every
    // vertex must match to the bit.
    //
    // Not a foregone conclusion: `applyStack` sums each target's contribution
    // into a vertex, float addition is not associative, and the stack is an
    // unordered_map. What saves it is that `setModifierValue` ends in
    // `rebuildStack()`, which recomputes every entry from the scalars rather
    // than patching the previous stack -- so the stack that is applied depends
    // on the VALUES, not on the route taken to them.
    const auto forward = everyModifier(false);
    const auto mesh    = meshFor(forward);

    auto reversed = forward;
    std::ranges::reverse(reversed);
    INFO("reversed order");
    CHECK(verticesDiffering(mesh, meshFor(reversed)) == 0);

    for (uint32_t trial = 1; trial <= 3; ++trial) {
        auto shuffled = forward;
        std::mt19937 rng(trial);
        std::ranges::shuffle(shuffled, rng);
        INFO("shuffle " << trial);
        CHECK(verticesDiffering(mesh, meshFor(shuffled)) == 0);
    }
}

TEST_CASE("the ethnicity triple IS order-dependent, and that is by construction",
          "[core][determinism][slow]") {
    // African, Asian and Caucasian are a NORMALISED triple: setting one
    // rescales the other two (`MacroFactors::setEthnicVals`, and the reference
    // does the same at `human.py:811-822`). So the order they are assigned in
    // is part of the input, not noise.
    //
    // Pinned in the direction that matters -- it must KEEP differing. A future
    // change that made the triple order-free would be changing the character
    // model, not fixing a bug, and this test is where that argument has to be
    // had. Including all three in the sweep above moves 19,158 of 19,158
    // vertices by up to 3 cm, which is the whole reason the exception is
    // written down.
    const World& w = World::get();
    std::vector<std::pair<std::string, float>> triple;
    for (const core::Modifier& m : w.layout.modifiers) {
        if (isEthnicity(m)) triple.emplace_back(m.fullName, 0.0F);
    }
    REQUIRE(triple.size() == 3);
    triple[0].second = 0.7F;
    triple[1].second = 0.2F;
    triple[2].second = 0.5F;

    auto reversed = triple;
    std::ranges::reverse(reversed);

    const auto a = meshFor(triple);
    const auto b = meshFor(reversed);
    CHECK(verticesDiffering(a, b) > 0);
    // Millimetres, not microns: this is a different character, not a rounding
    // difference, and calling it either would be wrong.
    const double worst = worstDistance(a, b);
    INFO("worst |delta| " << worst << " dm");
    CHECK(worst > 0.001);
}
