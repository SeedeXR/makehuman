// SPDX-License-Identifier: AGPL-3.0-or-later
#include "makehuman/core/Material.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <algorithm>
#include <filesystem>
#include <fstream>

using Catch::Matchers::WithinAbs;
using namespace mh::core;

namespace {

class TempMat {
public:
    explicit TempMat(std::string_view contents) {
        static int counter = 0;
        path_              = std::filesystem::temp_directory_path() /
                ("mh_mat_" + std::to_string(++counter) + ".mhmat");
        std::ofstream out(path_);
        out << contents;
    }

    ~TempMat() {
        std::error_code ec;
        std::filesystem::remove(path_, ec);
    }

    TempMat(const TempMat&)            = delete;
    TempMat& operator=(const TempMat&) = delete;

    [[nodiscard]] const std::filesystem::path& path() const { return path_; }

private:
    std::filesystem::path path_;
};

bool hasDefine(const std::vector<std::string>& v, std::string_view d) {
    return std::ranges::find(v, d) != v.end();
}

}  // namespace

TEST_CASE("the default skin parses as the reference describes it", "[core][material][golden]") {
    const auto path = std::filesystem::path(MH_DATA_DIR) / "skins" / "default.mhmat";
    if (!std::filesystem::exists(path)) SKIP("default skin not present");

    const auto m = loadMaterial(path);
    REQUIRE(m.has_value());

    CHECK(m->name == "DefaultSkin");
    CHECK(m->autoBlendSkin);  // drives the ethnic litsphere blend
    CHECK(m->sssEnabled);
    CHECK_THAT(m->sssRScale, WithinAbs(4.0, 1e-5));
    CHECK_THAT(m->sssGScale, WithinAbs(2.0, 1e-5));
    CHECK_THAT(m->sssBScale, WithinAbs(1.0, 1e-5));
    CHECK_THAT(m->shininess, WithinAbs(0.96, 1e-5));
    CHECK(m->shader.string().ends_with("litsphere"));

    // It deliberately disables diffuse and vertex colours: the litsphere
    // provides shading, and the skin tone comes from the blended matcap.
    CHECK_FALSE(m->shaderConfig.diffuse);
    CHECK_FALSE(m->shaderConfig.vertexColors);
    CHECK(m->shaderConfig.bump);

    // No diffuse texture ships with it.
    CHECK_FALSE(m->texture(TextureChannel::Diffuse).present());
}

TEST_CASE("the eye material parses with its texture and shader params",
          "[core][material][golden]") {
    const auto path = std::filesystem::path(MH_DATA_DIR) / "eyes/materials/brown.mhmat";
    if (!std::filesystem::exists(path)) SKIP("eye material not present");

    const auto m = loadMaterial(path);
    REQUIRE(m.has_value());

    CHECK(m->name == "Eye_brown");
    CHECK(m->transparent);
    CHECK(m->alphaToCoverage);
    CHECK(m->texture(TextureChannel::Diffuse).present());
    CHECK(m->texture(TextureChannel::Diffuse).path.filename() == "brown_eye.png");

    // Texture paths resolve relative to the .mhmat's own directory.
    CHECK(m->texture(TextureChannel::Diffuse).path.is_absolute());

    REQUIRE(m->shaderParams.contains("AdditiveShading"));
    CHECK(m->shaderParams.at("AdditiveShading").at(0) == "0.5");
    REQUIRE(m->shaderParams.contains("litsphereTexture"));
}

TEST_CASE("every shipped material parses", "[core][material][golden]") {
    const auto root = std::filesystem::path(MH_DATA_DIR);
    if (!std::filesystem::exists(root)) SKIP("data not present");

    size_t ok = 0, failed = 0;
    for (const auto& e : std::filesystem::recursive_directory_iterator(root)) {
        if (e.path().extension() != ".mhmat") continue;
        const auto m = loadMaterial(e.path());
        if (m) {
            ++ok;
        } else {
            ++failed;
            UNSCOPED_INFO("failed: " << m.error().message());
        }
    }
    CHECK(failed == 0);
    // 3 originals (xray, default skin, brown eye) + the 8 generated skin tones
    // (tools/make_skins.py). The count is pinned rather than left open because
    // "every material parses" passes trivially when the loop finds none.
    CHECK(ok == 11);
}

TEST_CASE("booleans accept the reference's spellings", "[core][material]") {
    // material.py:357-358 -- yes / enabled / true, case-insensitively.
    const TempMat a("transparent yes\nbackfaceCull ENABLED\ndepthless True\nwireframe no\n");
    const auto m = loadMaterial(a.path());
    REQUIRE(m.has_value());
    CHECK(m->transparent);
    CHECK(m->backfaceCull);
    CHECK(m->depthless);
    CHECK_FALSE(m->wireframe);
}

TEST_CASE("comments are only comments as the first token", "[core][material]") {
    // material.py:364-365 checks lineData[0], so a '#' mid-line is data.
    const TempMat a("# a comment\n// also a comment\nname Real\n");
    const auto m = loadMaterial(a.path());
    REQUIRE(m.has_value());
    CHECK(m->name == "Real");
}

TEST_CASE("colours are clamped to zero..one", "[core][material]") {
    const TempMat a("diffuseColor 2.0 -1.0 0.5\n");
    const auto m = loadMaterial(a.path());
    REQUIRE(m.has_value());
    CHECK_THAT(m->diffuse.x, WithinAbs(1.0, 1e-6));
    CHECK_THAT(m->diffuse.y, WithinAbs(0.0, 1e-6));
    CHECK_THAT(m->diffuse.z, WithinAbs(0.5, 1e-6));
}

TEST_CASE("a known key with an unparseable value is an error", "[core][material]") {
    // Silently keeping the default would give the asset a different appearance
    // with no diagnostic.
    const TempMat a("shininess not_a_number\n");
    const auto m = loadMaterial(a.path());
    REQUIRE_FALSE(m.has_value());
    CHECK(m.error().kind == MaterialErrorKind::MalformedLine);
    CHECK(m.error().line == 1);
}

TEST_CASE("an unknown key is ignored, not an error", "[core][material]") {
    // Community assets carry keys this build has never seen.
    const TempMat a("name Fine\nsomeFutureKey 1 2 3\nopacity 0.5\n");
    const auto m = loadMaterial(a.path());
    REQUIRE(m.has_value());
    CHECK(m->name == "Fine");
    CHECK_THAT(m->opacity, WithinAbs(0.5, 1e-6));
}

TEST_CASE("the default uv map sentinel means no override", "[core][material]") {
    // material.py:451-458
    const TempMat a("uvMap data/uvs/default.mhuv\n");
    const auto m = loadMaterial(a.path());
    REQUIRE(m.has_value());
    CHECK_FALSE(m->uvMap.has_value());

    const TempMat b("uvMap custom.mhuv\n");
    const auto n = loadMaterial(b.path());
    REQUIRE(n.has_value());
    CHECK(n->uvMap.has_value());
}

TEST_CASE("the shader stem has its stage suffix stripped", "[core][material]") {
    // material.py:1431-1445 -- the stem is probed with each stage suffix.
    const TempMat a("shader data/shaders/glsl/phong_vertex_shader.txt\n");
    const auto m = loadMaterial(a.path());
    REQUIRE(m.has_value());
    CHECK(m->shader.string().ends_with("phong"));
}

TEST_CASE("normal and bump are mutually exclusive in the define set", "[core][material]") {
    // material.py:984-995 -- bump is only enabled when normal is not.
    const TempMat a(
        "normalmapTexture n.png\nbumpmapTexture b.png\n"
        "shaderConfig normal true\nshaderConfig bump true\n");
    const auto m = loadMaterial(a.path());
    REQUIRE(m.has_value());

    const auto d = m->effectiveDefines();
    CHECK(hasDefine(d, "NORMALMAP"));
    CHECK_FALSE(hasDefine(d, "BUMPMAP"));

    const TempMat b("bumpmapTexture b.png\nshaderConfig normal false\nshaderConfig bump true\n");
    const auto n = loadMaterial(b.path());
    REQUIRE(n.has_value());
    CHECK(hasDefine(n->effectiveDefines(), "BUMPMAP"));
}

TEST_CASE("a channel contributes no define without its texture", "[core][material]") {
    const TempMat a("shaderConfig spec true\n");  // enabled, but no specularmap
    const auto m = loadMaterial(a.path());
    REQUIRE(m.has_value());
    CHECK_FALSE(hasDefine(m->effectiveDefines(), "SPECULARMAP"));
}

TEST_CASE("the define list is sorted", "[core][material]") {
    // The sorted list is the shader-variant cache key (material.py:1015), so
    // ordering is part of the contract.
    const TempMat a("diffuseTexture d.png\nspecularmapTexture s.png\naomapTexture a.png\n");
    const auto m = loadMaterial(a.path());
    REQUIRE(m.has_value());

    const auto d = m->effectiveDefines();
    CHECK(std::ranges::is_sorted(d));
    CHECK(d.size() >= 3);
}

TEST_CASE("a missing material is reported", "[core][material]") {
    const auto m = loadMaterial("/definitely/not/here.mhmat");
    REQUIRE_FALSE(m.has_value());
    CHECK(m.error().kind == MaterialErrorKind::NotFound);
    CHECK_FALSE(m.error().message().empty());
}
