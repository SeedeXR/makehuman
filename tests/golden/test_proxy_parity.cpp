// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Proxy fitting parity: the barycentric result for each shipped proxy, on two
// different bodies, compared against the reference's getCoords().
//
// Two bodies matter here. The TMatrix offset rescaling is identity-like on the
// neutral shape, so a bug in it only surfaces once proportions change.
//
// Regenerate with:
//     ./.venv-mh/bin/python tools/capture_fixture.py proxy

#include "makehuman/core/Proxy.h"

#include "makehuman/core/Mhm.h"
#include "makehuman/core/Modifier.h"
#include "makehuman/core/ObjReader.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <map>
#include <string>
#include <vector>

using namespace mh::core;

namespace {

std::filesystem::path proxyDir() {
    return std::filesystem::path(MH_GOLDEN_DIR) / "proxy";
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

std::vector<Modifier> loadAllModifiers() {
    std::vector<Modifier> all;
    for (const char* f :
         {"modeling_modifiers.json", "measurement_modifiers.json", "bodyshapes_modifiers.json"}) {
        auto m = loadModifiers(std::filesystem::path(MH_DATA_DIR) / "modifiers" / f);
        if (m) all.insert(all.end(), m->begin(), m->end());
    }
    return all;
}

struct ProxyRef {
    const char* relative;
    const char* stem;
};

constexpr std::array<ProxyRef, 3> kProxies{{
    {"eyes/high-poly/high-poly.mhclo", "high-poly"},
    {"eyes/low-poly/low-poly.mhclo", "low-poly"},
    {"3dobjs/base.mhclo", "base"},
}};

}  // namespace

TEST_CASE("shipped proxies parse with the expected shape", "[core][proxy]") {
    for (const ProxyRef& r : kProxies) {
        const auto path = std::filesystem::path(MH_DATA_DIR) / r.relative;
        if (!std::filesystem::exists(path)) continue;
        INFO(r.relative);

        const auto p = loadProxy(path);
        REQUIRE(p.has_value());
        CHECK(p->vertexCount() > 0);
        CHECK(p->refVerts.size() == p->weights.size());
        CHECK(p->refVerts.size() == p->offsets.size());
        CHECK_FALSE(p->uuid.empty());
        // Not every proxy targets hm08: data/3dobjs/base.mhclo is the alpha-7
        // converter and declares "alpha_7". Only that the field is populated is
        // universal.
        CHECK_FALSE(p->basemesh.empty());

        // Every reference index must be inside the base mesh.
        CHECK(p->maxRefIndex() < 19158);
    }
}

TEST_CASE("the eye proxy has the counts the reference reports", "[core][proxy]") {
    const auto path = std::filesystem::path(MH_DATA_DIR) / "eyes/high-poly/high-poly.mhclo";
    if (!std::filesystem::exists(path)) SKIP("eye proxy not present");

    const auto p = loadProxy(path);
    REQUIRE(p.has_value());
    // Measured from the reference: 1,064 proxy vertices.
    CHECK(p->vertexCount() == 1064);
    CHECK(p->basemesh == "hm08");
    CHECK(p->zDepth == 50);
    CHECK(p->uuid == "2c12f43b-1303-432c-b7ce-d78346baf2e6");
    // It declares all three scale axes (x_scale / y_scale / z_scale).
    CHECK_FALSE(p->tmatrix.isIdentity());
}

TEST_CASE("barycentric weights sum to about one", "[core][proxy]") {
    // A barycentric coordinate on a triangle; the shipped assets are authored
    // that way even though the format does not enforce it.
    const auto path = std::filesystem::path(MH_DATA_DIR) / "eyes/high-poly/high-poly.mhclo";
    if (!std::filesystem::exists(path)) SKIP("eye proxy not present");

    const auto p = loadProxy(path);
    REQUIRE(p.has_value());

    size_t bad = 0;
    for (const auto& w : p->weights) {
        const float sum = w[0] + w[1] + w[2];
        if (std::abs(sum - 1.0F) > 1e-3F) ++bad;
    }
    CHECK(bad == 0);
}

TEST_CASE("fitProxy rejects a body the proxy does not fit", "[core][proxy]") {
    Proxy p;
    p.refVerts     = {{0, 1, 2}};
    p.weights      = {{1.0F, 0.0F, 0.0F}};
    p.offsets      = {Vec3{}};
    p.maxRefIndex_ = 99;

    const std::vector<Vec3> tiny(3, Vec3{});
    std::vector<Vec3> out;
    CHECK_FALSE(fitProxy(p, tiny, out));
}

TEST_CASE("an exact-fit vertex lands exactly on its base vertex", "[core][proxy]") {
    // The single-index form: weights (1,0,0) and a zero offset (proxy.py:710-717).
    Proxy p;
    p.refVerts     = {{2, 0, 1}};
    p.weights      = {{1.0F, 0.0F, 0.0F}};
    p.offsets      = {Vec3{}};
    p.maxRefIndex_ = 2;

    const std::vector<Vec3> body{{0, 0, 0}, {1, 1, 1}, {5, 6, 7}};
    std::vector<Vec3> out;
    REQUIRE(fitProxy(p, body, out));
    REQUIRE(out.size() == 1);
    CHECK(out[0] == Vec3{5, 6, 7});
}

TEST_CASE("proxy fitting matches the reference on two bodies", "[golden][parity][fixture][proxy]") {
    if (!std::filesystem::exists(proxyDir() / "proxies.json")) {
        SKIP("run tools/capture_fixture.py proxy first");
    }

    const auto idx = TargetIndex::build(MH_DATA_DIR);
    if (idx.componentCount() == 0) SKIP("target data not present");
    const auto mods = loadAllModifiers();
    REQUIRE_FALSE(mods.empty());

    auto mesh = loadObj(std::filesystem::path(MH_DATA_DIR) / "3dobjs" / "base.obj");
    REQUIRE(mesh.has_value());
    TargetLibrary targets(MH_DATA_DIR);

    const std::map<std::string, std::map<std::string, float>> bodies{
        {"neutral", {}},
        {"mixed",
         {{"macrodetails/Gender", 1.0F},
          {"macrodetails/Age", 0.8F},
          {"macrodetails-universal/Muscle", 0.9F},
          {"macrodetails-height/Height", 0.75F}}}};

    size_t compared = 0;
    for (const auto& [bodyName, settings] : bodies) {
        Human h(&idx, mods);
        for (const auto& [full, value] : settings)
            REQUIRE(h.setModifierValue(full, value));
        h.applyStack(*mesh, targets);

        for (const ProxyRef& r : kProxies) {
            const auto path = std::filesystem::path(MH_DATA_DIR) / r.relative;
            const auto blob = proxyDir() / (std::string(r.stem) + "_" + bodyName + ".bin");
            if (!std::filesystem::exists(path) || !std::filesystem::exists(blob)) continue;
            INFO(r.stem << " on " << bodyName);

            const auto p = loadProxy(path);
            REQUIRE(p.has_value());

            std::vector<Vec3> fitted;
            REQUIRE(fitProxy(*p, mesh->coord(), fitted));

            const auto expected = readBlob(blob);
            REQUIRE(expected.size() == fitted.size() * 3);

            size_t bad  = 0;
            float worst = 0.0F;
            for (size_t i = 0; i < fitted.size(); ++i) {
                const float d = std::max({std::abs(fitted[i].x - expected[i * 3 + 0]),
                                          std::abs(fitted[i].y - expected[i * 3 + 1]),
                                          std::abs(fitted[i].z - expected[i * 3 + 2])});
                worst         = std::max(worst, d);
                if (d > 1e-5F) ++bad;
            }
            INFO("worst delta " << worst);
            CHECK(bad == 0);
            ++compared;
        }
    }
    CHECK(compared == 6);  // 3 proxies x 2 bodies
}

// The reference parses nine shear keys (`shared/proxy.py:476-492`) and builds
// the fit matrix from an affine solve over point correspondences
// (`matrixFromShear` -> `affine_matrix_from_points`). We implement only the
// three diagonal `*_scale` forms.
//
// Until this, a `.mhclo` using shear parsed "successfully" and the shear was
// silently dropped -- the proxy then fitted with the wrong transform and
// nothing said so. Refusing is worse than supporting it and far better than
// pretending.
TEST_CASE("a proxy using shear is refused, not silently mis-fitted", "[proxy][shear]") {
    const auto path = std::filesystem::temp_directory_path() / "mh_shear.mhclo";
    {
        std::ofstream f(path);
        f << "name ShearedThing\n"
          << "basemesh hm08\n"
          << "x_scale 5399 11998 1.4800\n"
          << "shear_x 5399 11998 0.1 0.9\n"
          << "verts 0\n"
          << "0\n";
    }

    const auto proxy = mh::core::loadProxy(path);
    REQUIRE_FALSE(proxy.has_value());
    INFO(proxy.error().message());
    CHECK(proxy.error().detail.find("shear_x") != std::string::npos);

    std::error_code ec;
    std::filesystem::remove(path, ec);
}

// Every one of the nine spellings must be caught, not just the unprefixed set:
// a left/right asset that slipped through would mis-fit exactly as before.
TEST_CASE("every shear spelling is refused", "[proxy][shear]") {
    for (const char* key : {"shear_x", "shear_y", "shear_z", "l_shear_x", "l_shear_y", "l_shear_z",
                            "r_shear_x", "r_shear_y", "r_shear_z"}) {
        const auto path = std::filesystem::temp_directory_path() / "mh_shear_each.mhclo";
        {
            std::ofstream f(path);
            f << "name T\nbasemesh hm08\n" << key << " 1 2 0.1 0.9\nverts 0\n0\n";
        }
        const auto proxy = mh::core::loadProxy(path);
        INFO(key);
        CHECK_FALSE(proxy.has_value());
        std::error_code ec;
        std::filesystem::remove(path, ec);
    }
}
