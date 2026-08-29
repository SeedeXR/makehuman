// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include "makehuman/core/Types.h"

#include <array>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <vector>

namespace mh::core {

/// The seven texture channels the format defines
/// (legacy-python/shared/material.py:176). There is no PBR here: the model is
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
};

enum class MaterialErrorKind { NotFound, Unreadable, MalformedLine };

struct MaterialError {
    MaterialErrorKind kind{};
    std::string file;
    uint32_t line{};
    std::string detail;

    [[nodiscard]] std::string message() const;
};

[[nodiscard]] std::expected<Material, MaterialError> loadMaterial(
    const std::filesystem::path& path);

}  // namespace mh::core
