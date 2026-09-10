// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Choosing which wrinkle map the renderer shows, and admitting what it cannot.
//
// Owner directive 12.3's "one pose-signal evaluator, several consumers" meets
// the shape of the thing consuming it: the RBF returns a weight for EVERY
// example pose, each of which may name its own wrinkle map, and
// `render::MeshInstance` carries exactly ONE map and one weight per mesh.
//
// So something has to choose, and the interesting cases are not the arithmetic
// -- they are what happens when the choice loses information.
#include "makehuman/rig/Wrinkles.h"

#include "makehuman/core/CorrectiveManifest.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <string>
#include <string_view>
#include <vector>

using namespace mh;
using Catch::Matchers::WithinAbs;

namespace {

/// A manifest with one pose per (name, wrinkle) pair given. Only the fields
/// `chooseWrinkle` reads are filled: it does not solve anything, so radius,
/// drivers and signals are beside the point here.
core::CorrectiveManifest manifestOf(const std::vector<std::pair<std::string, std::string>>& poses) {
    core::CorrectiveManifest m;
    m.formatVersion = 2;
    for (const auto& [name, wrinkle] : poses) {
        core::ExamplePose p;
        p.name = name;
        if (!wrinkle.empty()) p.wrinkle = wrinkle;
        m.poses.push_back(std::move(p));
    }
    return m;
}

std::vector<std::string_view> namesOf(const core::CorrectiveManifest& m) {
    std::vector<std::string_view> names;
    for (const core::ExamplePose& p : m.poses)
        names.emplace_back(p.name);
    return names;
}

}  // namespace

TEST_CASE("nothing fired means no wrinkle map at all", "[rig][wrinkle]") {
    // A Gaussian RBF never returns exactly zero -- measured 2.7e-16 on a pose
    // far from every centre -- so "no map" cannot mean "weight == 0". It means
    // below the threshold, and this is the case that keeps a bound-but-idle
    // wrinkle set off the screen instead of showing the first map it finds at
    // an invisible strength.
    const auto m =
        manifestOf({{"arm_up", "wrinkles/shoulder.png"}, {"arm_back", "wrinkles/elbow.png"}});
    const std::vector<double> w{2.7e-16, -1.1e-15};

    const auto choice = rig::chooseWrinkle(m, namesOf(m), w);
    CHECK(choice.map.empty());
    CHECK(choice.weight == 0.0F);
    CHECK(choice.dropped == 0);
}

TEST_CASE("one live pose gives its own map at its own weight", "[rig][wrinkle]") {
    const auto m = manifestOf({{"arm_up", "wrinkles/shoulder.png"}});
    const std::vector<double> w{0.7};

    const auto choice = rig::chooseWrinkle(m, namesOf(m), w);
    CHECK(choice.map == std::filesystem::path("wrinkles/shoulder.png"));
    CHECK_THAT(static_cast<double>(choice.weight), WithinAbs(0.7, 1e-6));
    CHECK(choice.dropped == 0);
}

TEST_CASE("poses sharing a map add up", "[rig][wrinkle]") {
    // The ordinary authoring case, and the reason the weights are SUMMED per
    // map rather than maxed: one crease sheet keyed at several example poses is
    // how a shoulder is authored, and two neighbouring poses each half-active
    // should show the sheet as fully on -- not as half on, which is what taking
    // the maximum would give.
    const auto m = manifestOf({{"a", "wrinkles/shoulder.png"}, {"b", "wrinkles/shoulder.png"}});
    const std::vector<double> w{0.3, 0.4};

    const auto choice = rig::chooseWrinkle(m, namesOf(m), w);
    CHECK(choice.map == std::filesystem::path("wrinkles/shoulder.png"));
    CHECK_THAT(static_cast<double>(choice.weight), WithinAbs(0.7, 1e-6));
    CHECK(choice.dropped == 0);
}

TEST_CASE("the strongest map wins and the rest are REPORTED", "[rig][wrinkle]") {
    // The case that matters. Two different sheets are live, the renderer takes
    // one, and the other is simply not shown.
    //
    // Reporting it is the whole point of `dropped`. Silently showing the
    // stronger sheet is how an author spends an afternoon wondering why the
    // elbow crease never appears -- the same class of failure as the live-rig
    // export that quietly carried no correctives.
    const auto m = manifestOf({{"shoulder", "wrinkles/shoulder.png"},
                               {"elbow", "wrinkles/elbow.png"},
                               {"knee", "wrinkles/knee.png"}});
    const std::vector<double> w{0.7, 0.2, 0.1};

    const auto choice = rig::chooseWrinkle(m, namesOf(m), w);
    CHECK(choice.map == std::filesystem::path("wrinkles/shoulder.png"));
    CHECK_THAT(static_cast<double>(choice.weight), WithinAbs(0.7, 1e-6));
    CHECK(choice.dropped == 2);
    // Named, not just counted: "2 maps dropped" is not something an author can
    // act on.
    CHECK(choice.droppedNames.find("elbow.png") != std::string::npos);
    CHECK(choice.droppedNames.find("knee.png") != std::string::npos);
    CHECK(choice.droppedNames.find("shoulder.png") == std::string::npos);
}

TEST_CASE("a negligible rival is not reported as dropped", "[rig][wrinkle]") {
    // Without this the warning fires on EVERY frame of every character with
    // more than one sheet, because a Gaussian never returns zero. A message
    // that always appears is a message nobody reads.
    const auto m =
        manifestOf({{"shoulder", "wrinkles/shoulder.png"}, {"elbow", "wrinkles/elbow.png"}});
    const std::vector<double> w{0.9, 1e-9};

    const auto choice = rig::chooseWrinkle(m, namesOf(m), w);
    CHECK(choice.map == std::filesystem::path("wrinkles/shoulder.png"));
    CHECK(choice.dropped == 0);
    CHECK(choice.droppedNames.empty());
}

TEST_CASE("a negative weight is not a live map", "[rig][wrinkle]") {
    // RBF weights overshoot BETWEEN example poses, so a negative one is
    // ordinary rather than exceptional. A negative crease depth has no meaning
    // -- and `abs` would give it one, inverting the sheet.
    const auto m =
        manifestOf({{"shoulder", "wrinkles/shoulder.png"}, {"elbow", "wrinkles/elbow.png"}});
    const std::vector<double> w{-0.8, 0.2};

    const auto choice = rig::chooseWrinkle(m, namesOf(m), w);
    CHECK(choice.map == std::filesystem::path("wrinkles/elbow.png"));
    CHECK_THAT(static_cast<double>(choice.weight), WithinAbs(0.2, 1e-6));
    CHECK(choice.dropped == 0);
}

TEST_CASE("the weight is clamped to one", "[rig][wrinkle]") {
    // Summing can overshoot, and the shader has no upper bound of its own: it
    // adds `weight * slope` to the base normal, so an unclamped 1.8 is a sheet
    // creased almost twice as deep as anything the author previewed.
    const auto m = manifestOf({{"a", "wrinkles/shoulder.png"}, {"b", "wrinkles/shoulder.png"}});
    const std::vector<double> w{0.8, 1.0};

    const auto choice = rig::chooseWrinkle(m, namesOf(m), w);
    CHECK(choice.weight == 1.0F);
}

TEST_CASE("a pose with no wrinkle map contributes nothing", "[rig][wrinkle]") {
    // Version 2 spells "none" as `""`, so a fully-active pose with no sheet is
    // an ordinary thing to find in a manifest -- most geometry correctives have
    // no wrinkle at all.
    const auto m = manifestOf({{"geometry_only", ""}, {"shoulder", "wrinkles/shoulder.png"}});
    const std::vector<double> w{1.0, 0.25};

    const auto choice = rig::chooseWrinkle(m, namesOf(m), w);
    CHECK(choice.map == std::filesystem::path("wrinkles/shoulder.png"));
    CHECK_THAT(static_cast<double>(choice.weight), WithinAbs(0.25, 1e-6));
    CHECK(choice.dropped == 0);
}

TEST_CASE("a version 1 manifest has no wrinkles to choose from", "[rig][wrinkle]") {
    // The compatibility path, and it must not be an error: every corrective set
    // authored before version 2 existed goes through here on every frame.
    core::CorrectiveManifest m = manifestOf({{"arm_up", ""}, {"arm_back", ""}});
    m.formatVersion            = 1;
    const std::vector<double> w{1.0, 1.0};

    const auto choice = rig::chooseWrinkle(m, namesOf(m), w);
    CHECK(choice.map.empty());
    CHECK(choice.weight == 0.0F);
    CHECK(choice.dropped == 0);
}

TEST_CASE("mismatched inputs choose nothing rather than guessing", "[rig][wrinkle]") {
    // `poseNames` comes from the compiled blob and the paths come from the
    // manifest, and the cache's hash check is what guarantees they agree. This
    // is what happens if that guarantee is ever broken: nothing is shown.
    //
    // Choosing by NAME rather than by index is the same instinct. An index
    // pairing would hand pose 0's weight to pose 0's map whatever the two
    // actually were, so a reordered blob would show the wrong sheet at the
    // wrong strength -- and look entirely plausible.
    const auto m = manifestOf({{"shoulder", "wrinkles/shoulder.png"}});

    SECTION("more weights than names") {
        const std::vector<double> w{0.9, 0.9};
        const auto choice = rig::chooseWrinkle(m, namesOf(m), w);
        CHECK(choice.map.empty());
    }

    SECTION("a name the manifest does not have") {
        const std::vector<std::string_view> names{"elbow"};
        const std::vector<double> w{0.9};
        const auto choice = rig::chooseWrinkle(m, names, w);
        CHECK(choice.map.empty());
    }
}
