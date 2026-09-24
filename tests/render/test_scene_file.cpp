// SPDX-License-Identifier: Apache-2.0
//
// The scene loader, and chiefly the thing it REFUSES to do.
//
// `data/scenes/*.mhscene` are Python pickles. Reading one means executing
// whatever it names, which is why `memory/project_context.md` lists the format
// as a verified defect and CLAUDE.md rule 3 forbids porting it. The shipped
// pickles were converted offline, once, by `tools/convert_mhscene.py` under an
// unpickler that can build exactly one class -- and nothing in the application
// unpickles anything.
//
// That makes the pickles themselves the ideal CONTROL: they are real files of
// the dangerous format, they ship, and they sit beside the JSON the loader is
// supposed to read. A loader that quietly fell back to "malformed JSON", or
// worse grew a pickle path, fails the case below.

#include "makehuman/render/SceneFile.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <algorithm>
#include <filesystem>

using mh::render::availableScenes;
using mh::render::kBuiltinSceneName;
using mh::render::Lighting;
using mh::render::loadLighting;
using mh::render::RenderErrorKind;

namespace {

std::filesystem::path sceneDir() {
    return std::filesystem::path(MH_DATA_DIR) / "scenes";
}

float luminance(const std::array<float, 3>& rgb) {
    return 0.2126F * rgb[0] + 0.7152F * rgb[1] + 0.0722F * rgb[2];
}

float totalLightLuminance(const Lighting& lighting) {
    float total = 0.0F;
    for (const auto& light : lighting.lights) {
        total += luminance(light.colour) * light.intensity;
    }
    return total;
}

}  // namespace

TEST_CASE("a pickled scene is refused as a pickle, not as bad JSON", "[render][scene]") {
    // THE discriminating case. `default.mhscene` really is a pickle -- protocol
    // 2, first byte 0x80 -- so this is not a synthetic fixture standing in for
    // one.
    const auto result = loadLighting(sceneDir() / "default.mhscene");
    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().kind == RenderErrorKind::Malformed);
    // The wording is load-bearing: a user handed a scene from upstream
    // MakeHuman must be told WHAT the file is and what to do with it, not
    // handed the JSON parser's "illegal value at offset 0".
    INFO(result.error().message());
    CHECK_THAT(result.error().detail, Catch::Matchers::ContainsSubstring("Python pickle") &&
                                          Catch::Matchers::ContainsSubstring("convert_mhscene.py"));
}

TEST_CASE("the converted scenes load", "[render][scene]") {
    for (const char* name : {"default", "coldlights", "warmlights"}) {
        INFO(name);
        const auto lighting = loadLighting(sceneDir() / (std::string(name) + ".json"));
        REQUIRE(lighting.has_value());
        // MEASURED in `tools/convert_mhscene.py`'s output: default carries one
        // light, the other two carry two. A scene that silently loaded zero
        // lights would still be a valid `Lighting`, so the count is asserted
        // rather than inferred from the rig being non-empty.
        const size_t lit = static_cast<size_t>(
            std::count_if(lighting->lights.begin(), lighting->lights.end(),
                          [](const auto& light) { return light.intensity > 0.0F; }));
        CHECK(lit == (std::string(name) == "default" ? 1U : 2U));
    }
}

TEST_CASE("a scene is a change of colour, not of exposure", "[render][scene]") {
    // What the derived intensity is FOR. The reference's format has no
    // intensity at all, so reading its colours at 1.0 would make every
    // converted scene far darker than the default rig -- a change nobody
    // authored. The loader instead scales each scene to the default rig's total
    // light luminance.
    //
    // Asserting the property rather than the number: the scale is derived from
    // `Lighting{}`, so retuning the default rig must move both sides together.
    const float reference = totalLightLuminance(Lighting{});
    REQUIRE(reference > 0.0F);

    for (const char* name : {"default", "coldlights", "warmlights"}) {
        INFO(name);
        const auto lighting = loadLighting(sceneDir() / (std::string(name) + ".json"));
        REQUIRE(lighting.has_value());
        CHECK_THAT(totalLightLuminance(*lighting), Catch::Matchers::WithinRel(reference, 0.001F));
    }
}

TEST_CASE("a scene keeps the ratio between its own lights", "[render][scene]") {
    // The other half of the exposure match, and the reason it is ONE scale for
    // the whole scene rather than one per light. Normalising each light
    // separately would make every scene's lights equally bright and throw away
    // the only thing its author chose.
    //
    // MEASURED from the converted file: coldlights' two colours have
    // luminances 0.9043 and 0.7712, a ratio of 1.173.
    const auto lighting = loadLighting(sceneDir() / "coldlights.json");
    REQUIRE(lighting.has_value());
    const float first  = luminance(lighting->lights[0].colour) * lighting->lights[0].intensity;
    const float second = luminance(lighting->lights[1].colour) * lighting->lights[1].intensity;
    REQUIRE(second > 0.0F);
    INFO("ratio " << first / second);
    CHECK_THAT(first / second, Catch::Matchers::WithinRel(1.173F, 0.01F));
}

TEST_CASE("an unfilled light slot contributes nothing", "[render][scene]") {
    // Why the rig needs no light count. `default` has one light, so slots 1 and
    // 2 must stay at intensity 0 -- the shader multiplies radiance by that, so
    // a stale intensity there would light the model from a direction no scene
    // asked for.
    const auto lighting = loadLighting(sceneDir() / "default.json");
    REQUIRE(lighting.has_value());
    CHECK(lighting->lights[1].intensity == 0.0F);
    CHECK(lighting->lights[2].intensity == 0.0F);
}

TEST_CASE("a missing scene is FileMissing, not malformed", "[render][scene]") {
    // Zero is a meaningful answer elsewhere in this codebase and so is
    // "malformed"; a typo in a path must not read as a corrupt file.
    const auto result = loadLighting(sceneDir() / "no-such-scene.json");
    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().kind == RenderErrorKind::FileMissing);
}

TEST_CASE("the scene list leads with the built-in", "[render][scene]") {
    const auto names = availableScenes(sceneDir());
    REQUIRE(names.size() >= 4);
    // The default first, because a list whose head is not the default reads as
    // though the default were missing.
    CHECK(names.front() == std::string(kBuiltinSceneName));
    // And the converted three are all there, so a dropped file is visible.
    for (const char* name : {"default", "coldlights", "warmlights"}) {
        INFO(name);
        CHECK(std::find(names.begin(), names.end(), std::string(name)) != names.end());
    }
    // The pickles must NOT be offered: they cannot be loaded, and a chooser
    // that lists an entry it will then refuse is worse than one that does not.
    CHECK(std::find(names.begin(), names.end(), std::string("default.mhscene")) == names.end());
}
