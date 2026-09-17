// SPDX-License-Identifier: Apache-2.0
//
// The viewport backdrop's fragment stage. ORIGINAL work, Apache-2.0; see
// backdrop.vert.

#version 450

layout(location = 0) in vec2 vUV;
layout(location = 0) out vec4 outColor;

layout(std140, binding = 0) uniform Buf {
    vec4 uvTransform;
    vec4 params;
}
ubuf;

layout(binding = 1) uniform sampler2D backdropTex;

void main() {
    // Opacity is ALPHA, and the pipeline blends source-over. That is exact
    // rather than approximate: the backdrop is the first thing drawn into a
    // pass that `beginPass` has already cleared to `bgViewport`
    // (`ViewportWidget.cpp:187`), so blending against the framebuffer is
    // blending against precisely the colour the user sees behind it.
    //
    // Multiplying the colour by opacity instead -- the first thing written here
    // -- would have faded the photograph toward BLACK, which is only the same
    // thing on a black theme.
    outColor = vec4(texture(backdropTex, vUV).rgb, ubuf.params.x);
}
