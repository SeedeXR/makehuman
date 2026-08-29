// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Lit-sphere (matcap) vertex stage, ported to Qt RHI GLSL 450 from the
// reference's GLSL 120 shader (data/shaders/glsl/litsphere_vertex_shader.txt).
//
// This is a TRANSLATION OF AGPL SHADER SOURCE, so it is AGPL-3.0, not
// Apache-2.0, and must never be moved into a permissive module.
// See LICENSING.md 4.
//
// What changed, and why -- the reference relies on fixed-function state that
// no longer exists:
//   ftransform()      -> an explicit mvp multiply
//   gl_NormalMatrix   -> a normalMatrix supplied in the uniform block
//   gl_MultiTexCoord0 -> an explicit vertex attribute
//   gl_TexCoord[0]    -> an explicit varying
//   attribute/varying -> in/out with explicit locations, which RHI requires
//
// The #ifdef NORMALMAP / VERTEX_COLOR variants of the original are not
// branched here: RHI wants one pipeline per variant, so the tangent path
// becomes a separate shader rather than a preprocessor fork.

#version 450

layout(location = 0) in vec3 position;
layout(location = 1) in vec3 normal;
layout(location = 2) in vec2 texcoord;

layout(location = 0) out vec2 vTexCoord;
layout(location = 1) out vec3 vNormal;  // view space

layout(std140, binding = 0) uniform Buf {
    mat4 mvp;
    mat4 modelView;
    mat4 normalMatrix;
    vec4 params;  // x = AdditiveShading, y = normalmapIntensity
}
ubuf;

void main() {
    gl_Position = ubuf.mvp * vec4(position, 1.0);
    vTexCoord   = texcoord;

    // The reference's gl_NormalMatrix is the inverse-transpose of the
    // modelview upper-left 3x3; it is passed in rather than derived here
    // because deriving it per vertex is wasteful and RHI has no equivalent.
    vNormal = normalize(mat3(ubuf.normalMatrix) * normal);
}
