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
// The original's #ifdef NORMALMAP fork is handled by a UNIFORM BRANCH rather
// than a second shader. Two pipelines would double the state for what is a
// per-mesh runtime choice, and a branch on a uniform is uniform control flow --
// every fragment in a draw takes the same side. The tangent attribute is always
// present, which costs 4 floats a vertex (~1.3 MB on the subdivided mesh) and
// removes the need to switch layouts.
//
// VERTEX_COLOR is still not ported: nothing supplies per-vertex colours.

#version 450

layout(location = 0) in vec3 position;
layout(location = 1) in vec3 normal;
layout(location = 2) in vec2 texcoord;
/// xyz = tangent, w = handedness (+1 / -1) from Lengyel's method.
layout(location = 3) in vec4 tangent;

layout(location = 0) out vec2 vTexCoord;
layout(location = 1) out vec3 vNormal;   // view space
layout(location = 2) out vec3 vTangent;  // view space
layout(location = 3) out float vHanded;

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

    // The reference builds its binormal as `cross(vNormal, tang)`, discarding
    // handedness (litsphere_fragment_shader.txt's NORMALMAP path via
    // litsphere_vertex_shader.txt:59). On a symmetric human that is wrong:
    // mirrored UV islands have opposite handedness, so one side of the body
    // gets its normal-map lighting inverted. We carry `w` through and apply it
    // in the fragment stage.
    //
    // This is a deliberate divergence, of a piece with not porting the
    // reference's three tangent bugs (project_context.md §8).
    vTangent = normalize(mat3(ubuf.normalMatrix) * tangent.xyz);
    vHanded  = tangent.w;
}
