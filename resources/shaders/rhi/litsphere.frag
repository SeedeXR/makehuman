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
//
// A THIRD thing is preserved, and it is the easiest to "fix" by accident: the
// reference samples the RAW INTERPOLATED normal, `vec3 normal = vNormal;`
// (litsphere_fragment_shader.txt:78). It does not renormalize per fragment.
// Interpolation shortens the normal across a triangle, which pulls the
// litsphere UV toward the sphere centre, and the shipped litspheres were
// authored against exactly that.
//
// We renormalized here until it was measured: doing so changes 0.98% of the
// rendered frame, by up to 107/255 in a channel -- concentrated where the
// matcap gradient is steepest. That is a visible difference, not rounding, so
// parity wins and the normalize() is gone.
//
// Renormalizing IS the more conventional choice, and it remains available as a
// deliberate quality decision. It was not adopted on a "more stable under
// tessellation" argument: that was tested and the result was confounded
// (subdividing moves the silhouette too), 3.09% vs 2.92% of the frame, which
// favours nothing.

#version 450

layout(location = 0) in vec2 vTexCoord;
layout(location = 1) in vec3 vNormal;
layout(location = 2) in vec3 vTangent;
layout(location = 3) in float vHanded;

layout(location = 0) out vec4 fragColor;

layout(std140, binding = 0) uniform Buf {
    mat4 mvp;
    mat4 modelView;
    mat4 normalMatrix;
    vec4 params;  // x = AdditiveShading
}
ubuf;

/// Per-MESH, unlike `Buf` which is per frame. Whether a normal map exists is a
/// property of the material, so it cannot live in a buffer shared by every draw
/// in the frame.
layout(std140, binding = 4) uniform MeshBuf {
    // x = normalmapIntensity, y = 1 when a normal map is bound. At 0 the map
    // is NOT sampled: the slot holds a placeholder only because a declared
    // binding must point at a live texture.
    vec4 material;
}
mbuf;

layout(binding = 1) uniform sampler2D litsphereTexture;
layout(binding = 2) uniform sampler2D diffuseTexture;
layout(binding = 3) uniform sampler2D normalTexture;

void main() {
    // With a normal map: the reference's NORMALMAP path
    // (litsphere_fragment_shader.txt:63-77) -- unpack, scale by intensity, and
    // rotate from tangent into view space, normalizing at the end.
    //
    // Without one: the RAW interpolated normal, NOT renormalized -- see the
    // header. This one word changes every rendered skin.
    //
    // A flat 1x1 placeholder cannot stand in for the branch: it would give
    // normalize(TBN * (0,0,1)) == normalize(vNormal), and normalizing is
    // exactly what the no-map path must not do.
    vec3 normal;
    if (mbuf.material.y > 0.5) {
        const vec3 packed = texture(normalTexture, vTexCoord).rgb;
        const vec3 unpacked = 2.0 * packed - 1.0;
        // Intensity scales XY only, keeping Z.
        //
        // The reference writes `(2.0*normalH - 1.0) * normalmapIntensity` and
        // then normalizes (litsphere_fragment_shader.txt:74-77). A uniform
        // scale followed by a normalize CANCELS EXACTLY, so its
        // `normalmapIntensity` uniform does nothing at all -- it only bites
        // under CALC_NORMAL_Z, which the reference leaves commented out at :64.
        //
        // Porting that would ship a control that silently does nothing, so this
        // diverges deliberately: flattening XY against a fixed Z is the standard
        // normal-map strength idiom and is what the CALC_NORMAL_Z branch was
        // reaching for. Verified by test: 1.0 vs 0.01 changes the image.
        const vec3 tangentSpace = vec3(unpacked.xy * mbuf.material.x, unpacked.z);
        // Handedness applied here; the reference drops it. See litsphere.vert.
        const vec3 n = normalize(vNormal);
        const vec3 t = normalize(vTangent - n * dot(n, vTangent));  // Gram-Schmidt
        const vec3 b = cross(n, t) * vHanded;
        normal = normalize(mat3(t, b, n) * tangentSpace);
    } else {
        normal = vNormal;
    }

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
