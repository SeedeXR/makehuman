// SPDX-License-Identifier: Apache-2.0
//
// Vertex stage for the metallic-roughness viewport.
//
// ORIGINAL WORK, not a port. It shares no line with the reference's
// litsphere_vertex_shader.txt, which is why it is Apache-2.0 while
// `litsphere.vert` next to it is AGPL-3.0 (LICENSING.md 4).
//
// It exists as a separate stage rather than an extra output on litsphere.vert
// for two reasons. The obvious one is that PBR needs the view vector, so it
// needs `vViewPos`, which the litsphere stage has no use for. The other is that
// litsphere.vert is the parity-critical path -- M6 compares its output against
// the reference pixel for pixel -- and the cheapest way to guarantee an added
// varying cannot perturb it is to not touch the file.
//
// The vertex LAYOUT is deliberately identical to litsphere.vert's, so both
// pipelines consume the same buffers and `SceneResources` uploads geometry once
// regardless of which shading model is active.

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
layout(location = 4) out vec3 vViewPos;  // view space position

layout(std140, binding = 0) uniform Buf {
    mat4 mvp;
    mat4 modelView;
    mat4 normalMatrix;
    vec4 params;
}
ubuf;

void main() {
    const vec4 viewPos = ubuf.modelView * vec4(position, 1.0);

    gl_Position = ubuf.mvp * vec4(position, 1.0);
    vTexCoord   = texcoord;
    vViewPos    = viewPos.xyz;

    // Unlike the litsphere stage, both of these are consumed as UNIT vectors by
    // the BRDF, and the fragment stage renormalizes after interpolation. The
    // litsphere path must not do that -- it changes 0.98% of the frame there --
    // but here it is simply correct: a shortened normal would bias every dot
    // product in the microfacet terms.
    vNormal  = mat3(ubuf.normalMatrix) * normal;
    vTangent = mat3(ubuf.normalMatrix) * tangent.xyz;
    vHanded  = tangent.w;
}
