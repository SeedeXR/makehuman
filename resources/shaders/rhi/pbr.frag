// SPDX-License-Identifier: Apache-2.0
//
// Metallic-roughness fragment stage for the viewport.
//
// ORIGINAL WORK, not a port. The reference has no PBR path at all -- it shades
// exclusively with the litsphere matcap -- so there was nothing to translate.
// The equations below are the published microfacet model (Cook-Torrance with
// Trowbridge-Reitz/GGX distribution, height-correlated Smith visibility and
// Schlick's Fresnel approximation) as written up in Karis, "Real Shading in
// Unreal Engine 4" (2013) and Lagarde & de Rousiers, "Moving Frostbite to PBR"
// (2014). That is why this file is Apache-2.0 while `litsphere.frag` beside it
// is AGPL-3.0 (LICENSING.md 4).
//
// WHY NO IBL. A physically-based shader normally gets its ambient term from a
// prefiltered environment map. We ship no environment: every usable HDRI is
// either CC-BY (attribution we cannot honour inside a binary asset) or
// non-commercial, and generating a convincing one procedurally is a project of
// its own. So the ambient is an analytic hemisphere -- sky colour above,
// bounce colour below, interpolated by the normal's Y. It is not
// energy-accurate and does not pretend to be; it exists so that surfaces facing
// away from all three lights are shaded rather than black, which is the actual
// failure an IBL prevents.
//
// WHY THE LIGHTS ARE IN VIEW SPACE. The camera is fixed and the MODEL rotates
// (SceneResources::Camera). Lights fixed in view space therefore behave like a
// studio rig bolted to the camera: turning the character walks it through the
// lighting instead of dragging the lighting along. That is the behaviour a
// modelling viewport wants, and it matches what the litsphere already does --
// a matcap is, definitionally, lighting locked to the eye.
//
// COLOUR SPACE. The render target is RGBA8 with no sRGB flag
// (OffscreenRenderer.cpp:66), so nothing linearizes for us. Albedo maps are
// sRGB-encoded PNGs sampled as UNORM, so they are decoded on the way in, the
// BRDF runs in linear light, and the result is tonemapped and re-encoded on the
// way out. The litsphere path does none of this -- it works in gamma space
// throughout -- which is correct for it, because the shipped litspheres were
// authored against exactly that arithmetic.

#version 450

layout(location = 0) in vec2 vTexCoord;
layout(location = 1) in vec3 vNormal;
layout(location = 2) in vec3 vTangent;
layout(location = 3) in float vHanded;
layout(location = 4) in vec3 vViewPos;

layout(location = 0) out vec4 fragColor;

layout(std140, binding = 0) uniform Buf {
    mat4 mvp;
    mat4 modelView;
    mat4 normalMatrix;
    vec4 params;
}
ubuf;

/// Per-MESH. Metallic and roughness are material properties, so they cannot
/// live in `Buf`, which is shared by every draw in the frame.
layout(std140, binding = 4) uniform MeshBuf {
    // x = normalmapIntensity, y = 1 when a normal map is bound, z = 1 when an
    // AO map is bound, w unused.
    vec4 material;
    // x = metallic, y = roughness, zw unused.
    vec4 pbr;
    // rgb = the material's diffuse colour, glTF's baseColorFactor;
    // w = its opacity, the alpha of that same factor.
    vec4 base;
}
mbuf;

layout(binding = 2) uniform sampler2D albedoTexture;
layout(binding = 3) uniform sampler2D normalTexture;
layout(binding = 5) uniform sampler2D aoTexture;

const float kPi = 3.14159265359;

/// Three-point studio rig, in view space: key over the viewer's left shoulder,
/// a dimmer cool fill opposite it to keep the shadow side readable, and a rim
/// behind the subject to separate the silhouette from the background.
/// Intensities are in arbitrary linear units chosen so mid-grey skin lands near
/// mid-grey on screen after the tonemap.
const vec3 kKeyDir = vec3(-0.400, 0.520, 0.756);
const vec3 kKeyColor = vec3(1.000, 0.976, 0.945) * 3.20;
const vec3 kFillDir = vec3(0.640, 0.128, 0.758);
const vec3 kFillColor = vec3(0.855, 0.898, 1.000) * 0.90;
const vec3 kRimDir = vec3(0.180, 0.400, -0.898);
const vec3 kRimColor = vec3(1.000, 0.960, 0.900) * 1.40;

/// Analytic ambient hemisphere; see the header for why this is not an IBL.
const vec3 kSkyColor = vec3(0.290, 0.330, 0.400);
const vec3 kGroundColor = vec3(0.180, 0.160, 0.150);

/// Trowbridge-Reitz (GGX) normal distribution.
float distributionGgx(float noh, float alpha) {
    const float a2 = alpha * alpha;
    const float d = noh * noh * (a2 - 1.0) + 1.0;
    return a2 / max(kPi * d * d, 1e-7);
}

/// Height-correlated Smith visibility, i.e. G / (4 NoL NoV) already folded in,
/// which is why the BRDF below multiplies D * V * F with no separate 1/(4...).
float visibilitySmith(float nov, float nol, float alpha) {
    const float a2 = alpha * alpha;
    const float gv = nol * sqrt(nov * nov * (1.0 - a2) + a2);
    const float gl = nov * sqrt(nol * nol * (1.0 - a2) + a2);
    return 0.5 / max(gv + gl, 1e-5);
}

vec3 fresnelSchlick(vec3 f0, float u) {
    const float f = pow(1.0 - u, 5.0);
    return f0 + (1.0 - f0) * f;
}

/// One directional light's contribution.
vec3 shadeLight(vec3 n, vec3 v, vec3 l, vec3 radiance, vec3 diffuseColor, vec3 f0, float alpha) {
    const float nol = dot(n, l);
    if (nol <= 0.0) return vec3(0.0);

    const vec3 h = normalize(v + l);
    const float nov = abs(dot(n, v)) + 1e-5;
    const float noh = clamp(dot(n, h), 0.0, 1.0);
    const float voh = clamp(dot(v, h), 0.0, 1.0);

    const vec3 f = fresnelSchlick(f0, voh);
    const vec3 specular = distributionGgx(noh, alpha) * visibilitySmith(nov, nol, alpha) * f;
    // Energy split: what Fresnel reflects cannot also diffuse. Metals have no
    // diffuse lobe at all, which `diffuseColor` already encodes by being black.
    const vec3 diffuse = (vec3(1.0) - f) * diffuseColor / kPi;

    return (diffuse + specular) * radiance * nol;
}

/// Narkowicz's ACES filmic curve. A tonemap is not optional here: three lights
/// plus ambient routinely exceed 1.0 on a lit cheekbone, and clamping instead
/// would flatten every highlight into a white blob.
vec3 tonemapAces(vec3 x) {
    const float a = 2.51;
    const float b = 0.03;
    const float c = 2.43;
    const float d = 0.59;
    const float e = 0.14;
    return clamp((x * (a * x + b)) / (x * (c * x + d) + e), 0.0, 1.0);
}

void main() {
    // Same tangent-space unpack as the litsphere path, with ONE difference: the
    // no-map branch normalizes here. The litsphere path must not (it changes
    // 0.98% of the frame there, and the shipped matcaps were authored against
    // the un-normalized vector); a microfacet BRDF has no such excuse, because
    // an interpolated short normal biases every dot product in the model.
    vec3 n = normalize(vNormal);
    if (mbuf.material.y > 0.5) {
        const vec3 unpacked = 2.0 * texture(normalTexture, vTexCoord).rgb - 1.0;
        const vec3 tangentSpace = vec3(unpacked.xy * mbuf.material.x, unpacked.z);
        const vec3 t = normalize(vTangent - n * dot(n, vTangent));  // Gram-Schmidt
        const vec3 b = cross(n, t) * vHanded;
        n = normalize(mat3(t, b, n) * tangentSpace);
    }

    // The camera sits at the view-space origin, so the vector to the eye is
    // just the negated fragment position.
    const vec3 v = normalize(-vViewPos);

    const vec4 texel = texture(albedoTexture, vTexCoord);
    // sRGB -> linear (see header), then the material's base colour. The factor
    // is already linear -- a .mhmat's diffuseColor is a multiplier, not a
    // pixel -- so it must NOT go through the same decode.
    const vec3 albedo = pow(texel.rgb, vec3(2.2)) * mbuf.base.rgb;

    const float metallic = clamp(mbuf.pbr.x, 0.0, 1.0);
    // The floor is not cosmetic: at roughness 0 the GGX denominator collapses to
    // a delta function, and a directional light -- which subtends no solid angle
    // -- then produces either nothing or a single blown pixel. 0.045 is the
    // usual clamp, corresponding to a just-barely-not-mirror surface.
    const float roughness = clamp(mbuf.pbr.y, 0.045, 1.0);
    const float alpha = roughness * roughness;

    // Dielectrics reflect ~4% at normal incidence; metals reflect their own
    // colour and have no diffuse albedo.
    const vec3 f0 = mix(vec3(0.04), albedo, metallic);
    const vec3 diffuseColor = albedo * (1.0 - metallic);

    vec3 color = vec3(0.0);
    color += shadeLight(n, v, normalize(kKeyDir), kKeyColor, diffuseColor, f0, alpha);
    color += shadeLight(n, v, normalize(kFillDir), kFillColor, diffuseColor, f0, alpha);
    color += shadeLight(n, v, normalize(kRimDir), kRimColor, diffuseColor, f0, alpha);

    // Hemisphere ambient. The specular half is scaled by (1 - roughness) as a
    // stand-in for a prefiltered environment: a mirror should pick up most of
    // the surroundings, a chalk surface almost none.
    const vec3 hemisphere = mix(kGroundColor, kSkyColor, 0.5 + 0.5 * n.y);
    color += diffuseColor * hemisphere;
    color += f0 * hemisphere * (1.0 - roughness);

    // Occlusion multiplies the whole result, matching the litsphere path's
    // ordering so switching shading models does not move where creases darken.
    if (mbuf.material.z > 0.5) {
        color *= texture(aoTexture, vTexCoord).rgb;
    }

    // The texture's alpha AND the material's opacity. A cut-out needs the
    // first; a uniformly translucent material like the shipped xray skin
    // (`opacity 0.1`) has no alpha channel at all and needs the second.
    fragColor = vec4(pow(tonemapAces(color), vec3(1.0 / 2.2)), texel.a * mbuf.base.w);
}
