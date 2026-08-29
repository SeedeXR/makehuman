#!/usr/bin/env bash
# SPDX-License-Identifier: Apache-2.0
#
# Compiles every resources/shaders/rhi/*.vert|.frag to a .qsb containing
# SPIR-V, GLSL 450 and Metal (MSL 12).
#
# Not wired into CMake yet: Qt is not a build dependency until M6, and adding
# it now would make every build require a ~5-minute Qt install for shaders
# nothing yet renders. Run this by hand after touching a shader; M6 replaces it
# with qt6_add_shaders().
#
# Usage:  tools/compile_shaders.sh [outdir]
set -euo pipefail

repo="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
src="$repo/resources/shaders/rhi"
out="${1:-$repo/build/shaders}"

if ! command -v qsb >/dev/null 2>&1; then
    echo "error: qsb not found. It ships with Qt 6 (brew install qt)." >&2
    exit 1
fi

mkdir -p "$out"
count=0
for f in "$src"/*.vert "$src"/*.frag; do
    [ -e "$f" ] || continue
    name="$(basename "$f")"
    qsb --glsl 450 --msl 12 -o "$out/$name.qsb" "$f"
    echo "  ok  $name"
    count=$((count + 1))
done
echo "compiled $count shader stage(s) -> $out"
