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

#include <nlohmann/json.hpp>

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

// The nine shear keys used to be REFUSED here, and that refusal was right for
// as long as the form was unimplemented: a `.mhclo` using shear once parsed
// "successfully" with the shear silently dropped, which fitted the proxy with
// the wrong transform and reported success.
//
// They are implemented now (2026-09-08), so these two cases assert the opposite
// of what they used to -- kept rather than deleted, because the thing worth
// pinning is unchanged: **every one of the nine spellings is handled, and none
// is silently dropped.** A left/right asset that slipped through would mis-fit
// exactly as before.
TEST_CASE("a proxy using shear loads, and scale still wins", "[proxy][shear]") {
    const auto path = std::filesystem::temp_directory_path() / "mh_shear.mhclo";
    {
        std::ofstream f(path);
        f << "name ShearedThing\n"
          << "basemesh hm08\n"
          << "x_scale 5399 11998 1.4800\n"
          << "shear_x 5399 11998 0.1 0.9\n"
          << "shear_y 5399 11998 0.1 0.9\n"
          << "shear_z 5399 11998 0.1 0.9\n"
          << "verts 0\n"
          << "0\n";
    }

    const auto proxy = mh::core::loadProxy(path);
    REQUIRE(proxy.has_value());
    // Both forms are recorded, and `getMatrix`'s precedence decides which is
    // used -- scale, because it is present at all (`proxy.py:900-918`).
    REQUIRE(proxy->tmatrix.scale[0].has_value());
    REQUIRE(proxy->tmatrix.shear[0].has_value());

    std::error_code ec;
    std::filesystem::remove(path, ec);
}

TEST_CASE("every shear spelling reaches its own slot", "[proxy][shear]") {
    struct Case {
        const char* key;
        int form;  // 0 = unsided, 1 = left, 2 = right
        size_t axis;
    };

    for (const Case& c :
         {Case{"shear_x", 0, 0}, Case{"shear_y", 0, 1}, Case{"shear_z", 0, 2},
          Case{"l_shear_x", 1, 0}, Case{"l_shear_y", 1, 1}, Case{"l_shear_z", 1, 2},
          Case{"r_shear_x", 2, 0}, Case{"r_shear_y", 2, 1}, Case{"r_shear_z", 2, 2}}) {
        const auto path = std::filesystem::temp_directory_path() / "mh_shear_each.mhclo";
        {
            // All three axes of the form under test, so the file is complete --
            // a partial spec is refused, which its own case covers.
            const std::string prefix = std::string(c.key).substr(0, std::string(c.key).size() - 1);
            std::ofstream f(path);
            f << "name T\nbasemesh hm08\n";
            for (const char* ax : {"x", "y", "z"})
                f << prefix << ax << " 1 2 0.1 0.9\n";
            f << "verts 0\n0\n";
        }
        const auto proxy = mh::core::loadProxy(path);
        INFO(c.key);
        REQUIRE(proxy.has_value());
        const auto& t = proxy->tmatrix;
        const std::array<std::optional<mh::core::TMatrix::Shear>, 3>& slot =
            c.form == 0 ? t.shear : (c.form == 1 ? t.leftShear : t.rightShear);
        CHECK(slot[c.axis].has_value());
        // ...and nothing landed in the other two forms.
        CHECK((c.form == 0) == t.shear[c.axis].has_value());
        CHECK((c.form == 1) == t.leftShear[c.axis].has_value());
        CHECK((c.form == 2) == t.rightShear[c.axis].has_value());
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

// `TMatrix` shear, against the reference's own solve.
//
// The nine `shear_*` keys were REFUSED rather than implemented, on the grounds
// that no shipped `.mhclo` uses them and that implementing needed "a general
// SVD-based affine solve". Measuring the reference settled both halves of that:
//
//  * both boxes its solve receives are AXIS-ALIGNED -- source corners built per
//    axis from the two authored coordinates, target corners from one component
//    of two base vertices -- so the exact affine map is DIAGONAL. Off-diagonal
//    terms come back at 4.6e-15, which is float noise. No SVD is needed and no
//    shear is expressible; the keys are a signed per-axis scale.
//  * the fixture is generated by the oracle
//    (`tools/capture_fixture.py shear`), so "untestable machinery" no longer
//    applies -- the same call as `.mhpose`.
//
// The degenerate cases are why this is a fixture and not a formula I derived: a
// zero source extent yields **0**, not an infinity, because the least-squares
// solve collapses that axis. The obvious ratio would give inf and a mesh of
// NaNs.
TEST_CASE("the shear diagonal matches the reference's affine solve",
          "[core][proxy][shear][golden][parity]") {
    std::ifstream in(std::filesystem::path(MH_GOLDEN_DIR) / "shear" / "cases.json");
    REQUIRE(in);
    nlohmann::json spec;
    in >> spec;

    // The reference's own measure of how far from diagonal its solve lands.
    CHECK(spec["worst_off_diagonal"].get<double>() < 1e-12);
    REQUIRE(spec["cases"].size() == 5);

    for (const auto& c : spec["cases"]) {
        const std::string label = c["label"].get<std::string>();
        CAPTURE(label);

        std::vector<mh::foundation::Vec3> coords;
        for (const auto& co : c["coords"]) {
            coords.push_back({co[0].get<float>(), co[1].get<float>(), co[2].get<float>()});
        }

        mh::core::TMatrix t;
        for (size_t n = 0; n < 3; ++n) {
            const auto& e = c["shear"][n];
            t.shear[n]    = mh::core::TMatrix::Shear{e[0].get<uint32_t>(), e[1].get<uint32_t>(),
                                                  e[2].get<float>(), e[3].get<float>()};
        }
        CHECK_FALSE(t.isIdentity());

        const mh::foundation::Vec3 got = t.diagonal(coords);
        const float want[3] = {c["diagonal"][0].get<float>(), c["diagonal"][1].get<float>(),
                               c["diagonal"][2].get<float>()};
        CHECK(std::abs(got.x - want[0]) < 1e-5F);
        CHECK(std::abs(got.y - want[1]) < 1e-5F);
        CHECK(std::abs(got.z - want[2]) < 1e-5F);
    }
}

// Scale wins over shear, and the sided forms come after the unsided one --
// `getMatrix`'s own order (`shared/proxy.py:900-918`). A file carrying both is
// odd, but the precedence has to be the reference's or the same asset fits
// differently in the two applications.
TEST_CASE("scale takes precedence over shear, and unsided over sided", "[core][proxy][shear]") {
    const std::vector<mh::foundation::Vec3> coords{{0.0F, 0.0F, 0.0F}, {4.0F, 4.0F, 4.0F}};

    mh::core::TMatrix t;
    for (size_t n = 0; n < 3; ++n) {
        t.shear[n]      = mh::core::TMatrix::Shear{0, 1, 0.0F, 1.0F};  // -> 4
        t.leftShear[n]  = mh::core::TMatrix::Shear{0, 1, 0.0F, 2.0F};  // -> 2
        t.rightShear[n] = mh::core::TMatrix::Shear{0, 1, 0.0F, 4.0F};  // -> 1
    }
    CHECK(std::abs(t.diagonal(coords).x - 4.0F) < 1e-5F);

    mh::core::TMatrix sided;
    for (size_t n = 0; n < 3; ++n) {
        sided.leftShear[n]  = mh::core::TMatrix::Shear{0, 1, 0.0F, 2.0F};
        sided.rightShear[n] = mh::core::TMatrix::Shear{0, 1, 0.0F, 4.0F};
    }
    CHECK(std::abs(sided.diagonal(coords).x - 2.0F) < 1e-5F);

    mh::core::TMatrix withScale = t;
    withScale.scale[0]          = mh::core::TMatrix::Scale{0, 1, 8.0F};  // -> |0-4|/8 = 0.5
    CHECK(std::abs(withScale.diagonal(coords).x - 0.5F) < 1e-5F);
    // The scale form is per-axis and the others are not consulted at all once
    // any scale entry exists, exactly as `getMatrix` reads `if self.scaleData`.
    CHECK(std::abs(withScale.diagonal(coords).y - 1.0F) < 1e-5F);
}

// The nine keys, parsed. They were refused by name until 2026-09-08; the
// refusal was honest but it turned away files MakeHuman itself reads.
TEST_CASE("a .mhclo declaring shear parses and fits", "[core][proxy][shear]") {
    const auto dir = std::filesystem::temp_directory_path() / "mh_shear_proxy";
    std::filesystem::create_directories(dir);
    const auto write = [&](const char* stem, std::string_view body) {
        const auto p = dir / stem;
        std::ofstream out(p);
        out << body;
        out.close();
        return p;
    };

    // All three axes, unsided. `verts` is required or the file has no mapping.
    const auto full   = write("full.mhclo", R"(name shearful
obj_file x.obj
shear_x 0 1 -1.0 1.0
shear_y 0 1 -1.0 1.0
shear_z 0 1 -1.0 1.0
verts
0
)");
    const auto loaded = mh::core::loadProxy(full);
    REQUIRE(loaded.has_value());
    REQUIRE(loaded->tmatrix.shear[0].has_value());
    REQUIRE(loaded->tmatrix.shear[2].has_value());
    CHECK(loaded->tmatrix.shear[0]->v1 == 0);
    CHECK(loaded->tmatrix.shear[0]->v2 == 1);
    CHECK(std::abs(loaded->tmatrix.shear[0]->x1 + 1.0F) < 1e-6F);
    CHECK(std::abs(loaded->tmatrix.shear[0]->x2 - 1.0F) < 1e-6F);
    CHECK_FALSE(loaded->tmatrix.isIdentity());

    // ...and it reaches the fit: a body twice the authored span doubles it.
    const std::vector<mh::foundation::Vec3> coords{{-2.0F, -2.0F, -2.0F}, {2.0F, 2.0F, 2.0F}};
    const mh::foundation::Vec3 d = loaded->tmatrix.diagonal(coords);
    CHECK(std::abs(d.x - 2.0F) < 1e-5F);

    // The sided spellings land in the sided slots, and only there.
    const auto sided = mh::core::loadProxy(write("sided.mhclo", R"(name sided
obj_file x.obj
l_shear_x 0 1 -1.0 1.0
l_shear_y 0 1 -1.0 1.0
l_shear_z 0 1 -1.0 1.0
r_shear_x 0 1 -2.0 2.0
r_shear_y 0 1 -2.0 2.0
r_shear_z 0 1 -2.0 2.0
verts
0
)"));
    REQUIRE(sided.has_value());
    CHECK_FALSE(sided->tmatrix.shear[0].has_value());
    REQUIRE(sided->tmatrix.leftShear[1].has_value());
    REQUIRE(sided->tmatrix.rightShear[1].has_value());
    CHECK(std::abs(sided->tmatrix.rightShear[1]->x2 - 2.0F) < 1e-6F);

    // A PARTIAL spec is refused. The reference unpacks all three axes
    // unconditionally (`matrixFromShear`, `proxy.py:927-930`), so a file naming
    // only `shear_x` raises a TypeError there -- it is a broken file, and
    // saying so beats fitting it with two identity axes.
    const auto partial = mh::core::loadProxy(write("partial.mhclo", R"(name partial
obj_file x.obj
shear_x 0 1 -1.0 1.0
verts
0
)"));
    REQUIRE_FALSE(partial.has_value());
    CHECK(partial.error().kind == mh::core::ProxyErrorKind::Unsupported);

    // A shear line missing its numbers is malformed, not ignored.
    const auto short_line = mh::core::loadProxy(write("short.mhclo", R"(name short
obj_file x.obj
shear_x 0 1
verts
0
)"));
    REQUIRE_FALSE(short_line.has_value());
}
