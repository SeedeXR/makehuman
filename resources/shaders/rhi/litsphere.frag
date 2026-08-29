// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Lit-sphere (matcap) fragment stage, ported to Qt RHI GLSL 450 from
// data/shaders/glsl/litsphere_fragment_shader.txt.
//
// This is a TRANSLATION OF AGPL SHADER SOURCE -- AGPL-3.0, never permissive.
//
// The two magic numbers are deliberately preserved EXACTLY, because they are
// what make the result pixel-comparable with the reference renderer
// (memory/todo.md M6):
//
//   * the 0.495 normal scale (not 0.5) when mapping the view-space normal into
//     litsphere UV space -- it insets the sample by half a texel so the sphere
//     edge never wraps;
//   * the `2.0 - mean(shading)` term in the diffuse combine, which is a
//     brightness compensation, not a normalisation, and does not simplify.
//
// Changing either silently changes every rendered skin.

#version 450

layout(location = 0) in vec2 vTexCoord;
layout(location = 1) in vec3 vNormal;

layout(location = 0) out vec4 fragColor;

layout(std140, binding = 0) uniform Buf {
    mat4 mvp;
    mat4 modelView;
    mat4 normalMatrix;
    vec4 params;  // x = AdditiveShading, y = normalmapIntensity
}
ubuf;

layout(binding = 1) uniform sampler2D litsphereTexture;
layout(binding = 2) uniform sampler2D diffuseTexture;

void main() {
    const vec3 normal = normalize(vNormal);

    // 0.495, not 0.5 -- see the header.
    const vec3 shading =
        texture(litsphereTexture, (normal * vec3(0.495) + vec3(0.5)).xy).rgb;

    const vec4 diffuse         = texture(diffuseTexture, vTexCoord);
    const float additive       = ubuf.params.x;
    const float brightnessComp = 2.0 - (shading.r + shading.g + shading.b) / 3.0;

    vec4 outColor;
    outColor.rgb = (1.0 - additive) * shading * diffuse.rgb * vec3(brightnessComp);
    outColor.rgb += additive * (shading + diffuse.rgb);
    outColor.a = diffuse.a;

    fragColor = outColor;
}
