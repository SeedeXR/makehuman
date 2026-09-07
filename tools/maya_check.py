#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Checks Maya's report against what the FBX export claims. Exits non-zero on
any disagreement, so this is a gate rather than something to read."""
import json
import sys

# Measured 2026-09-07 against `makehuman --rig mixamo_superset --pose tpose`.
#
# `live_rig: False` is not an aspiration recorded as a pass -- it is the CURRENT
# behaviour, pinned so that a change is noticed. Our .glb ships rest geometry
# with a posed armature; the .fbx, written through assimp, ships the POSED
# geometry with a rig that works but starts from the wrong place. Autodesk's own
# importer says so, which is the strongest statement available about an FBX.
#
# If an assimp upgrade ever starts preserving the pose, this fails and the
# expectation gets flipped -- which is the point of writing it down.
EXPECT = {
    # Written by OUR FBX writer. The counts are the compacted base mesh with the
    # static face mask applied, triangulated: what mh_fbx_probe writes.
    #
    # `meshes: 1` is the whole point. It read 0 for four separate reasons in
    # turn -- a missing NULL record, an unvalidated footer id, an empty
    # Properties70 on the Model, and a truncated property NAME -- and Blender
    # opened every one of those files without complaint.
    "ours.fbx": {
        "meshes": 1,
        "vertices": 21833,
        "uv_sets": 1,
        # Checked by NAME. An empty Maya scene already ships lambert1 and
        # friends, and which defaults exist depends on the version, so a count
        # is a number nobody can verify -- but "our material is there" is.
        "material_names contains": "Skin",
        # The RELATIVE path, as the material names it. An absolute path from
        # the build machine would be a broken link everywhere else.
        "file_textures contains": "textures/skin/african_deep.png",
    },
    # OUR writer with a rig. `live_rig: True` is the statement the whole FBX
    # effort exists to make, and it is the one assimp's writer cannot: the file
    # ships REST geometry and Maya's own skinning moves it (169.455 -> 249.455
    # in y), rather than shipping geometry with the pose already baked in.
    "rigged.fbx": {
        "meshes": 1,
        "vertices": 21833,
        "joints": 2,
        "skin_clusters": 1,
        "live_rig": True,
        "deforms_when_posed": True,
        "uv_sets": 1,
        "material_names contains": "Skin",
    },
    # The APPLICATION's own FBX export, through our writer. This is the one a
    # user gets, and the number that matters is `evaluated`: Maya's own skinning
    # of our rest geometry must reproduce OUR CPU LBS answer, which the Blender
    # harness pins for the glTF live rig as 1.6863 x 0.3009 x 1.663 m.
    #
    # Getting `Transform` wrong -- identity instead of the inverse bind --
    # produced 247 x 334 x 269 here while every other field stayed correct.
    "app_posed.fbx": {
        "joints": 179,
        "skin_clusters": 1,
        "live_rig": True,
        "deformed_extent": [168.6275, 166.3017, 30.0878],
    },
    # ... and with NO pose the deformation must be exactly the identity. An
    # unposed rig that moves anything is the same bug, showing up where it is
    # easiest to see.
    "app_unposed.fbx": {
        "joints": 179,
        "live_rig": False,
    },
    # OUR writer with blend shapes. The names come back ESCAPED: Maya cannot
    # put a hyphen in a node name and encodes it as `FBXASC045`, so
    # `mouth-open` arrives as `mouthFBXASC045open`. The names survive; they are
    # just spelled Maya's way, and asserting the raw name would fail on a file
    # that is perfectly correct.
    "morphed.fbx": {
        "blend_shapes": [
            "headFBXASC045oval",
            "mouthFBXASC045open",
            "noseFBXASC045baseFBXASC045up",
        ],
        # 4 meshes, not 1: Maya imports each blend-shape target as its own mesh,
        # and does the same to its OWN files -- a cube with one target reports
        # 2. Ours is the base plus three targets.
        "meshes": 4,
    },
}

failures = 0
seen = set()
for line in sys.stdin:
    line = line.strip()
    if not line.startswith("MAYA_VALIDATE:"):
        continue
    d = json.loads(line[len("MAYA_VALIDATE:"):])
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
    for key, expected in want.items():
        # `<field> contains <value>` for the list-valued reports, where the
        # scene's own defaults sit alongside what we wrote.
        if key.endswith(" contains"):
            field = key[: -len(" contains")]
            if expected not in (d.get(field) or []):
                problems.append(f"{field} {d.get(field)!r} does not contain {expected!r}")
        elif d.get(key) != expected:
            problems.append(f"{key} {d.get(key)!r} != {expected!r}")
    if problems:
        print(f"FAIL {name}: " + "; ".join(problems))
        failures += 1
    else:
        print(f"ok   {name}: {d['vertices']} verts, {d['joints']} joints, "
              f"live_rig={d['live_rig']}, deforms={d['deforms_when_posed']}")

for missing in sorted(set(EXPECT) - seen):
    print(f"FAIL {missing}: never reported")
    failures += 1

print(f"\n{len(EXPECT) - failures}/{len(EXPECT)} FBX exports agree with Maya")
sys.exit(1 if failures else 0)
