// SPDX-License-Identifier: AGPL-3.0-or-later
#include "makehuman/core/Material.h"
#include "makehuman/foundation/Chars.h"
#include "makehuman/foundation/FileRead.h"

#include <algorithm>
#include <cctype>
#include <cmath>
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

/// The shim accepts a leading '+' (which std::from_chars rejects but the
/// oracle's float() accepts, and shipped assets use) and rejects nan/inf, which
/// would otherwise reach the exporter and produce unparseable JSON.
bool parseFloat(const std::string& s, float& out) {
    return foundation::parseFloat(s, out);
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

/// Reading is case-insensitive (we lowercase the key first), so the `texture`
/// and `intensity` names here are lowercase. **Writing is not symmetric**: the
/// reference's parser compares `words[0]` case-SENSITIVELY
/// (`material.py:369-448`), so a file saying `diffusetexture` loads in this
/// port and is silently ignored by MakeHuman 1.x -- the texture just vanishes.
/// `writeTexture`/`writeIntensity` are therefore the canonical spellings, and
/// the writer must use those, never the lookup keys.
struct ChannelKey {
    const char* texture;
    const char* intensity;
    const char* writeTexture;
    const char* writeIntensity;
    TextureChannel channel;
};

constexpr std::array<ChannelKey, kTextureChannelCount> kChannels{{
    {"diffusetexture", nullptr, "diffuseTexture", nullptr, TextureChannel::Diffuse},
    {"bumpmaptexture", "bumpmapintensity", "bumpmapTexture", "bumpmapIntensity",
     TextureChannel::BumpMap},
    {"normalmaptexture", "normalmapintensity", "normalmapTexture", "normalmapIntensity",
     TextureChannel::NormalMap},
    {"displacementmaptexture", "displacementmapintensity", "displacementmapTexture",
     "displacementmapIntensity", TextureChannel::DisplacementMap},
    {"specularmaptexture", "specularmapintensity", "specularmapTexture", "specularmapIntensity",
     TextureChannel::SpecularMap},
    {"transparencymaptexture", "transparencymapintensity", "transparencymapTexture",
     "transparencymapIntensity", TextureChannel::TransparencyMap},
    {"aomaptexture", "aomapintensity", "aomapTexture", "aomapIntensity", TextureChannel::AoMap},
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
        case MaterialErrorKind::Unwritable: k = "cannot write"; break;
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
    // openForRead, not exists()+ifstream: a DIRECTORY satisfies both and
    // then parses as an empty file, so this reader used to accept one.
    // See foundation/FileRead.h for what each reader did before.
    auto opened = foundation::openForRead(path);
    if (!opened) {
        // NotAFile maps to Unreadable rather than NotFound: something IS
        // there, and saying "not found" about a path that exists sends
        // whoever is debugging it looking in the wrong place.
        const auto kind = opened.error() == foundation::FileReadErrorKind::NotFound
                              ? MaterialErrorKind::NotFound
                              : MaterialErrorKind::Unreadable;
        return std::unexpected(MaterialError{kind, path.string(), 0, {}});
    }
    std::ifstream& in = *opened;

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

    // Colours went through `(void)readColor(...)`, so `diffuseColor 0.5 0.5`
    // and `diffuseColor 0.5 zzz 0.5` both loaded as pure white with no
    // diagnostic. The oracle raises ValueError on both
    // (`[float(w) for w in words[1:4]]`), and the policy stated above says a
    // known key with an unparseable value is an error. Make it one.
    const auto color = [&](const std::vector<std::string>& t, Vec3& dst) {
        if (readColor(t, dst)) return;
        if (!failure) {
            failure = MaterialError{MaterialErrorKind::MalformedLine, path.string(), lineNo,
                                    "expected three numbers for '" + t[0] + "'"};
        }
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
            color(tok, m.ambient);
        } else if (key == "diffusecolor") {
            color(tok, m.diffuse);
        } else if (key == "specularcolor") {
            color(tok, m.specular);
        } else if (key == "emissivecolor") {
            color(tok, m.emissive);
        } else if (key == "viewportcolor") {
            color(tok, m.viewPortColor);
            m.hasViewPortColor = true;  // material.py:389, set unconditionally
        } else if (key == "viewportalpha" && need(1)) {
            num(tok[1], m.viewPortAlpha, 0.0F, 1.0F);
            m.hasViewPortColor = true;  // material.py:392 -- alpha sets it too
        } else if (key == "shininess" && need(1)) {
            num(tok[1], m.shininess, 0.0F, 1.0F);
        } else if (key == "opacity" && need(1)) {
            num(tok[1], m.opacity, 0.0F, 1.0F);
        } else if (key == "translucency" && need(1)) {
            num(tok[1], m.translucency, 0.0F, 1.0F);
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

namespace {

/// Shortest representation that round-trips exactly, locale-independently.
///
/// The reference formats with `%s`/`%r`, i.e. Python's shortest round-trip
/// repr; formatShortest is the same idea. It lives in foundation because
/// floating-point std::to_chars is missing from some libc++ we build on.
std::string num(float v) {
    return foundation::formatShortest(v);
}

/// The reference writes Python's `True`/`False`; its own reader lowercases
/// before comparing (material.py:357), so this stays readable by both.
const char* boolStr(bool b) {
    return b ? "True" : "False";
}

/// Relative to the material's own directory when the file lives under it,
/// absolute otherwise. Mirrors _texPath's intent (material.py:497-509) without
/// its dependency on the app's registered data paths.
std::string portablePath(const std::filesystem::path& file, const std::filesystem::path& dir) {
    if (file.empty()) return {};

    // A bare filename has no parent; relative(x, "") is not meaningful.
    const std::filesystem::path base = dir.empty() ? std::filesystem::path(".") : dir;

    std::error_code ec;
    const auto rel = std::filesystem::relative(file, base, ec);
    if (ec || rel.empty()) return file.generic_string();

    // Escaping the material's directory means the path is not portable, so
    // write it absolute rather than emit a "../.." nobody can resolve.
    // Compare the first COMPONENT, not the string: a directory legitimately
    // named "..cache" starts with ".." without escaping anything.
    const auto first = rel.begin();
    if (first != rel.end() && *first == "..") return file.generic_string();

    return rel.generic_string();
}

}  // namespace

std::expected<void, MaterialError> saveMaterial(const std::filesystem::path& path,
                                                const Material& material) {
    std::ofstream out(path);
    if (!out) {
        return std::unexpected(MaterialError{MaterialErrorKind::Unwritable, path.string(), 0,
                                             "cannot open for writing"});
    }
    const std::filesystem::path dir = path.parent_path();

    out << "# Material definition for " << material.name << "\n\n";
    out << "name " << material.name << "\n";
    if (!material.description.empty()) out << "description " << material.description << "\n";

    // The reference drops these entirely; a save that loses the asset's tags is
    // a defect, not a format rule.
    for (const auto& t : material.tags)
        out << "tag " << t << "\n";

    out << "\nambientColor " << num(material.ambient.x) << ' ' << num(material.ambient.y) << ' '
        << num(material.ambient.z) << "\n";
    out << "diffuseColor " << num(material.diffuse.x) << ' ' << num(material.diffuse.y) << ' '
        << num(material.diffuse.z) << "\n";
    out << "specularColor " << num(material.specular.x) << ' ' << num(material.specular.y) << ' '
        << num(material.specular.z) << "\n";
    out << "emissiveColor " << num(material.emissive.x) << ' ' << num(material.emissive.y) << ' '
        << num(material.emissive.z) << "\n";
    out << "shininess " << num(material.shininess) << "\n";
    out << "opacity " << num(material.opacity) << "\n";
    out << "translucency " << num(material.translucency) << "\n";

    if (material.hasViewPortColor) {
        out << "viewPortColor " << num(material.viewPortColor.x) << ' '
            << num(material.viewPortColor.y) << ' ' << num(material.viewPortColor.z) << "\n";
        out << "viewPortAlpha " << num(material.viewPortAlpha) << "\n";
    }

    out << "\nshadeless " << boolStr(material.shadeless) << "\n";
    out << "wireframe " << boolStr(material.wireframe) << "\n";
    out << "transparent " << boolStr(material.transparent) << "\n";
    out << "alphaToCoverage " << boolStr(material.alphaToCoverage) << "\n";
    out << "backfaceCull " << boolStr(material.backfaceCull) << "\n";
    out << "depthless " << boolStr(material.depthless) << "\n";
    out << "castShadows " << boolStr(material.castShadows) << "\n";
    out << "receiveShadows " << boolStr(material.receiveShadows) << "\n";
    out << "autoBlendSkin " << boolStr(material.autoBlendSkin) << "\n";

    bool wroteTexture = false;
    for (const ChannelKey& ck : kChannels) {
        const TextureSlot& slot = material.texture(ck.channel);
        if (!slot.present()) continue;
        if (!wroteTexture) out << "\n";
        wroteTexture = true;
        out << ck.writeTexture << ' ' << portablePath(slot.path, dir) << "\n";
        if (ck.writeIntensity != nullptr)
            out << ck.writeIntensity << ' ' << num(slot.intensity) << "\n";
    }

    if (material.sssEnabled) {
        out << "\nsssEnabled True\n";
        out << "sssRScale " << num(material.sssRScale) << "\n";
        out << "sssGScale " << num(material.sssGScale) << "\n";
        out << "sssBScale " << num(material.sssBScale) << "\n";
    }

    if (material.uvMap) out << "\nuvMap " << portablePath(*material.uvMap, dir) << "\n";
    if (!material.shader.empty()) out << "\nshader " << portablePath(material.shader, dir) << "\n";

    if (!material.shaderParams.empty()) out << "\n";
    for (const auto& [name, values] : material.shaderParams) {
        out << "shaderParam " << name;
        for (const auto& v : values)
            out << ' ' << v;
        out << "\n";
    }

    if (!material.shaderDefines.empty()) out << "\n";
    for (const auto& d : material.shaderDefines)
        out << "shaderDefine " << d << "\n";

    const ShaderConfig& c = material.shaderConfig;
    out << "\nshaderConfig diffuse " << boolStr(c.diffuse) << "\n";
    out << "shaderConfig bump " << boolStr(c.bump) << "\n";
    out << "shaderConfig normal " << boolStr(c.normal) << "\n";
    out << "shaderConfig displacement " << boolStr(c.displacement) << "\n";
    out << "shaderConfig spec " << boolStr(c.spec) << "\n";
    out << "shaderConfig vertexColors " << boolStr(c.vertexColors) << "\n";
    out << "shaderConfig transparency " << boolStr(c.transparency) << "\n";
    out << "shaderConfig ambientOcclusion " << boolStr(c.ambientOcclusion) << "\n";

    out.close();
    if (!out) {
        return std::unexpected(
            MaterialError{MaterialErrorKind::Unwritable, path.string(), 0, "write failed"});
    }
    return {};
}

foundation::MaterialDesc Material::desc() const {
    return foundation::MaterialDesc{name,
                                    ambient,
                                    diffuse,
                                    specular,
                                    shininess,
                                    opacity,
                                    transparent,
                                    texture(TextureChannel::Diffuse).path,
                                    texture(TextureChannel::NormalMap).path};
}

}  // namespace mh::core
