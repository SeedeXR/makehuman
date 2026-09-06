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
    # A POSED export, written by the application: T-pose, geometry baked and the
    # skeleton re-fitted so the posed state IS the bind pose. Blender applies
    # the armature itself and must move nothing -- checked generically by the
    # armature_shift rule below, which is the real assertion here.
    #
    # `tallest` is the arm SPAN (1.6863), not the height (1.6630): a T-pose is
    # wider than it is tall, and `tallest_extent` is max(extents). Reading it as
    # a height here would look like a 1.4% error that is not there.
    #
    # Counts are the compacted, masked export the app writes -- 15,593 vertices
    # across body + eyes, of which 14,517 are skinned -- not the fixture's raw
    # 21,833.
    # A LIVE RIG (owner decision, 2026-09-05): the file ships REST geometry with
    # a POSED armature, so Blender computes the deformation itself.
    #
    # `tallest` is the REST mesh, 1.6594 -- arms down. `evaluated` is what
    # Blender's own skinning produces from it, and it is our CPU LBS answer to
    # four decimals: we export the same T-pose baked to OBJ as
    # 16.8628 x 3.0088 x 16.6301 dm. That agreement is a third party
    # independently reproducing our skinning, which was impossible while we
    # baked -- the armature was a no-op by construction.
    #
    # 179 bones, not 163: the app defaults to mixamo_superset. The fixture's own
    # rigged.glb still uses the reference's 163-bone rig, so both numbers appear
    # in this file on purpose.
    "posed.glb": {
        "vertices": 15593, "triangles": 28796, "tallest": 1.659377, "uv_layers": 1,
        "bones": 179, "armatures": 1, "skinned": 14517, "vertex_groups": 179,
        "live_rig": True, "evaluated": [1.6863, 0.3009, 1.663],
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
    # The same set again through UsdSkel BlendShape, with NO skeleton --
    # the case the application cannot produce, because it always builds a rig.
    # Key names carry the ONE substitution USD forces: a prim name is an
    # identifier, so `eye-left-closure` becomes `eye_left_closure`. Derived
    # rather than retyped, so the counts cannot drift from the other two and
    # the renaming rule is stated in code instead of buried in 34 literals.
    "expressions.usda": dict(
        _EXPRESSION_MESH,
        shape_keys={k.replace("-", "_"): v for k, v in _EXPRESSION_KEYS.items()},
    ),
    "morphed.glb": {
        "vertices": 21833, "triangles": 36972, "tallest": 1.69455, "uv_layers": 1,
        "shape_keys": {
            "head-oval": 2200,
            "head-trans-backward": 5865,
            "nose-base-up": 294,
        },
    },
}

#: Exports of the SAME base mesh. Their UVs must survive every format
#: identically, and that is a real question rather than a tautology: glTF's UV
#: origin is the image's UPPER-left while OBJ, USD, FBX and Blender all use the
#: LOWER-left, so GltfWriter flips V (GltfWriter.cpp:300-302) and the others
#: must not. A writer on the wrong side of that ships every texture mirrored,
#: and no vertex/triangle/bounds check can see it -- the geometry is identical
#: either way.
#:
#: Anchored on the highest vertex in world space, not a vertex index: the
#: importers renumber, but the top of the head is the top of the head.
_UV_FORMATS = ("base.obj", "base.glb", "base.fbx", "base.usda")

failures = 0
seen = set()
uv_top = {}
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

    top = (d.get("uv_extremes") or {}).get("highest")
    if top:
        uv_top[name] = top["uv"]

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
    # Applying the armature must not move the mesh -- posed or not.
    #
    # GltfWriter derives BOTH the joint node transforms and the inverse-bind
    # matrices from one scaled-global array (GltfWriter.cpp:330-374) precisely so
    # they cannot disagree, which makes the armature a no-op by construction.
    # Measured 9e-06 on posed.glb and rigged.glb. Break that single source --
    # unscaled IBMs against scaled nodes -- and the shift is 8.38, so this has
    # teeth rather than passing by default.
    #
    # A file that fails here is double-deformed in every DCC while our own tests,
    # which never apply an armature, all stay green.
    if want.get("live_rig"):
        # A LIVE RIG must deform: the file ships REST geometry and the consumer
        # applies the pose. A shift of ~0 here means the armature is doing
        # nothing and the file is a rest-pose statue -- which is exactly what
        # assimp's FBX writer produces, and why FBX still bakes.
        if d.get("armature_shift", 0.0) < 1e-3:
            problems.append("live rig does not deform: applying the armature moves nothing")
        # And it must deform to the RIGHT place. These extents are our own CPU
        # LBS answer, so agreement means Blender independently reproduced it.
        if "evaluated" in want and d.get("evaluated_extents") != want["evaluated"]:
            problems.append(
                f"evaluated extents {d.get('evaluated_extents')} != {want['evaluated']} "
                f"-- Blender's skinning disagrees with ours")
    elif "armature_shift" in d and d["armature_shift"] > 1e-4:
        # Everything else BAKES, so its armature must be a no-op: a non-zero
        # shift means the joint nodes and the inverse-bind matrices disagree and
        # the file is double-deformed in every DCC.
        problems.append(
            f"applying the armature moves the mesh by {d['armature_shift']} "
            f"-- the joint node transforms and the inverse-bind matrices disagree")

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

# The cross-format UV comparison, once every file has reported.
uv_failed = False
present = [n for n in _UV_FORMATS if n in uv_top]
if len(present) < 2:
    print(f"skip UV convention: only {len(present)} of the base exports reported a UV")
else:
    reference = uv_top[present[0]]
    disagree = [n for n in present[1:]
                if max(abs(a - b) for a, b in zip(uv_top[n], reference)) > 1e-4]
    if disagree:
        print(f"FAIL UV convention: {present[0]} puts the top vertex at {reference}, but "
              + "; ".join(f"{n} at {uv_top[n]}" for n in disagree)
              + " -- one of these writers is on the wrong side of the V origin")
        # Counted apart from `failures`: this is a disagreement BETWEEN exports,
        # not one export failing its own expectation, and folding it in would
        # make the tally below claim an export disagreed when none did.
        uv_failed = True
    else:
        print(f"ok   UV convention: {len(present)} formats agree the top vertex is at "
              f"{reference}")

missing = set(EXPECT) - seen
for m in sorted(missing):
    print(f"FAIL {m}: never reported")
    failures += 1

agreed = len(EXPECT) - failures
print(f"\n{agreed}/{len(EXPECT)} exports agree with Blender")
sys.exit(1 if (failures or uv_failed) else 0)
