#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Checks Blender's report against what the exporters claim. Exits non-zero on
any disagreement, so this is usable as a gate rather than something to read."""
import json
import sys

# Measured from the reference (tests/golden/mesh/MANIFEST.json) and from
# RenderMesh: 19,158 welded vertices, 21,833 unwelded, 36,972 triangles, and a
# 16.9455 dm body -- 169.5 cm, a real human height.
EXPECT = {
    "base.obj": {"vertices": 19158, "triangles": 36972, "tallest": 16.9455, "uv_layers": 1},
    "base.glb": {"vertices": 21833, "triangles": 36972, "tallest": 1.69455, "uv_layers": 1},
    "base.fbx": {"vertices": 21833, "triangles": 36972, "tallest": 1.69455, "uv_layers": 1},
    # The rigged export: same geometry, plus the full 163-bone skeleton and a
    # weight on every vertex. "bones" and "skinned" are the checks that a static
    # geometry comparison cannot make.
    "rigged.glb": {
        "vertices": 21833, "triangles": 36972, "tallest": 1.69455, "uv_layers": 1,
        "bones": 163, "armatures": 1, "skinned": 21833,
    },
}

failures = 0
seen = set()
for line in sys.stdin:
    line = line.strip()
    if not line:
        continue
    d = json.loads(line)
    name = d["file"].split("/")[-1]
    seen.add(name)

    if not d.get("ok"):
        print(f"FAIL {name}: {d.get('error')}")
        failures += 1
        continue

    want = EXPECT.get(name)
    if want is None:
        print(f"skip {name}: no expectation recorded")
        continue

    problems = []
    if d["vertices"] != want["vertices"]:
        problems.append(f"vertices {d['vertices']} != {want['vertices']}")
    if d["triangles"] != want["triangles"]:
        problems.append(f"triangles {d['triangles']} != {want['triangles']}")
    if d["uv_layers"] < want["uv_layers"]:
        problems.append(f"uv_layers {d['uv_layers']} < {want['uv_layers']}")
    # 0.5% covers float32 storage and Blender's own unit conversion.
    if abs(d["tallest_extent"] - want["tallest"]) > want["tallest"] * 0.005:
        problems.append(f"tallest {d['tallest_extent']} != ~{want['tallest']}")
    if "bones" in want and d.get("bones") != want["bones"]:
        problems.append(f"bones {d.get('bones')} != {want['bones']}")
    if "armatures" in want and d.get("armatures") != want["armatures"]:
        problems.append(f"armatures {d.get('armatures')} != {want['armatures']}")
    if "skinned" in want and d.get("skinned_vertices") != want["skinned"]:
        problems.append(f"skinned vertices {d.get('skinned_vertices')} != {want['skinned']}")

    if problems:
        print(f"FAIL {name}: " + "; ".join(problems))
        failures += 1
    else:
        extra = ""
        if d.get("bones"):
            extra = f", {d['bones']} bones, {d['skinned_vertices']} skinned"
        print(f"ok   {name}: {d['vertices']} verts, {d['triangles']} tris, "
              f"tallest {d['tallest_extent']}, {d['uv_layers']} uv layer(s){extra}")

missing = set(EXPECT) - seen
for m in sorted(missing):
    print(f"FAIL {m}: never reported")
    failures += 1

agreed = len(EXPECT) - failures
print(f"\n{agreed}/{len(EXPECT)} exports agree with Blender")
sys.exit(1 if failures else 0)
