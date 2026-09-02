#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Checks Blender's report against what the exporters claim. Exits non-zero on
any disagreement, so this is usable as a gate rather than something to read."""
import json
import sys

# Measured from the reference (tests/golden/mesh/MANIFEST.json) and from
# RenderMesh: 19,158 welded vertices, 21,833 unwelded, 36,972 triangles, and a
# 16.9455 dm body -- 169.5 cm, a real human height.
_EXPRESSION_MESH = {
    "vertices": 21833, "triangles": 36972, "tallest": 1.69455, "uv_layers": 1,
}

_EXPRESSION_KEYS = {
    "eye-left-closure": 186,
    "eye-left-opened-up": 200,
    "eye-left-slit": 284,
    "eye-right-closure": 186,
    "eye-right-opened-up": 200,
    "eye-right-slit": 284,
    "eyebrows-left-down": 54,
    "eyebrows-left-extern-up": 72,
    "eyebrows-left-inner-up": 40,
    "eyebrows-left-up": 52,
    "eyebrows-right-down": 54,
    "eyebrows-right-extern-up": 79,
    "eyebrows-right-inner-up": 40,
    "eyebrows-right-up": 54,
    "mouth-compression": 704,
    "mouth-corner-puller": 738,
    "mouth-depression": 631,
    "mouth-depression-retraction": 584,
    "mouth-elevation": 657,
    "mouth-eversion": 392,
    "mouth-open": 1445,
    "mouth-parling": 237,
    "mouth-part-later": 971,
    "mouth-protusion": 732,
    "mouth-pursing": 906,
    "mouth-retraction": 646,
    "mouth-upward-retraction": 778,
    "neck-platysma": 854,
    "nose-compression": 50,
    "nose-depression": 269,
    "nose-left-dilatation": 78,
    "nose-left-elevation": 180,
    "nose-right-dilatation": 78,
    "nose-right-elevation": 180,
}

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
    # Morph targets. The moved-vertex counts are the source .target files'
    # NON-ZERO rows plus their UV-seam duplicates -- nose-base-up has 305 rows
    # of which 11 are literally (0,0,0), so only 294 move. A shape key that
    # exists but displaces nothing is the failure a name-only check misses.
    # USD ASCII, written from the spec rather than by linking OpenUSD.
    "base.usda": {"vertices": 21833, "triangles": 36972, "tallest": 1.69455, "uv_layers": 1},
    # The same rig and morphs through FBX. The moved-vertex counts must match
    # morphed.glb EXACTLY -- two independent formats agreeing is a stronger
    # statement than either one matching an expectation.
    # vertex_groups is 139, not 163: only 139 bones carry any weight in
    # default_weights.mhw, and both assimp and Blender drop the empty ones.
    "rigged.fbx": {
        "vertices": 21833, "triangles": 36972, "tallest": 1.69455, "uv_layers": 1,
        "bones": 163, "armatures": 1, "skinned": 21833, "vertex_groups": 139,
        "shape_keys": {
            "head-oval": 2200,
            "head-trans-backward": 5865,
            "nose-base-up": 294,
        },
    },
    # The SHIPPED blendshape set -- what `makehuman --blendshapes` writes: the
    # 34 expression units, each blended across african/asian/caucasian by the
    # character's macro factors. NOT the 102 `.target` files on disk, which are
    # 34 units x 3 ethnicities; exporting those would give a DCC three
    # near-duplicate keys per expression.
    #
    # Counts are Blender's own, measured 2026-09-02. Left/right pairs agree
    # where the data is symmetric (eye closure 186/186) and differ where it is
    # not (eyebrows-extern-up 72 left, 79 right), so a symmetry bug in the blend
    # shows here rather than hiding behind a total.
    #
    # ONE table for both formats, deliberately. Our glTF writer and assimp's FBX
    # writer are independent implementations; requiring them to agree key-for-key
    # is a stronger statement than either matching an expectation, and writing
    # the numbers twice would let them drift apart silently.
    "expressions.glb": dict(_EXPRESSION_MESH, shape_keys=_EXPRESSION_KEYS),
    "expressions.fbx": dict(_EXPRESSION_MESH, shape_keys=_EXPRESSION_KEYS),
    "morphed.glb": {
        "vertices": 21833, "triangles": 36972, "tallest": 1.69455, "uv_layers": 1,
        "shape_keys": {
            "head-oval": 2200,
            "head-trans-backward": 5865,
            "nose-base-up": 294,
        },
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
    if "vertex_groups" in want and d.get("vertex_groups") != want["vertex_groups"]:
        problems.append(
            f"vertex groups {d.get('vertex_groups')} != {want['vertex_groups']}")
    if "skinned" in want and d.get("skinned_vertices") != want["skinned"]:
        problems.append(f"skinned vertices {d.get('skinned_vertices')} != {want['skinned']}")
    if "shape_keys" in want:
        got = {k["name"]: k["moved"] for k in d.get("shape_keys", [])}
        for key, moved in want["shape_keys"].items():
            if key not in got:
                problems.append(f"shape key '{key}' missing")
            elif got[key] != moved:
                problems.append(f"shape key '{key}' moves {got[key]} verts, expected {moved}")
        for key in got:
            if key not in want["shape_keys"]:
                problems.append(f"unexpected shape key '{key}'")

    if problems:
        print(f"FAIL {name}: " + "; ".join(problems))
        failures += 1
    else:
        extra = ""
        if d.get("bones"):
            extra = f", {d['bones']} bones, {d['skinned_vertices']} skinned"
        if d.get("shape_keys"):
            extra += f", {len(d['shape_keys'])} shape keys"
        print(f"ok   {name}: {d['vertices']} verts, {d['triangles']} tris, "
              f"tallest {d['tallest_extent']}, {d['uv_layers']} uv layer(s){extra}")

missing = set(EXPECT) - seen
for m in sorted(missing):
    print(f"FAIL {m}: never reported")
    failures += 1

agreed = len(EXPECT) - failures
print(f"\n{agreed}/{len(EXPECT)} exports agree with Blender")
sys.exit(1 if failures else 0)
