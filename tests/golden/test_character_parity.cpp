// SPDX-License-Identifier: AGPL-3.0-or-later
//
// End-to-end parity: modifier values -> macro factors -> target weights ->
// applied targets -> final vertex positions, compared against the Python
// reference for fourteen characters.
//
// This is the test that validates the whole parameterisation chain at once. The
// component parity tests can all pass while the composition is still wrong --
// the accumulate-vs-assign bug found in the previous iteration was exactly that
// shape.
//
// Regenerate with:
//     ./.venv-mh/bin/python tools/capture_fixture.py character

#include "makehuman/core/Modifier.h"
#include "makehuman/core/ObjReader.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <map>
#include <string>
#include <vector>

using namespace mh::core;

namespace {

std::filesystem::path fixtureDir() {
    return std::filesystem::path(MH_GOLDEN_DIR) / "character";
}

std::vector<float> readBlob(const std::filesystem::path& p) {
    std::ifstream in(p, std::ios::binary | std::ios::ate);
    if (!in) return {};
    const auto bytes = static_cast<size_t>(in.tellg());
    in.seekg(0);
    std::vector<float> out(bytes / sizeof(float));
    in.read(reinterpret_cast<char*>(out.data()), static_cast<std::streamsize>(bytes));
    return out;
}

struct Case {
    std::string name;
    std::map<std::string, float> settings;
    size_t stackSize{};
};

/// Extracts name / settings / stack_size from cases.json. The shape is fixed
/// and shallow, so a targeted scan beats adding a JSON dependency.
std::vector<Case> loadCases() {
    std::vector<Case> out;
    std::ifstream in(fixtureDir() / "cases.json");
    if (!in) return out;
    const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());

    size_t pos = 0;
    while ((pos = text.find("\"name\":", pos)) != std::string::npos) {
        Case c;
        const size_t nq = text.find('"', pos + 7);
        const size_t ne = text.find('"', nq + 1);
        c.name          = text.substr(nq + 1, ne - nq - 1);

        const size_t ss = text.find("\"stack_size\":", ne);
        if (ss != std::string::npos)
            c.stackSize = std::strtoul(text.c_str() + ss + 13, nullptr, 10);

        // "settings" is an object of "modifier/full/name": value pairs.
        const size_t setPos = text.find("\"settings\":", ne);
        if (setPos != std::string::npos) {
            const size_t open  = text.find('{', setPos);
            const size_t close = text.find('}', open);
            size_t k           = open;
            while ((k = text.find('"', k + 1)) != std::string::npos && k < close) {
                const size_t kEnd = text.find('"', k + 1);
                if (kEnd == std::string::npos || kEnd > close) break;
                const std::string key = text.substr(k + 1, kEnd - k - 1);
                const size_t col      = text.find(':', kEnd);
                if (col == std::string::npos || col > close) break;
                c.settings[key] = std::strtof(text.c_str() + col + 1, nullptr);
                k               = text.find(',', col);
                if (k == std::string::npos || k > close) break;
            }
        }
        out.push_back(std::move(c));
        pos = ne;
    }
    return out;
}

std::vector<Modifier> loadAllModifiers() {
    std::vector<Modifier> all;
    for (const char* f :
         {"modeling_modifiers.json", "measurement_modifiers.json", "bodyshapes_modifiers.json"}) {
        auto m = loadModifiers(std::filesystem::path(MH_DATA_DIR) / "modifiers" / f);
        if (m) all.insert(all.end(), m->begin(), m->end());
    }
    return all;
}

}  // namespace

TEST_CASE("character geometry matches the Python reference end to end",
          "[golden][parity][fixture][character]") {
    if (!std::filesystem::exists(fixtureDir() / "cases.json")) {
        SKIP("run tools/capture_fixture.py character first");
    }

    const auto cases = loadCases();
    REQUIRE(cases.size() == 14);

    const auto idx = TargetIndex::build(MH_DATA_DIR);
    if (idx.componentCount() == 0) SKIP("target data not present");
    const auto mods = loadAllModifiers();
    REQUIRE_FALSE(mods.empty());

    auto baseMesh = loadObj(std::filesystem::path(MH_DATA_DIR) / "3dobjs" / "base.obj");
    REQUIRE(baseMesh.has_value());

    TargetLibrary targets(MH_DATA_DIR);

    for (const Case& c : cases) {
        INFO("case: " << c.name);

        Human h(&idx, mods);
        for (const auto& [full, value] : c.settings) {
            INFO("  setting " << full << " = " << value);
            REQUIRE(h.setModifierValue(full, value));
        }

        // The stack must match the reference's before the geometry can.
        CHECK(h.stackSize() == c.stackSize);

        uint32_t missing = 0;
        h.applyStack(*baseMesh, targets, &missing);
        CHECK(missing == 0);

        const auto expected = readBlob(fixtureDir() / (c.name + ".bin"));
        REQUIRE(expected.size() == baseMesh->vertexCount() * 3);

        size_t bad  = 0;
        float worst = 0.0F;
        for (size_t i = 0; i < baseMesh->vertexCount(); ++i) {
            const Vec3& got = baseMesh->coord()[i];
            const float d   = std::max({std::abs(got.x - expected[i * 3 + 0]),
                                        std::abs(got.y - expected[i * 3 + 1]),
                                        std::abs(got.z - expected[i * 3 + 2])});
            worst           = std::max(worst, d);
            if (d > 1e-5F) ++bad;
        }
        INFO("  worst delta " << worst);
        CHECK(bad == 0);
    }
}

TEST_CASE("the applied character actually differs between cases",
          "[golden][parity][fixture][character]") {
    // Guards against the whole test passing because every case is the base
    // mesh -- if applyStack silently did nothing, every comparison above would
    // still succeed against a fixture that was also unchanged.
    if (!std::filesystem::exists(fixtureDir() / "male.bin")) SKIP("fixture not present");

    const auto def  = readBlob(fixtureDir() / "default.bin");
    const auto male = readBlob(fixtureDir() / "male.bin");
    REQUIRE(def.size() == male.size());
    REQUIRE_FALSE(def.empty());

    float worst = 0.0F;
    for (size_t i = 0; i < def.size(); ++i)
        worst = std::max(worst, std::abs(def[i] - male[i]));
    CHECK(worst > 0.1F);  // measured ~0.75 mesh units
}
