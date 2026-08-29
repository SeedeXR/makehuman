// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Parity of the macro factor derivations against the Python reference, over a
// sweep that includes every breakpoint in the piecewise age curve.
//
// tests/golden/macro_factors.json is generated from the reference's formulas
// (legacy-python/apps/human.py:517-888).

#include "makehuman/core/Macro.h"

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <filesystem>
#include <fstream>
#include <map>
#include <string>
#include <vector>

using namespace mh::core;

namespace {

struct Case {
    std::map<std::string, float> scalars;
    std::map<std::string, float> values;
};

/// Minimal reader for the flat two-level JSON this fixture uses. A general JSON
/// parser is not worth a dependency for one fixture with a known shape.
std::vector<Case> loadCases(const std::filesystem::path& p) {
    std::vector<Case> out;
    std::ifstream in(p);
    if (!in) return out;
    const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());

    size_t pos = 0;
    while ((pos = text.find("\"scalars\"", pos)) != std::string::npos) {
        Case c;
        for (const char* section : {"\"scalars\"", "\"values\""}) {
            const size_t sec = text.find(section, (section[1] == 's') ? pos : pos);
            if (sec == std::string::npos) break;
            const size_t open  = text.find('{', sec);
            const size_t close = text.find('}', open);
            if (open == std::string::npos || close == std::string::npos) break;

            auto& dst = (section[1] == 's') ? c.scalars : c.values;
            size_t k  = open;
            while ((k = text.find('"', k + 1)) != std::string::npos && k < close) {
                const size_t kEnd = text.find('"', k + 1);
                if (kEnd == std::string::npos || kEnd > close) break;
                const std::string key = text.substr(k + 1, kEnd - k - 1);
                const size_t col      = text.find(':', kEnd);
                if (col == std::string::npos || col > close) break;
                dst[key] = std::strtof(text.c_str() + col + 1, nullptr);
                k        = text.find(',', col);
                if (k == std::string::npos || k > close) break;
            }
            pos = close;
        }
        out.push_back(std::move(c));
        pos = text.find("\"scalars\"", pos);
        if (pos == std::string::npos) break;
    }
    return out;
}

float get(const std::map<std::string, float>& m, const std::string& k) {
    const auto it = m.find(k);
    return it == m.end() ? 0.0F : it->second;
}

}  // namespace

TEST_CASE("macro factor derivations match the Python reference",
          "[golden][parity][fixture][macro]") {
    const auto path = std::filesystem::path(MH_GOLDEN_DIR) / "macro_factors.json";
    if (!std::filesystem::exists(path)) SKIP("macro_factors.json not present");

    const auto cases = loadCases(path);
    REQUIRE(cases.size() >= 30);

    size_t compared = 0;
    for (const Case& c : cases) {
        MacroFactors f;
        f.setGender(get(c.scalars, "gender"));
        f.setAge(get(c.scalars, "age"));
        f.setMuscle(get(c.scalars, "muscle"));
        f.setWeight(get(c.scalars, "weight"));
        f.setHeight(get(c.scalars, "height"));
        f.setBreastSize(get(c.scalars, "cup"));
        f.setBreastFirmness(get(c.scalars, "firm"));
        f.setBodyProportions(get(c.scalars, "prop"));

        for (size_t i = 0; i < kMacroValueCount; ++i) {
            const auto v = static_cast<MacroValue>(i);
            const std::string name(macroValueName(v));
            const auto it = c.values.find(name);
            if (it == c.values.end()) continue;

            INFO("case gender=" << get(c.scalars, "gender") << " age=" << get(c.scalars, "age")
                                << " value=" << name);
            CHECK(std::abs(f.value(v) - it->second) < 1e-4F);
            ++compared;
        }
    }
    // 34 cases x 27 values, minus the three ethnic ones which the sweep holds
    // fixed and which are covered by the dedicated renormalisation tests.
    CHECK(compared >= 800);
}
