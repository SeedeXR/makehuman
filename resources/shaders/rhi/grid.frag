// SPDX-License-Identifier: Apache-2.0
//
// The ground grid's fragment stage: one colour, no lighting. ORIGINAL work,
// Apache-2.0; see grid.vert.
//
// `design.md` 4 names the colour: `--border-subtle` #35353b, the same value the
// panels use for a separator. A floor grid is exactly that -- a division line,
// not content -- and reading it off the token table is what keeps the viewport
// and the chrome in one palette.

#version 450

layout(location = 0) out vec4 outColor;

void main() {
    outColor = vec4(0.208, 0.208, 0.231, 1.0);
}
