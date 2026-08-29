// SPDX-License-Identifier: AGPL-3.0-or-later
#include "makehuman/core/Material.h"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <fstream>
#include <optional>
#include <sstream>

namespace mh::core {
namespace {

std::vector<std::string> splitWs(const std::string& line) {
    std::vector<std::string> out;
    std::istringstream ss(line);
    std::string tok;
    while (ss >> tok)
        out.push_back(tok);
    return out;
}

std::string toLower(std::string s) {
    std::ranges::transform(s, s.begin(),
                           [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

/// material.py:357-358 -- yes / enabled / true, case-insensitively.
bool readBool(const std::string& s) {
    const std::string v = toLower(s);
    return v == "yes" || v == "enabled" || v == "true";
}

bool parseFloat(const std::string& s, float& out) {
    const auto r = std::from_chars(s.data(), s.data() + s.size(), out);
    return r.ec == std::errc{} && r.ptr == s.data() + s.size();
}

float clamp01(float v) {
    return std::clamp(v, 0.0F, 1.0F);
}

bool readColor(const std::vector<std::string>& tok, Vec3& out) {
    if (tok.size() < 4) return false;
    float r{}, g{}, b{};
    if (!parseFloat(tok[1], r) || !parseFloat(tok[2], g) || !parseFloat(tok[3], b)) return false;
    // Colours are clamped to [0,1] (material.py:62-69).
    out = Vec3{clamp01(r), clamp01(g), clamp01(b)};
    return true;
}

/// Strips the shader-stage suffix so what remains is the stem the loader probes
/// with (material.py:1431-1445).
std::filesystem::path shaderStem(std::string s) {
    for (const char* suffix :
         {"_vertex_shader.txt", "_fragment_shader.txt", "_geometry_shader.txt"}) {
        const std::string suf(suffix);
        if (s.size() > suf.size() && s.ends_with(suf)) {
            s.resize(s.size() - suf.size());
            break;
        }
    }
    return s;
}

struct ChannelKey {
    const char* texture;
    const char* intensity;
    TextureChannel channel;
};

constexpr std::array<ChannelKey, kTextureChannelCount> kChannels{{
    {"diffusetexture", nullptr, TextureChannel::Diffuse},
    {"bumpmaptexture", "bumpmapintensity", TextureChannel::BumpMap},
    {"normalmaptexture", "normalmapintensity", TextureChannel::NormalMap},
    {"displacementmaptexture", "displacementmapintensity", TextureChannel::DisplacementMap},
    {"specularmaptexture", "specularmapintensity", TextureChannel::SpecularMap},
    {"transparencymaptexture", "transparencymapintensity", TextureChannel::TransparencyMap},
    {"aomaptexture", "aomapintensity", TextureChannel::AoMap},
}};

}  // namespace

std::vector<std::string> Material::effectiveDefines() const {
    // material.py:956-1016. A channel contributes only when its texture exists
    // AND the config enables it. bump is suppressed when normal is active
    // (:984-995) -- they are mutually exclusive.
    std::vector<std::string> out = shaderDefines;

    if (shaderConfig.vertexColors) out.emplace_back("VERTEX_COLOR");
    if (shaderConfig.diffuse && texture(TextureChannel::Diffuse).present()) {
        out.emplace_back("DIFFUSE");
    }
    const bool normalOn = shaderConfig.normal && texture(TextureChannel::NormalMap).present();
    if (normalOn) out.emplace_back("NORMALMAP");
    if (!normalOn && shaderConfig.bump && texture(TextureChannel::BumpMap).present()) {
        out.emplace_back("BUMPMAP");
    }
    if (shaderConfig.displacement && texture(TextureChannel::DisplacementMap).present()) {
        out.emplace_back("DISPLACEMENT");
    }
    if (shaderConfig.spec && texture(TextureChannel::SpecularMap).present()) {
        out.emplace_back("SPECULARMAP");
    }
    if (shaderConfig.transparency && texture(TextureChannel::TransparencyMap).present()) {
        out.emplace_back("TRANSPARENCYMAP");
    }
    if (shaderConfig.ambientOcclusion && texture(TextureChannel::AoMap).present()) {
        out.emplace_back("AOMAP");
    }

    // Sorted: the sorted define list is the shader-variant cache key (:1015),
    // so ordering is part of the contract, not a formatting choice.
    std::ranges::sort(out);
    out.erase(std::ranges::unique(out).begin(), out.end());
    return out;
}

std::string MaterialError::message() const {
    const char* k = "unknown error";
    switch (kind) {
        case MaterialErrorKind::NotFound: k = "file not found"; break;
        case MaterialErrorKind::Unreadable: k = "file unreadable"; break;
        case MaterialErrorKind::MalformedLine: k = "malformed line"; break;
    }
    std::string m = file;
    if (line > 0) m += ':' + std::to_string(line);
    m += ": ";
    m += k;
    if (!detail.empty()) m += " (" + detail + ")";
    return m;
}

std::expected<Material, MaterialError> loadMaterial(const std::filesystem::path& path) {
    std::error_code ec;
    if (!std::filesystem::exists(path, ec)) {
        return std::unexpected(MaterialError{MaterialErrorKind::NotFound, path.string(), 0, {}});
    }
    std::ifstream in(path);
    if (!in) {
        return std::unexpected(MaterialError{MaterialErrorKind::Unreadable, path.string(), 0, {}});
    }

    Material m;
    const auto dir = path.parent_path();
    m.filename     = path;
    m.name         = path.stem().string();

    std::string line;
    uint32_t lineNo = 0;
    std::optional<MaterialError> failure;

    // A KNOWN key with an unparseable value is an error: silently keeping the
    // default would give the asset a different appearance with no diagnostic.
    // An UNKNOWN key is ignored -- community assets carry keys this build has
    // never seen, and the reference tolerates them too.
    const auto num = [&](const std::string& text, float& dst, float lo, float hi) {
        float v{};
        if (!parseFloat(text, v)) {
            if (!failure) {
                failure = MaterialError{MaterialErrorKind::MalformedLine, path.string(), lineNo,
                                        "expected a number, got '" + text + "'"};
            }
            return;
        }
        dst = std::clamp(v, lo, hi);
    };

    while (std::getline(in, line)) {
        ++lineNo;
        if (!line.empty() && line.back() == '\r') line.pop_back();
        const auto tok = splitWs(line);
        if (tok.empty()) continue;

        // '#' and '//' are comments only as the FIRST token (material.py:364-365).
        if (tok[0] == "#" || tok[0] == "//" || tok[0].starts_with("//")) continue;

        const std::string key = toLower(tok[0]);
        const auto need       = [&](size_t n) { return tok.size() > n; };

        if (key == "name" && need(1)) {
            m.name = tok[1];
        } else if (key == "tag" && need(1)) {
            std::string t;
            for (size_t i = 1; i < tok.size(); ++i) {
                if (i > 1) t.push_back(' ');
                t += tok[i];
            }
            m.tags.insert(toLower(t));
        } else if (key == "description" && need(1)) {
            for (size_t i = 1; i < tok.size(); ++i) {
                if (i > 1) m.description.push_back(' ');
                m.description += tok[i];
            }
        } else if (key == "ambientcolor") {
            (void)readColor(tok, m.ambient);
        } else if (key == "diffusecolor") {
            (void)readColor(tok, m.diffuse);
        } else if (key == "specularcolor") {
            (void)readColor(tok, m.specular);
        } else if (key == "emissivecolor") {
            (void)readColor(tok, m.emissive);
        } else if (key == "viewportcolor") {
            m.hasViewPortColor = readColor(tok, m.viewPortColor);
        } else if (key == "viewportalpha" && need(1)) {
            num(tok[1], m.viewPortAlpha, 0.0F, 1.0F);
        } else if (key == "shininess" && need(1)) {
            num(tok[1], m.shininess, 0.0F, 1.0F);
        } else if (key == "opacity" && need(1)) {
            num(tok[1], m.opacity, 0.0F, 1.0F);
        } else if (key == "translucency" && need(1)) {
            num(tok[1], m.translucency, -1e30F, 1e30F);
        } else if (key == "shadeless" && need(1)) {
            m.shadeless = readBool(tok[1]);
        } else if (key == "wireframe" && need(1)) {
            m.wireframe = readBool(tok[1]);
        } else if (key == "transparent" && need(1)) {
            m.transparent = readBool(tok[1]);
        } else if (key == "alphatocoverage" && need(1)) {
            m.alphaToCoverage = readBool(tok[1]);
        } else if (key == "backfacecull" && need(1)) {
            m.backfaceCull = readBool(tok[1]);
        } else if (key == "depthless" && need(1)) {
            m.depthless = readBool(tok[1]);
        } else if (key == "castshadows" && need(1)) {
            m.castShadows = readBool(tok[1]);
        } else if (key == "receiveshadows" && need(1)) {
            m.receiveShadows = readBool(tok[1]);
        } else if (key == "autoblendskin" && need(1)) {
            m.autoBlendSkin = readBool(tok[1]);
        } else if (key == "sssenabled" && need(1)) {
            m.sssEnabled = readBool(tok[1]);
        } else if (key == "sssrscale" && need(1)) {
            num(tok[1], m.sssRScale, 0.0F, 1e30F);
        } else if (key == "sssgscale" && need(1)) {
            num(tok[1], m.sssGScale, 0.0F, 1e30F);
        } else if (key == "sssbscale" && need(1)) {
            num(tok[1], m.sssBScale, 0.0F, 1e30F);
        } else if (key == "shader" && need(1)) {
            m.shader = shaderStem(tok[1]);
        } else if (key == "uvmap" && need(1)) {
            // "uvs/default.mhuv" is a sentinel meaning "no override"
            // (material.py:451-458).
            const std::string v = tok[1];
            if (v.find("default.mhuv") == std::string::npos) m.uvMap = dir / v;
        } else if (key == "shaderparam" && need(2)) {
            ShaderParam p;
            for (size_t i = 2; i < tok.size(); ++i)
                p.push_back(tok[i]);
            m.shaderParams[tok[1]] = std::move(p);
        } else if (key == "shaderdefine" && need(1)) {
            m.shaderDefines.push_back(tok[1]);
        } else if (key == "shaderconfig" && need(2)) {
            const std::string opt = toLower(tok[1]);
            const bool on         = readBool(tok[2]);
            if (opt == "diffuse") {
                m.shaderConfig.diffuse = on;
            } else if (opt == "bump") {
                m.shaderConfig.bump = on;
            } else if (opt == "normal") {
                m.shaderConfig.normal = on;
            } else if (opt == "displacement") {
                m.shaderConfig.displacement = on;
            } else if (opt == "spec") {
                m.shaderConfig.spec = on;
            } else if (opt == "vertexcolors") {
                m.shaderConfig.vertexColors = on;
            } else if (opt == "transparency") {
                m.shaderConfig.transparency = on;
            } else if (opt == "ambientocclusion") {
                m.shaderConfig.ambientOcclusion = on;
            }
        } else {
            // Texture channels and their intensities.
            for (const ChannelKey& ck : kChannels) {
                const auto slot = static_cast<size_t>(ck.channel);
                if (key == ck.texture && need(1)) {
                    m.textures[slot].path = dir / tok[1];
                    break;
                }
                if (ck.intensity != nullptr && key == ck.intensity && need(1)) {
                    num(tok[1], m.textures[slot].intensity, 0.0F, 1.0F);
                    break;
                }
            }
            // diffuseIntensity / specularIntensity are deprecated and warned on
            // by the reference (material.py:377-382); everything else is simply
            // not part of the format. Both are ignored rather than fatal, since
            // community assets carry keys this build has never seen.
        }
    }

    if (failure) return std::unexpected(*failure);
    return m;
}

}  // namespace mh::core
