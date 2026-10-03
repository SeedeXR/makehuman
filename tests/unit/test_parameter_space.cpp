// SPDX-License-Identifier: AGPL-3.0-or-later
//
// The parameter space, on the SHIPPED modifiers rather than a toy set. The
// claims here are about order, coupling and round-tripping, and all three are
// properties of the real 288-modifier set -- a hand-built pair of sliders would
// have no ethnic triple and no sort to get wrong.
#include "makehuman/core/ParameterSpace.h"

#include "makehuman/core/Modifier.h"
#include "makehuman/core/TargetIndex.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <set>

using Catch::Matchers::WithinAbs;
using namespace mh;

namespace {

std::filesystem::path dataDir() {
    return std::filesystem::path(MH_DATA_DIR);
}

/// The shipped target index, built ONCE. `TargetIndex::build` walks 1,280
/// targets and these cases build several humans; a per-call build turned the
/// file into the slowest unit test in the suite for no claim it was making.
const core::TargetIndex& sharedIndex() {
    static const core::TargetIndex index = core::TargetIndex::build(dataDir() / "targets");
    return index;
}

core::Human shippedHuman() {
    auto mods = core::loadModifiers(dataDir() / "modifiers" / "modeling_modifiers.json");
    REQUIRE(mods.has_value());
    return core::Human(&sharedIndex(), std::move(*mods));
}

}  // namespace

TEST_CASE("the space covers every modifier and is sorted by name", "[params]") {
    // The ORDER IS THE MEANING of a vector: element 7 is a particular slider
    // only if everyone agrees which. Sorting by name makes that a property of
    // the names rather than of the order a JSON file listed them, so adding a
    // custom modifier directory cannot silently renumber a saved vector.
    const auto human = shippedHuman();
    const auto space = core::ParameterSpace::of(human);

    CHECK(space.size() == human.modifiers().size());
    CHECK(space.size() > 200);  // the shipped set is 288; this is a floor, not a pin

    const auto params = space.parameters();
    CHECK(std::ranges::is_sorted(params, [](const core::Parameter& a, const core::Parameter& b) {
        return a.name < b.name;
    }));

    // Built twice, same order. A space whose order depended on anything but the
    // names would be useless as a serialisation format.
    const auto again = core::ParameterSpace::of(human);
    REQUIRE(again.size() == space.size());
    for (size_t i = 0; i < space.size(); ++i)
        CHECK(again.parameters()[i].name == params[i].name);
}

TEST_CASE("exactly the three ethnic components are marked coupled", "[params]") {
    // Not two, not four. The triple is what `MacroFactors` renormalises, and a
    // fourth member would be drawn from the simplex and renormalised with them.
    const auto human = shippedHuman();
    const auto space = core::ParameterSpace::of(human);

    std::set<std::string> ethnic;
    for (const core::Parameter& p : space.parameters()) {
        if (p.ethnic) ethnic.insert(p.name);
    }
    CHECK(ethnic.size() == 3);
    for (const std::string& n : ethnic) {
        INFO("ethnic parameter: " << n);
        CHECK(n.find("macrodetails/") == 0);
    }
}

TEST_CASE("the same vector always gives the same body", "[params]") {
    // THE LOAD-BEARING CLAIM, and the one a generative model depends on: a
    // vector names a body. The stack is the right thing to compare -- it is
    // what `applyStack` walks to move the mesh, so two humans with the same
    // stack produce the same vertices, bit for bit.
    auto human       = shippedHuman();
    const auto space = core::ParameterSpace::of(human);

    const auto v = space.sample(20261003ULL);
    REQUIRE(v.size() == space.size());
    REQUIRE(space.fromVector(v, human) > 0);

    auto other = shippedHuman();
    REQUIRE(space.fromVector(v, other) > 0);
    CHECK(other.stack() == human.stack());
}

TEST_CASE("a read-back vector is exact except on the coupled triple", "[params]") {
    // MEASURED, and the exception is arithmetic rather than a defect.
    //
    // Every independent dimension reads back BIT-EXACTLY. The three ethnic
    // components do not: `fromVector` normalises them to sum to 1, and three
    // floats that sum to 1 in double do not generally sum to 1.0f, so the
    // normalise moves each by about an ulp.
    //
    // It does NOT accumulate. Feeding the read-back vector round again six
    // times, the maximum change stays pinned at 5.96e-08 -- one ulp at this
    // magnitude -- and the ethnic sum alternates between +3.7e-08 and
    // -6.0e-08. That is a two-cycle between neighbouring float
    // representations, not drift, so a vector may be saved and reloaded
    // indefinitely without wandering.
    auto human       = shippedHuman();
    const auto space = core::ParameterSpace::of(human);
    const auto sent  = space.sample(20261003ULL);
    REQUIRE(space.fromVector(sent, human) > 0);
    const auto read = space.toVector(human);

    size_t inexact = 0;
    for (size_t i = 0; i < sent.size(); ++i) {
        const core::Parameter& p = space.parameters()[i];
        if (p.ethnic) {
            ++inexact;
            INFO(p.name);
            CHECK_THAT(static_cast<double>(read[i]), WithinAbs(static_cast<double>(sent[i]), 1e-6));
        } else {
            INFO(p.name << " sent " << sent[i] << " read " << read[i]);
            CHECK(read[i] == sent[i]);
        }
    }
    // Exactly three, so this never silently becomes "nothing is exact".
    CHECK(inexact == 3);
}

TEST_CASE("the ethnic triple sums to one after a round trip", "[params]") {
    // The coupling, which is the whole reason this type exists rather than a
    // bare std::vector<float>. `fromVector` goes through `setModifierValues`,
    // which blocks the renormalisation until all three are in and normalises
    // once -- a loop over `setModifierValue` would leave the answer depending
    // on this space's sort order.
    auto human       = shippedHuman();
    const auto space = core::ParameterSpace::of(human);

    for (const uint64_t seed : {1ULL, 7ULL, 4242ULL}) {
        REQUIRE(space.fromVector(space.sample(seed), human) > 0);
        const float sum =
            human.factors().african() + human.factors().asian() + human.factors().caucasian();
        INFO("seed " << seed);
        CHECK_THAT(static_cast<double>(sum), WithinAbs(1.0, 1e-5));
    }
}

TEST_CASE("sampling is deterministic and stays inside the bounds", "[params]") {
    // Deterministic in the seed, because a sampled character nobody can
    // reproduce is not a character anyone can report a bug about. In range,
    // because `fromVector` clamps and a sampler that relied on that clamp would
    // be hiding its own distribution.
    const auto human = shippedHuman();
    const auto space = core::ParameterSpace::of(human);

    const auto a = space.sample(99ULL);
    const auto b = space.sample(99ULL);
    const auto c = space.sample(100ULL);
    CHECK(a == b);
    CHECK(a != c);

    const auto params = space.parameters();
    for (size_t i = 0; i < space.size(); ++i) {
        INFO(params[i].name << " = " << a[i]);
        CHECK(a[i] >= params[i].minValue);
        CHECK(a[i] <= params[i].maxValue);
    }
}

TEST_CASE("a vector of the wrong length is refused outright", "[params]") {
    // Applied as far as it goes, a short vector leaves the tail at whatever the
    // human already carried -- a different person than the caller described,
    // and one that reads as a sampling bug rather than a length bug.
    auto human       = shippedHuman();
    const auto space = core::ParameterSpace::of(human);

    const auto full = space.sample(5ULL);
    REQUIRE(space.fromVector(full, human) > 0);
    const auto before = human.stack();

    CHECK(space.fromVector(std::span<const float>{}, human) == 0);
    std::vector<float> shortVec(full.begin(), full.end() - 1);
    CHECK(space.fromVector(shortVec, human) == 0);
    std::vector<float> longVec(full.begin(), full.end());
    longVec.push_back(0.5F);
    CHECK(space.fromVector(longVec, human) == 0);

    // And none of those touched the character.
    CHECK(human.stack() == before);
}

TEST_CASE("the ethnic triple is uniform on the simplex, not piled at the centre", "[params]") {
    // Three uniforms over their sum is NOT uniform on a simplex: it
    // concentrates near (1/3,1/3,1/3), so a sampled crowd would be short of
    // strongly-one-ethnicity faces. The Dirichlet(1,1,1) draw this uses puts a
    // corner-ish sample -- any component above 0.8 -- at about 3 x 0.2^2 = 12%,
    // so over 400 draws seeing NONE would be about 1e-23. Five is a floor far
    // below the expectation and far above what the uniform-ratio method, which
    // can never exceed 1/3 + a little, would produce.
    const auto human = shippedHuman();
    const auto space = core::ParameterSpace::of(human);
    std::vector<size_t> ethnic;
    for (size_t i = 0; i < space.size(); ++i) {
        if (space.parameters()[i].ethnic) ethnic.push_back(i);
    }
    REQUIRE(ethnic.size() == 3);

    size_t corners = 0;
    for (uint64_t seed = 0; seed < 400; ++seed) {
        const auto v = space.sample(seed);
        for (const size_t i : ethnic) {
            if (v[i] > 0.8F) {
                ++corners;
                break;
            }
        }
    }
    INFO("samples with a component above 0.8: " << corners << " of 400");
    CHECK(corners >= 5);
}
