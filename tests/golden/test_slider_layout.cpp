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
    const std::vector<std::string> expected{"Macro modelling", "Body shapes",   "Gender", "Face",
                                            "Torso",           "Arms and Legs", "Measure"};
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

    CHECK(standard->modifiers.size() == 291);
}
