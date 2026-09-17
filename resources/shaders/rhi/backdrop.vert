// SPDX-License-Identifier: Apache-2.0
//
// The viewport backdrop's vertex stage: a reference photograph behind the
// model, which is what the reference's BackgroundChooser exists for
// (`plugins/0_modeling_background.py:104`). ORIGINAL work, not a translation of
// the reference's shaders, so Apache-2.0 rather than AGPL-3.0 -- see
// LICENSING.md 4.
//
// NO VERTEX BUFFER. The three corners come from `gl_VertexIndex`, which is the
// standard fullscreen-triangle trick: it needs no buffer, no vertex layout and
// no upload, and a single triangle avoids the diagonal seam two would share.
// The grid next door owns a buffer because its geometry is real; this one's
// is three constants.
//
// The backdrop does NOT share the scene's uniform block. It is in screen space
// by definition -- the camera orbits by rotating the model, and a photograph
// pinned behind the model must not rotate with it.

#version 450

layout(location = 0) out vec2 vUV;

layout(std140, binding = 0) uniform Buf {
    // Where the image sits, as (scaleU, scaleV, offsetU, offsetV). Computed on
    // the CPU by `mh::ui::coverSource` -- the SAME function `overBackground`
    // uses for `--render` -- so the viewport and a production render place a
    // photograph identically. Recomputing cover here would be a second
    // implementation of one rule, which is how the two would drift.
    vec4 uvTransform;
    // x = opacity. The rest is padding: std140 rounds a block up to 16 bytes
    // anyway, so naming it is free and keeps the C++ side's struct honest.
    vec4 params;
}
ubuf;

void main() {
    // (0,0), (2,0), (0,2) in UV -- a triangle twice the viewport's size, whose
    // visible third is exactly the viewport.
    const vec2 corner = vec2(float((gl_VertexIndex << 1) & 2), float(gl_VertexIndex & 2));
    gl_Position       = vec4(corner * 2.0 - 1.0, 0.0, 1.0);
    vUV               = corner * ubuf.uvTransform.xy + ubuf.uvTransform.zw;
}
