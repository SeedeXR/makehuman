// SPDX-License-Identifier: AGPL-3.0-or-later
//
// The slider task-view registry, against the reference's own answer.
//
// Regenerate with:
//     ./.venv-mh/bin/python tools/capture_fixture.py slider_layout
//
// Order is the whole point here. A registry that contains the right sliders in
// the wrong order is a UI where every control has moved, and no assertion on
// counts alone would notice.
#include "makehuman/core/Modifier.h"
#include "makehuman/core/SliderLayout.h"

#include <nlohmann/json.hpp>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <map>
#include <string>
#include <vector>

using namespace mh;
using Catch::Matchers::WithinAbs;
using json = nlohmann::ordered_json;

namespace {

std::filesystem::path dataDir() {
    return std::filesystem::path(MH_DATA_DIR) / "modifiers";
}

/// Every modifier the three shipped files define, as the reference loads them.
/// The views in FILE order, which is what the fixture records. The shipped
/// modifiers come from loadStandardLayout so the three-file loop lives in one
/// place; only the ordering differs from what that function returns.
std::vector<foundation::TaskViewSpec> allViewsInFileOrder() {
    auto standard = core::loadStandardLayout(dataDir());
    REQUIRE(standard.has_value());
    std::vector<foundation::TaskViewSpec> views;
    for (const char* f :
         {"modeling_sliders.json", "bodyshapes_sliders.json", "measurement_sliders.json"}) {
        auto v = core::loadSliderLayout(dataDir() / f, standard->modifiers);
        REQUIRE(v.has_value());
        views.insert(views.end(), v->begin(), v->end());
    }
    return views;
}

json fixture() {
    std::ifstream in(std::filesystem::path(MH_GOLDEN_DIR) / "slider_layout" / "layout.json");
    REQUIRE(in);
    return json::parse(in);
}

}  // namespace

TEST_CASE("the label rule matches the reference's", "[sliders]") {
    // modifierslider.py:46-56, spelled out. Each case is a shape the rule has
    // to distinguish, not a random sample.
    CHECK(core::guessSliderLabel("head-age-decr|incr", "head") == "Age");
    CHECK(core::guessSliderLabel("head-angle-in|out", "head") == "Angle");
    CHECK(core::guessSliderLabel("head-oval", "head") == "Oval");
    // Leading component only dropped when it repeats the GROUP.
    CHECK(core::guessSliderLabel("head-oval", "torso") == "Head Oval");
    // A single component is never dropped, even if it is the group name --
    // dropping it would leave no label at all.
    CHECK(core::guessSliderLabel("head", "head") == "Head");
    // capitalize() lowercases the tail, so an all-caps source is normalised.
    CHECK(core::guessSliderLabel("bodyproportions", "x") == "Bodyproportions");
    CHECK(core::guessSliderLabel("l-eye-height1-min|max", "eyes") == "L Eye Height1");
}

TEST_CASE("every task view, section and slider matches the reference",
          "[sliders][golden][parity]") {
    const json want                                 = fixture();
    const std::vector<foundation::TaskViewSpec> got = allViewsInFileOrder();

    REQUIRE(got.size() == want.size());

    size_t sectionCount = 0;
    size_t sliderCount  = 0;
    size_t i            = 0;
    for (const auto& [viewName, wantView] : want.items()) {
        const foundation::TaskViewSpec& gotView = got[i++];
        INFO("task view " << viewName);
        // Task-view ORDER, not just membership.
        REQUIRE(gotView.name == viewName);

        const json& wantSections = wantView.at("sections");
        REQUIRE(gotView.sections.size() == wantSections.size());

        size_t s = 0;
        for (const auto& [sectionName, wantSliders] : wantSections.items()) {
            const foundation::SliderSection& gotSection = gotView.sections[s++];
            INFO("  section " << sectionName);
            REQUIRE(gotSection.name == sectionName);
            REQUIRE(gotSection.sliders.size() == wantSliders.size());
            ++sectionCount;

            for (size_t k = 0; k < wantSliders.size(); ++k) {
                const json& w                   = wantSliders[k];
                const foundation::SliderSpec& g = gotSection.sliders[k];
                INFO("    slider " << k << " " << w.at("mod").get<std::string>());
                CHECK(g.id == w.at("mod").get<std::string>());
                CHECK(g.label == w.at("label").get<std::string>());
                CHECK_THAT(static_cast<double>(g.minValue),
                           WithinAbs(w.at("min").get<double>(), 1e-6));
                CHECK_THAT(static_cast<double>(g.maxValue),
                           WithinAbs(w.at("max").get<double>(), 1e-6));
                CHECK_THAT(static_cast<double>(g.defaultValue),
                           WithinAbs(w.at("default").get<double>(), 1e-6));
                if (w.at("cam").is_string()) {
                    CHECK(g.camera == w.at("cam").get<std::string>());
                } else {
                    CHECK(g.camera.empty());
                }
                ++sliderCount;
            }
        }
    }

    // The captured totals, so a fixture that silently shrank is caught too.
    CHECK(got.size() == 7);
    CHECK(sectionCount == 50);
    CHECK(sliderCount == 291);
}

TEST_CASE("a slider naming an unknown modifier is dropped, not defaulted", "[sliders]") {
    // Loading the layout against an EMPTY modifier set: every entry is unknown,
    // so every section comes back empty rather than full of sliders that would
    // move nothing.
    const auto views = core::loadSliderLayout(dataDir() / "modeling_sliders.json", {});
    REQUIRE(views.has_value());
    CHECK_FALSE(views->empty());
    for (const auto& v : *views) {
        INFO("view " << v.name);
        size_t n = 0;
        for (const auto& sec : v.sections)
            n += sec.sliders.size();
        CHECK(n == 0);
        // The sections themselves survive: the tabs are still the tabs.
        CHECK_FALSE(v.sections.empty());
    }
}

TEST_CASE("a missing or malformed layout is reported", "[sliders]") {
    const auto missing = core::loadSliderLayout(dataDir() / "no-such-file.json", {});
    REQUIRE_FALSE(missing.has_value());
    CHECK(missing.error().kind == core::SliderLayoutErrorKind::NotFound);

    // A real file that is not a slider layout: valid JSON, wrong shape.
    const auto wrong = core::loadSliderLayout(dataDir() / "modeling_modifiers.json", {});
    REQUIRE_FALSE(wrong.has_value());
    CHECK(wrong.error().kind == core::SliderLayoutErrorKind::Malformed);
}

TEST_CASE("sort order and camera hints survive the load", "[sliders]") {
    const auto views = allViewsInFileOrder();
    const auto face =
        std::find_if(views.begin(), views.end(), [](const auto& v) { return v.name == "Face"; });
    REQUIRE(face != views.end());
    CHECK(face->hasSortOrder);
    CHECK_THAT(static_cast<double>(face->sortOrder), WithinAbs(0.2, 1e-6));
    CHECK(face->cameraView == "faceCamera");
}

TEST_CASE("loadStandardLayout puts the task views in the reference's tab order",
          "[sliders][golden][parity]") {
    const auto standard = core::loadStandardLayout(dataDir());
    REQUIRE(standard.has_value());

    // Not file order. gui3d.py:300-317 sorts by sortOrder, assigning a view
    // that gives none the lowest non-negative integer not already taken --
    // Measure is the only one, and gets 1 because 0 is used twice. Ties keep
    // load order, so Macro modelling (modeling) precedes Body shapes.
    // "Face units" is OURS -- the 52 bundled ARKit morphs, declared by
    // `faceunits_sliders.json`, which `loadStandardLayout` reads as a fourth
    // file alongside the reference's three. Listed here rather than filtered
    // out, so that a view appearing or vanishing still fails this.
    const std::vector<std::string> expected{
        "Macro modelling", "Body shapes",   "Gender",  "Face",
        "Torso",           "Arms and Legs", "Measure", "Face units"};
    std::vector<std::string> got;
    got.reserve(standard->views.size());
    for (const auto& v : standard->views)
        got.push_back(v.name);
    CHECK(got == expected);

    // Every view ends up with a sort order, including the one the file omits.
    for (const auto& v : standard->views) {
        INFO("view " << v.name);
        CHECK(v.hasSortOrder);
    }
    const auto measure = std::find_if(standard->views.begin(), standard->views.end(),
                                      [](const auto& v) { return v.name == "Measure"; });
    REQUIRE(measure != standard->views.end());
    CHECK_THAT(static_cast<double>(measure->sortOrder), WithinAbs(1.0, 1e-6));

    CHECK(standard->modifiers.size() == 343);
}

// The synonyms reach the SHIPPED sliders, not just a synthetic spec.
//
// `tests/ui/test_slider_search.cpp` drives the matching with specs it builds
// itself, so it says nothing about whether any real slider carries a keyword.
// Deleting the whole table would leave it green. This is the other half.
TEST_CASE("the shipped sliders carry the words a user searches for", "[slider][layout][search]") {
    const auto standard =
        mh::core::loadStandardLayout(std::filesystem::path(MH_DATA_DIR) / "modifiers");
    REQUIRE(standard.has_value());

    // id -> keywords, across every view and section.
    std::map<std::string, std::string> keywords;
    for (const auto& view : standard->views) {
        for (const auto& section : view.sections) {
            for (const auto& slider : section.sliders)
                keywords[slider.id] = slider.keywords;
        }
    }

    // The three the owner actually searched for and did not find. The labels
    // are Weight, Muscle and Stomach tone, so none of these words is reachable
    // without the table.
    struct Wanted {
        std::string id;
        std::string word;
    };

    for (const Wanted& w : std::vector<Wanted>{{"macrodetails-universal/Weight", "chubby"},
                                               {"macrodetails-universal/Muscle", "toned"},
                                               {"stomach/stomach-tone-decr|incr", "sixpack"}}) {
        INFO(w.id << " should be findable by \"" << w.word << "\"");
        const auto at = keywords.find(w.id);
        REQUIRE(at != keywords.end());
        CHECK(at->second.find(w.word) != std::string::npos);
    }

    // ...and the table is not applied to everything. A nose slider has no
    // business carrying body synonyms, and a search that matches every row is
    // the same as a search that matches none.
    const auto nose = keywords.find("nose/nose-scale-depth-decr|incr");
    REQUIRE(nose != keywords.end());
    CHECK(nose->second.empty());
}

// A preset naming a modifier that does not exist is REFUSED, not skipped.
//
// Skipping is the tempting choice and it is wrong: the click would work, the
// body would not move, and nothing would say why -- which is precisely how a
// recipe rots unnoticed after someone renames a slider.
TEST_CASE("a preset naming an unknown modifier is refused", "[slider][layout][preset]") {
    const auto standard =
        mh::core::loadStandardLayout(std::filesystem::path(MH_DATA_DIR) / "modifiers");
    REQUIRE(standard.has_value());

    // The shipped file loads, and every preset names real modifiers.
    const auto shipped = mh::core::loadCombinationPresets(
        std::filesystem::path(MH_DATA_DIR) / "modifiers", standard->modifiers);
    REQUIRE(shipped.has_value());
    CHECK(shipped->size() == 5);
    // Every preset sets MORE THAN ONE slider, which is the whole point of the
    // feature: a one-slider preset is a slider.
    for (const auto& preset : *shipped) {
        INFO("preset " << preset.name);
        CHECK(preset.values.size() >= 2);
    }

    // Now the refusal, against a modifier list that is missing them.
    const std::vector<mh::core::Modifier> none;
    const auto refused =
        mh::core::loadCombinationPresets(std::filesystem::path(MH_DATA_DIR) / "modifiers", none);
    REQUIRE_FALSE(refused.has_value());

    // A data directory with no presets file is NOT an error -- it simply
    // offers none, the way a rig without a retarget table renames nothing.
    const auto absent = mh::core::loadCombinationPresets(
        std::filesystem::path(MH_DATA_DIR) / "poses", standard->modifiers);
    REQUIRE(absent.has_value());
    CHECK(absent->empty());
}
