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

    problems = [f"{k} {d.get(k)!r} != {v!r}" for k, v in want.items() if d.get(k) != v]
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
