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
    "posed.fbx": {
        "joints": 179,
        "skin_clusters": 1,
        # The skin is real: turning a limb joint moves the mesh. Without this,
        # `live_rig: False` could equally mean the weights never arrived.
        "deforms_when_posed": True,
        "live_rig": False,
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
