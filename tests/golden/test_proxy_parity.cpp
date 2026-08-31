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

    // The fit's only body-dependent term is the TMatrix scale, and for the eye
    // proxies it is read off HEAD vertices (x_scale 5399 11998, y_scale 791 881,
    // z_scale 962 5320). neutral and mixed alone span a y-scale of just
    // 0.851..1.034; the four below take it to 0.417..1.168, so the extremes are
    // genuinely exercised rather than merely enumerated. Measured diagonals are
    // captured in tests/golden/proxy/tmatrix_scales.json.
    const std::map<std::string, std::map<std::string, float>> bodies{
        {"neutral", {}},
        {"mixed",
         {{"macrodetails/Gender", 1.0F},
          {"macrodetails/Age", 0.8F},
          {"macrodetails-universal/Muscle", 0.9F},
          {"macrodetails-height/Height", 0.75F}}},
        // Age 0.0 is an infant -- the largest head-to-body ratio the model makes.
        {"extreme_min",
         {{"macrodetails/Gender", 0.0F},
          {"macrodetails/Age", 0.0F},
          {"macrodetails-universal/Muscle", 0.0F},
          {"macrodetails-universal/Weight", 0.0F},
          {"macrodetails-height/Height", 0.0F},
          {"macrodetails-proportions/BodyProportions", 0.0F}}},
        {"extreme_max",
         {{"macrodetails/Gender", 1.0F},
          {"macrodetails/Age", 1.0F},
          {"macrodetails-universal/Muscle", 1.0F},
          {"macrodetails-universal/Weight", 1.0F},
          {"macrodetails-height/Height", 1.0F},
          {"macrodetails-proportions/BodyProportions", 1.0F}}},
        // Straight at the three axes the matrix divides by.
        {"head_small",
         {{"head/head-scale-depth-decr|incr", -1.0F},
          {"head/head-scale-horiz-decr|incr", -1.0F},
          {"head/head-scale-vert-decr|incr", -1.0F}}},
        {"head_large",
         {{"head/head-scale-depth-decr|incr", 1.0F},
          {"head/head-scale-horiz-decr|incr", 1.0F},
          {"head/head-scale-vert-decr|incr", 1.0F}}}};

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
    CHECK(compared == 18);  // 3 proxies x 6 bodies
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

// A fixture that cannot fail is not coverage. All 96 vertices of the low-poly
// eye proxy use the single-index form -- weights (1,0,0) and a zero offset --
// so the TMatrix term M*d is zero whatever the matrix says. Its six parity
// comparisons are therefore structurally incapable of catching a scale error.
//
// Verified by mutation, not assumed: swapping the y and z scale terms in
// fitProxy is caught by all 12 high-poly and base comparisons (worst delta
// 0.00046 to 0.646 dm) and by NONE of the six low-poly ones.
//
// That does not make them worthless -- they pin a different property, which
// this test states outright so it is not mistaken for scale coverage again.
TEST_CASE("the low-poly eye proxy fits exactly onto body vertices", "[core][proxy][exact]") {
    const auto path = std::filesystem::path(MH_DATA_DIR) / "eyes/low-poly/low-poly.mhclo";
    if (!std::filesystem::exists(path)) return;

    const auto p = loadProxy(path);
    REQUIRE(p.has_value());
    REQUIRE(p->exactFitOnly);
    REQUIRE(p->vertexCount() == 96);

    auto mesh = loadObj(std::filesystem::path(MH_DATA_DIR) / "3dobjs" / "base.obj");
    REQUIRE(mesh.has_value());

    std::vector<Vec3> fitted;
    REQUIRE(fitProxy(*p, mesh->coord(), fitted));
    REQUIRE(fitted.size() == 96);

    for (size_t i = 0; i < fitted.size(); ++i) {
        INFO("vertex " << i);
        // Weight (1,0,0) and a zero offset means the fitted point IS the body
        // vertex -- bit-exact, not approximately.
        const Vec3& body = mesh->coord()[p->refVerts[i][0]];
        CHECK(fitted[i].x == body.x);
        CHECK(fitted[i].y == body.y);
        CHECK(fitted[i].z == body.z);
        CHECK(p->offsets[i].x == 0.0F);
        CHECK(p->offsets[i].y == 0.0F);
        CHECK(p->offsets[i].z == 0.0F);
    }
}
