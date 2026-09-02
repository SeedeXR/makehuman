#!/usr/bin/env bash
# SPDX-License-Identifier: Apache-2.0
#
# Round-trips the base mesh through our exporters and asks Blender -- a third
# implementation that has never seen this codebase -- whether the results are
# what we claim.
#
# Every other check here shares lineage with what it checks: parity tests
# compare us against the reference we were ported from, and the glTF/FBX tests
# read back through assimp, which also wrote the FBX. A convention both sides
# get wrong the same way passes all of it. This does not.
#
# Not in CI: the GitHub runner has no Blender, and installing it per-run costs
# more than the check is worth at this cadence. Run it by hand after touching
# an exporter.
set -euo pipefail

BLENDER="${BLENDER:-/Applications/Blender.app/Contents/MacOS/Blender}"
repo="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
out="${1:-${TMPDIR:-/tmp}/mh_blender_validate}"

if [ ! -x "$BLENDER" ]; then
    echo "error: Blender not found at $BLENDER (set BLENDER=/path/to/blender)" >&2
    exit 1
fi

fixture="$repo/build/macos-arm64-release/tests/mh_export_fixture"
if [ ! -x "$fixture" ]; then
    echo "error: $fixture not built." >&2
    echo "  cmake --build --preset macos-arm64-release --target mh_export_fixture" >&2
    exit 1
fi

mkdir -p "$out"
echo "exporting to $out"
"$fixture" "$out"

# A POSED export, through the application rather than the fixture: posing is an
# app-level sequence and the point is to check the file a user actually gets.
#
# Measured, not assumed: the geometry is baked into the pose (POSITION bounds go
# from +-0.526 to +-0.843 in x) AND the exported skeleton follows it (41 of 163
# joint node matrices differ from the rest export, by up to 0.62). Blender then
# applies the armature itself and moves the mesh by 9e-06 -- a no-op. So the
# file is self-consistent: the posed state IS its bind pose.
#
# The corollary matters for what this harness can and cannot check: because a
# DCC's own skinning is a no-op on our exports, Blender CANNOT independently
# verify LBS from them. That needs an export carrying rest geometry with a posed
# armature, which is an interchange-semantics decision, not a validation gap.
app="$repo/build/macos-arm64-release/src/app/makehuman"
if [ -x "$app" ]; then
    "$app" --pose tpose --export "$out/posed.glb" >/dev/null 2>&1 \
        && echo "posed.glb: T-pose, baked and re-bound" \
        || echo "warn: posed.glb export failed"
else
    echo "skip posed.glb: $app not built"
fi

# Pixar's own validator, on the stages we generate. It is the tool that caught
# the SkelRoot requirement for blend shapes -- Blender imports the
# non-conformant version without complaint, so it cannot be the one that tells
# you. expressions.usda deliberately has NO skeleton, which is the case the
# application cannot produce because it always builds a rig.
if command -v usdchecker >/dev/null 2>&1; then
    for stage in "$out/base.usda" "$out/expressions.usda"; do
        if usdchecker "$stage" >/dev/null 2>&1; then
            echo "ok   $(basename "$stage"): usdchecker validates"
        else
            echo "FAIL $(basename "$stage"): usdchecker rejects the stage"
            usdchecker "$stage" 2>&1 | grep -v "Coding Error" | head -5
            exit 1
        fi
    done
else
    echo "skip usdchecker: not installed"
fi

"$BLENDER" --background --python "$repo/tools/blender_validate.py" -- \
    "$out/base.obj" "$out/posed.glb" "$out/base.glb" "$out/expressions.glb" "$out/expressions.fbx" "$out/expressions.usda" "$out/base.fbx" "$out/rigged.glb" "$out/morphed.glb" "$out/rigged.fbx" "$out/base.usda" 2>/dev/null |
    grep '^BLENDER_VALIDATE:' | sed 's/^BLENDER_VALIDATE://' |
    python3 "$repo/tools/blender_check.py"
