// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include "makehuman/core/Types.h"
#include "makehuman/foundation/Geometry.h"
#include "makehuman/foundation/MaterialProperty.h"

#include <array>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <vector>

namespace mh::core {

/// The seven texture channels the format defines
/// (legacy/python/shared/material.py:176). There is no PBR here: the model is
/// Blinn-Phong with per-channel intensities. A metallic-roughness conversion
/// belongs in the renderer (memory/architecture.md §II.4), not in the parser.
enum class TextureChannel : uint8_t {
    Diffuse,
    BumpMap,
    NormalMap,
    DisplacementMap,
    SpecularMap,
    TransparencyMap,
    AoMap,
    Count
};

inline constexpr size_t kTextureChannelCount = static_cast<size_t>(TextureChannel::Count);

struct TextureSlot {
    std::filesystem::path path;
    float intensity{1.0F};

    [[nodiscard]] bool present() const noexcept { return !path.empty(); }
};

/// Which built-in shader features a material enables (material.py:466-484).
struct ShaderConfig {
    bool diffuse{true};
    bool bump{true};
    bool normal{true};
    bool displacement{true};
    bool spec{true};
    bool vertexColors{true};
    bool transparency{true};
    bool ambientOcclusion{true};
};

/// A shader parameter value: a scalar, a vector, or a path.
using ShaderParam = std::vector<std::string>;

/// A parsed `.mhmat`.
///
/// Line-oriented, whitespace-split; `#` and `//` are comments when they are the
/// first token (material.py:364-365). Booleans accept `yes|enabled|true`
/// case-insensitively (:357-358).
struct Material {
    std::string name{"UnnamedMaterial"};
    std::string description;
    std::set<std::string> tags;
    std::filesystem::path filename;

    Vec3 ambient{1, 1, 1};
    Vec3 diffuse{1, 1, 1};
    Vec3 specular{1, 1, 1};
    Vec3 emissive{0, 0, 0};
    Vec3 viewPortColor{0, 0, 0};
    float viewPortAlpha{1.0F};
    bool hasViewPortColor{false};

    float shininess{0.2F};
    float opacity{1.0F};
    float translucency{0.0F};

    bool shadeless{false};
    bool wireframe{false};
    bool transparent{false};
    bool alphaToCoverage{true};
    bool backfaceCull{true};
    bool depthless{false};
    bool castShadows{true};
    bool receiveShadows{true};
    bool autoBlendSkin{false};

    bool sssEnabled{false};
    float sssRScale{0.0F};
    float sssGScale{0.0F};
    float sssBScale{0.0F};

    std::array<TextureSlot, kTextureChannelCount> textures{};

    /// Shader stem: the path with `_vertex_shader.txt` etc. stripped
    /// (material.py:1431-1445).
    std::filesystem::path shader;
    std::optional<std::filesystem::path> uvMap;

    ShaderConfig shaderConfig;
    std::map<std::string, ShaderParam> shaderParams;
    std::vector<std::string> shaderDefines;

    [[nodiscard]] const TextureSlot& texture(TextureChannel c) const noexcept {
        return textures[static_cast<size_t>(c)];
    }

    /// The define set the reference derives from the enabled channels
    /// (material.py:956-1016). **Sorted**, because the sorted list is the
    /// shader-variant cache key (:1015) and asset shaders depend on it.
    [[nodiscard]] std::vector<std::string> effectiveDefines() const;

    /// The subset a format writer needs, as plain data (see Mesh::view()).
    [[nodiscard]] foundation::MaterialDesc desc() const;
};

enum class MaterialErrorKind { NotFound, Unreadable, MalformedLine, Unwritable };

struct MaterialError {
    MaterialErrorKind kind{};
    std::string file;
    uint32_t line{};
    std::string detail;

    [[nodiscard]] std::string message() const;
};

[[nodiscard]] std::expected<Material, MaterialError> loadMaterial(
    const std::filesystem::path& path);

/// Writes a `.mhmat`, losslessly.
///
/// The reference's writer (`material.py:511-620`) is **not** lossless and is
/// not the model here:
///
///  - It never writes `tag`, so every tag is dropped on save. Verified by
///    round-tripping `brown.mhmat` through the reference: `['makehuman™']`
///    comes back `[]`.
///  - It never writes `autoBlendSkin` or the viewport colour.
///  - It cannot save a skin at all outside a running app: `autoBlendSkin`
///    routes `diffuseColor` through the skin blender, so `default.mhmat`
///    raises `AttributeError: 'NoneType' object has no attribute
///    'selectedHuman'` from `toFile`. In-app it writes the *blended* colour
///    over the authored one.
///
/// Losing user data on save is a defect, not a behaviour to port
/// (`project_context.md` §8), so everything the reader understands is written.
/// The output stays readable by the reference's own parser: booleans are
/// written `True`/`False`, which its `_readbool` accepts (`material.py:357`).
///
/// Texture and shader paths are written relative to @p path's directory when
/// they live under it, and absolute otherwise -- the reference's `_texPath`
/// intent (`:497-509`) without its dependency on the app's data-path registry.
[[nodiscard]] std::expected<void, MaterialError> saveMaterial(const std::filesystem::path& path,
                                                              const Material& material);

/// Applies one `property=value` edit to @p material, in place.
///
/// The editor and the parser are ONE dispatch: `loadMaterial` routes every
/// line through the same function, so anything the format can express is
/// editable and the two can never drift into disagreeing about a key's name,
/// its clamp range, or its side effects (`viewPortAlpha` also setting
/// `hasViewPortColor`, for one).
///
/// @p spec is `key=value`; the value is split on whitespace and commas, so
/// `diffuseColor=0.8,0.1,0.1` and `diffuseColor=0.8 0.1 0.1` are the same
/// edit, as is the multi-token `shaderConfig=spec False`.
///
/// @p dir is what a texture path is resolved against, exactly as the
/// material's own directory is when loading.
///
/// An UNKNOWN key is an error here, though `loadMaterial` ignores one: a
/// community asset carrying a key this build has never seen must still load,
/// but a person typing `--set-material diffusColor=1,0,0` has made a mistake
/// and silently doing nothing is the worst possible answer.
[[nodiscard]] std::expected<void, std::string> setMaterialProperty(
    Material& material, std::string_view spec, const std::filesystem::path& dir);

/// Every property of @p material the editor offers, in the reference's order.
///
/// The **exact inverse** of `setMaterialProperty`: feeding any returned row
/// back as `id + "=" + value` leaves the material unchanged. That is what makes
/// a panel built from these rows non-decorative, and there is a test that walks
/// every row asserting it.
///
/// The rows are the reference's own material box
/// (`legacy/python/plugins/7_material_editor.py:138-358`), in its order: four
/// colours, the scalars, the flags, each texture with its intensity, the UV map
/// and the name. `shaderConfig` is deliberately NOT here -- its line is
/// `shaderConfig <name> <bool>`, so eight rows would share one id and the
/// `id=value` contract above would not hold. The reference keeps those
/// checkboxes in a separate box too (`:68-70`), and `setMaterialProperty`
/// already accepts `shaderConfig=spec False` from the command line.
///
/// Returns a `foundation` type on purpose: AGPL may depend on Apache-2.0, never
/// the reverse, and this is what lets `mh_ui` show a material it cannot include.
[[nodiscard]] std::vector<foundation::MaterialProperty> editableProperties(
    const Material& material);

}  // namespace mh::core
