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

#include <nlohmann/json.hpp>

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
    /// The reference's whole target stack: path relative to `data/targets`, and
    /// the weight it gave that target. Same key form our own `Human::stack()`
    /// uses, which is why `capture_fixture.py` anchors there.
    std::map<std::string, float> stack;
};

/// Reads cases.json with the JSON library the target already links.
///
/// This was a hand-rolled scan of `find("\"name\":")` and friends, on the
/// grounds that "the shape is fixed and shallow, so a targeted scan beats
/// adding a JSON dependency". The dependency was never added -- `mh_json` is
/// already linked into this target -- and the shape stopped being shallow the
/// moment the stack itself became worth reading.
std::vector<Case> loadCases() {
    std::ifstream in(fixtureDir() / "cases.json");
    if (!in) return {};

    const nlohmann::json doc = nlohmann::json::parse(in, nullptr, false);
    if (doc.is_discarded() || !doc.is_array()) return {};

    std::vector<Case> out;
    out.reserve(doc.size());
    for (const auto& entry : doc) {
        Case c;
        c.name      = entry.value("name", std::string{});
        c.stackSize = entry.value("stack_size", size_t{0});
        for (const auto& [k, v] : entry.value("settings", nlohmann::json::object()).items()) {
            c.settings[k] = v.get<float>();
        }
        for (const auto& [k, v] : entry.value("stack", nlohmann::json::object()).items()) {
            c.stack[k] = v.get<float>();
        }
        out.push_back(std::move(c));
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

    // data/targets, NOT data/ -- which is what `main.cpp:1441` builds and this
    // test did not. The root decides the KEY FORM the stack uses: from `data/`
    // every key carries a `targets/` prefix the application never sees. It went
    // unnoticed because nothing compared the keys, only their count; the moment
    // the stack itself is asserted against the reference, the two disagree on
    // every single entry.
    const auto idx = TargetIndex::build(std::filesystem::path(MH_DATA_DIR) / "targets");
    if (idx.componentCount() == 0) SKIP("target data not present");
    const auto mods = loadAllModifiers();
    REQUIRE_FALSE(mods.empty());

    auto baseMesh = loadObj(std::filesystem::path(MH_DATA_DIR) / "3dobjs" / "base.obj");
    REQUIRE(baseMesh.has_value());

    // Rooted at data/targets to MATCH the index above and `main.cpp:1445`.
    // The two roots must agree -- the index decides the key form, the library
    // resolves it -- and this test had both at `data/`, which is self-consistent
    // and is not what the application does.
    TargetLibrary targets(std::filesystem::path(MH_DATA_DIR) / "targets");

    for (const Case& c : cases) {
        INFO("case: " << c.name);

        Human h(&idx, mods);
        for (const auto& [full, value] : c.settings) {
            INFO("  setting " << full << " = " << value);
            REQUIRE(h.setModifierValue(full, value));
        }

        // The stack must match the reference's before the geometry can -- and
        // the SIZE alone does not say that. A stack that picks the wrong
        // targets, or the right targets with wrong weights, has the same count
        // and produces a different body; the geometry check below would catch
        // it, but as a wall of bad vertices rather than as "this target should
        // not be here".
        //
        // The fixture's keys were unreadable before this: `capture_fixture.py`
        // anchored them at a CWD-dependent path, so they were
        // `../../data/targets/...` or `../../../data/...` depending on where
        // the repo sat. Anchored at `data/targets` they are exactly the keys
        // `Human::stack()` uses, so the two are directly comparable.
        CHECK(h.stackSize() == c.stackSize);

        std::vector<std::string> onlyOurs;
        std::vector<std::string> onlyTheirs;
        std::vector<std::string> differentWeight;
        for (const auto& [key, weight] : c.stack) {
            const auto it = h.stack().find(key);
            if (it == h.stack().end()) {
                onlyTheirs.push_back(key);
            } else if (std::fabs(it->second - weight) > 1e-4F) {
                differentWeight.push_back(key + " (ref " + std::to_string(weight) + ", ours " +
                                          std::to_string(it->second) + ")");
            }
        }
        for (const auto& [key, weight] : h.stack()) {
            if (!c.stack.contains(key)) onlyOurs.push_back(key);
        }
        INFO("targets only the reference has: " << onlyTheirs.size());
        INFO("targets only we have: " << onlyOurs.size());
        INFO("targets with a different weight: " << differentWeight.size());
        for (const auto& k : onlyTheirs)
            INFO("  ref only: " << k);
        for (const auto& k : onlyOurs)
            INFO("  ours only: " << k);
        if (!differentWeight.empty()) INFO("  e.g. " << differentWeight.front());
        CHECK(onlyTheirs.empty());
        CHECK(onlyOurs.empty());
        CHECK(differentWeight.empty());

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
