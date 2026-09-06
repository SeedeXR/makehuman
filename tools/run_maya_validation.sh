#!/usr/bin/env bash
# SPDX-License-Identifier: Apache-2.0
#
# Reads what we write with Autodesk's OWN FBX implementation.
#
# tools/run_blender_validation.sh already checks every format against a third
# party. This adds the one reader Blender cannot substitute for: for FBX, Maya
# is the reference implementation of the format, so when the two disagree about
# an FBX it is Blender that has to justify itself.
#
# It answers a question Blender can only infer: is the rig LIVE or is the pose
# baked into the vertices? A skinCluster's input shape is the geometry before
# the deformer runs, and comparing it with the output settles it.
#
# Not in CI: no Maya on the runner, and no licence for one. Same standing as the
# Blender harness -- run it by hand after touching the FBX path.
set -euo pipefail

MAYAPY="${MAYAPY:-/Applications/Autodesk/maya2027/Maya.app/Contents/bin/mayapy}"
repo="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
out="${1:-${TMPDIR:-/tmp}/mh_maya_validate}"

if [ ! -x "$MAYAPY" ]; then
    echo "error: mayapy not found at $MAYAPY (set MAYAPY=/path/to/mayapy)" >&2
    exit 1
fi

app="$repo/build/macos-arm64-release/src/app/makehuman.app/Contents/MacOS/makehuman"
if [ ! -x "$app" ]; then
    echo "error: $app not built." >&2
    echo "  cmake --build --preset macos-arm64-release" >&2
    exit 1
fi

mkdir -p "$out"
# The POSED export, which is the only one where live-versus-baked is a question
# at all. An unposed file's rest and deformed shapes agree by construction and
# would report `live_rig: false` while being perfectly correct.
"$app" --rig mixamo_superset --pose tpose --export "$out/posed.fbx" >/dev/null
echo "exported $out/posed.fbx"

"$MAYAPY" "$repo/tools/maya_validate.py" "$out/posed.fbx" 2>/dev/null |
    grep '^MAYA_VALIDATE:' |
    python3 "$repo/tools/maya_check.py"
