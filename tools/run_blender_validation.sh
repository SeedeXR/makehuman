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

"$BLENDER" --background --python "$repo/tools/blender_validate.py" -- \
    "$out/base.obj" "$out/base.glb" "$out/base.fbx" "$out/rigged.glb" "$out/morphed.glb" 2>/dev/null |
    grep '^BLENDER_VALIDATE:' | sed 's/^BLENDER_VALIDATE://' |
    python3 "$repo/tools/blender_check.py"
