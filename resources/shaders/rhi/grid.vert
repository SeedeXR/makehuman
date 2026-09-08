// SPDX-License-Identifier: Apache-2.0
//
// The ground grid's vertex stage. ORIGINAL work, not a translation of the
// reference's shaders, so it is Apache-2.0 rather than AGPL-3.0 like
// litsphere.* -- see LICENSING.md 4.
//
// It shares the scene's uniform block rather than owning one: the grid must be
// rotated by exactly the same matrix as the body, because this camera orbits by
// rotating the MODEL. A grid with its own transform would drift out of step the
// first time the two were computed differently. Only `mvp` is read; the rest of
// the block is declared so the layout matches the buffer the scene already
// uploads.

#version 450

layout(location = 0) in vec3 position;

layout(std140, binding = 0) uniform Buf {
    mat4 mvp;
    mat4 modelView;
    mat4 normalMatrix;
    vec4 params;
}
ubuf;

void main() {
    gl_Position = ubuf.mvp * vec4(position, 1.0);
}
