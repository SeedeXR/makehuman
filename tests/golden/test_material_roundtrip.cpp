// SPDX-License-Identifier: AGPL-3.0-or-later
//
// .mhmat writer: round-trip on every shipped material.
//
// The reference's own writer is NOT the model here -- it is lossy, and verified
// so: round-tripping brown.mhmat through material.py turns tag ['makehuman™']
// into [], and default.mhmat cannot be written at all outside a running app
// (autoBlendSkin routes diffuseColor through the skin blender and toFile raises
// AttributeError on G.app). See project_context.md 8.1.
//
// So the property tested is losslessness, not byte-parity with material.py.
// That our output is still readable BY material.py is checked separately, by
// tools/verify_material_roundtrip.py.

#include "makehuman/core/Material.h"

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

using namespace mh::core;

namespace {

const std::vector<std::string> kShipped{
    "materials/xray.mhmat",
    "skins/default.mhmat",
    "eyes/materials/brown.mhmat",
};

/// Everything the format can carry. Compared field by field rather than by
/// re-serialising, so a writer that drops a field cannot hide behind a reader
/// that also drops it.
void requireSame(const Material& a, const Material& b) {
    CHECK(a.name == b.name);
    CHECK(a.description == b.description);
    CHECK(a.tags == b.tags);

    CHECK(a.ambient.x == b.ambient.x);
    CHECK(a.ambient.y == b.ambient.y);
    CHECK(a.ambient.z == b.ambient.z);
    CHECK(a.diffuse.x == b.diffuse.x);
    CHECK(a.diffuse.y == b.diffuse.y);
    CHECK(a.diffuse.z == b.diffuse.z);
    CHECK(a.specular.x == b.specular.x);
    CHECK(a.specular.y == b.specular.y);
    CHECK(a.specular.z == b.specular.z);
    CHECK(a.emissive.x == b.emissive.x);
    CHECK(a.emissive.y == b.emissive.y);
    CHECK(a.emissive.z == b.emissive.z);

    CHECK(a.shininess == b.shininess);
    CHECK(a.opacity == b.opacity);
    CHECK(a.translucency == b.translucency);

    CHECK(a.hasViewPortColor == b.hasViewPortColor);
    if (a.hasViewPortColor) {
        CHECK(a.viewPortColor.x == b.viewPortColor.x);
        CHECK(a.viewPortAlpha == b.viewPortAlpha);
    }

    CHECK(a.shadeless == b.shadeless);
    CHECK(a.wireframe == b.wireframe);
    CHECK(a.transparent == b.transparent);
    CHECK(a.alphaToCoverage == b.alphaToCoverage);
    CHECK(a.backfaceCull == b.backfaceCull);
    CHECK(a.depthless == b.depthless);
    CHECK(a.castShadows == b.castShadows);
    CHECK(a.receiveShadows == b.receiveShadows);
    CHECK(a.autoBlendSkin == b.autoBlendSkin);

    CHECK(a.sssEnabled == b.sssEnabled);
    CHECK(a.sssRScale == b.sssRScale);
    CHECK(a.sssGScale == b.sssGScale);
    CHECK(a.sssBScale == b.sssBScale);

    for (size_t i = 0; i < kTextureChannelCount; ++i) {
        CAPTURE(i);
        const auto c = static_cast<TextureChannel>(i);
        CHECK(a.texture(c).path == b.texture(c).path);
        CHECK(a.texture(c).intensity == b.texture(c).intensity);
    }

    CHECK(a.shader == b.shader);
    CHECK(a.uvMap.has_value() == b.uvMap.has_value());
    if (a.uvMap && b.uvMap) CHECK(*a.uvMap == *b.uvMap);

    CHECK(a.shaderParams == b.shaderParams);
    CHECK(a.shaderDefines == b.shaderDefines);
    CHECK(a.effectiveDefines() == b.effectiveDefines());

    CHECK(a.shaderConfig.diffuse == b.shaderConfig.diffuse);
    CHECK(a.shaderConfig.bump == b.shaderConfig.bump);
    CHECK(a.shaderConfig.normal == b.shaderConfig.normal);
    CHECK(a.shaderConfig.displacement == b.shaderConfig.displacement);
    CHECK(a.shaderConfig.spec == b.shaderConfig.spec);
    CHECK(a.shaderConfig.vertexColors == b.shaderConfig.vertexColors);
    CHECK(a.shaderConfig.transparency == b.shaderConfig.transparency);
    CHECK(a.shaderConfig.ambientOcclusion == b.shaderConfig.ambientOcclusion);
}

}  // namespace

TEST_CASE("every shipped .mhmat round-trips losslessly", "[material][golden][roundtrip]") {
    for (const auto& rel : kShipped) {
        CAPTURE(rel);
        const auto src = std::filesystem::path(MH_DATA_DIR) / rel;
        REQUIRE(std::filesystem::exists(src));

        const auto original = loadMaterial(src);
        REQUIRE(original.has_value());

        // Written into the SAME directory, so relative texture and shader paths
        // resolve identically on the way back in. Writing elsewhere would
        // change what "relative" means and mask a path bug.
        const auto out = src.parent_path() / (src.stem().string() + ".roundtrip.mhmat");
        REQUIRE(saveMaterial(out, *original).has_value());

        const auto reread = loadMaterial(out);
        REQUIRE(reread.has_value());
        requireSame(*original, *reread);

        // A second pass must be a fixed point: if it is not, the writer and
        // reader disagree about some field and the file drifts on every save.
        const auto out2 = src.parent_path() / (src.stem().string() + ".roundtrip2.mhmat");
        REQUIRE(saveMaterial(out2, *reread).has_value());
        const auto reread2 = loadMaterial(out2);
        REQUIRE(reread2.has_value());
        requireSame(*reread, *reread2);

        std::error_code ec;
        std::filesystem::remove(out, ec);
        std::filesystem::remove(out2, ec);
    }
}

// The two fields the reference's own writer silently drops. default.mhmat
// carries both, and is exactly the file material.py cannot write at all.
TEST_CASE("the writer keeps what the reference's writer drops", "[material][roundtrip]") {
    const auto src      = std::filesystem::path(MH_DATA_DIR) / "skins" / "default.mhmat";
    const auto original = loadMaterial(src);
    REQUIRE(original.has_value());
    REQUIRE(original->autoBlendSkin);       // material.py never writes this
    REQUIRE_FALSE(original->tags.empty());  // nor this

    const auto out = src.parent_path() / "default.keepcheck.mhmat";
    REQUIRE(saveMaterial(out, *original).has_value());
    const auto reread = loadMaterial(out);
    REQUIRE(reread.has_value());

    CHECK(reread->autoBlendSkin);
    CHECK(reread->tags == original->tags);

    std::error_code ec;
    std::filesystem::remove(out, ec);
}

// Our reader lowercases the key, so it round-trips ANY casing and can never
// catch this. The reference's reader compares `words[0]` case-sensitively
// (material.py:369-448), so a file saying `diffusetexture` loads here and is
// silently ignored by MakeHuman 1.x -- the texture simply disappears.
//
// This was a real bug: the writer reused the lowercase lookup keys, and
// tools/verify_material_roundtrip.py caught it by round-tripping brown.mhmat
// through the reference, which reported diffuseTexture -> None. Guard the
// canonical spellings here so CI catches a regression without needing Python.
TEST_CASE("texture keys are written in the reference's exact casing",
          "[material][roundtrip][parity]") {
    const auto src      = std::filesystem::path(MH_DATA_DIR) / "eyes" / "materials" / "brown.mhmat";
    const auto original = loadMaterial(src);
    REQUIRE(original.has_value());
    REQUIRE(original->texture(TextureChannel::Diffuse).present());

    const auto out = src.parent_path() / "brown.casecheck.mhmat";
    REQUIRE(saveMaterial(out, *original).has_value());

    std::ifstream in(out);
    const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    in.close();

    CHECK(text.find("diffuseTexture ") != std::string::npos);
    CHECK(text.find("diffusetexture ") == std::string::npos);
    // The other camelCase keys the reference is equally strict about.
    CHECK(text.find("ambientColor ") != std::string::npos);
    CHECK(text.find("specularColor ") != std::string::npos);
    CHECK(text.find("shaderConfig ") != std::string::npos);
    CHECK(text.find("shaderParam ") != std::string::npos);

    std::error_code ec;
    std::filesystem::remove(out, ec);
}

// A texture outside the material's own directory cannot be written relative
// without a "../.." that depends on where the file ends up, so it goes out
// absolute and must still resolve on the way back in.
TEST_CASE("a texture outside the material directory stays resolvable", "[material][roundtrip]") {
    const auto dir = std::filesystem::temp_directory_path() / "mh_mat_outside";
    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
    std::filesystem::create_directories(dir / "mats", ec);
    std::filesystem::create_directories(dir / "tex", ec);
    {
        std::ofstream(dir / "tex" / "t.png") << "x";
    }

    Material m;
    m.name                                                        = "Outside";
    m.textures[static_cast<size_t>(TextureChannel::Diffuse)].path = dir / "tex" / "t.png";

    const auto out = dir / "mats" / "m.mhmat";
    REQUIRE(saveMaterial(out, m).has_value());

    const auto reread = loadMaterial(out);
    REQUIRE(reread.has_value());
    CHECK(std::filesystem::exists(reread->texture(TextureChannel::Diffuse).path));

    std::filesystem::remove_all(dir, ec);
}

TEST_CASE("an unwritable material path is reported", "[material][roundtrip]") {
    const Material m;
    const auto dir = std::filesystem::temp_directory_path() / "mh_mat_unwritable";
    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
    std::filesystem::create_directories(dir / "blocked.mhmat", ec);  // a directory, not a file

    const auto r = saveMaterial(dir / "blocked.mhmat", m);
    REQUIRE_FALSE(r.has_value());
    CHECK(r.error().kind == MaterialErrorKind::Unwritable);

    std::filesystem::remove_all(dir, ec);
}
