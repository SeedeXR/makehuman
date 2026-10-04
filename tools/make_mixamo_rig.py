#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-or-later
"""Generate the exact Mixamo skeleton, fitted to this project's base mesh.

    python3 tools/make_mixamo_rig.py           # write it
    python3 tools/make_mixamo_rig.py --check   # fail if the files are stale

WHY A SECOND MIXAMO RIG, WHEN THERE IS ALREADY A SUPERSET. They answer
different questions. `mixamo_superset` is OURS: 179 bones, every MakeHuman bone
plus the 16 Mixamo needs, so nothing is lost and our own animation retargets
work. This one is THEIRS: the 65 bones Mixamo actually uses, named exactly as
Mixamo names them, so Mixamo can MAP it.

The difference is not cosmetic, and it is why `--rig-names mixamo` on the
superset is not a substitute. That flag renames the bones that have a Mixamo
counterpart -- `Hips`, `Spine`, `LeftUpLeg` -- and leaves the other 114 under
their native names, because Mixamo has nowhere to put `spine05`, `pelvis.L`,
`upperleg02.L` or any of the face bones. Mixamo is then handed a skeleton it
can only half-map, and answers "Sorry, unable to map your existing skeleton."

WHERE THE HIERARCHY COMES FROM. `mixamo-asset/X Bot.fbx`, downloaded from
Mixamo by the owner: 65 LimbNodes under a single `mixamorig:Hips` root. That
file is an ORACLE in the sense LICENSING.md 5.2 already uses for Blender, Maya
and the FBX SDK -- read, never linked, never redistributed -- and it is
gitignored, so it cannot be read at build time. The facts it yielded are
therefore BAKED IN below rather than parsed. Bone names and a parent-child
order are the interface, not the asset: no geometry, no weights and no
animation from X Bot travels into this repository.

WHERE THE JOINT POSITIONS COME FROM. Not from X Bot -- its body is not ours.
Each bone takes the head and tail of its superset counterpart, which are named
anchors in the base mesh's helper geometry, so this rig follows the character
the way every other rig here does.
"""

# The hierarchy, read from X Bot.fbx and recorded here. See the note above.
MIXAMO_PARENT = {
    'Hips'              : None,
    'LeftUpLeg'         : 'Hips',
    'LeftLeg'           : 'LeftUpLeg',
    'LeftFoot'          : 'LeftLeg',
    'LeftToeBase'       : 'LeftFoot',
    'LeftToe_End'       : 'LeftToeBase',
    'RightUpLeg'        : 'Hips',
    'RightLeg'          : 'RightUpLeg',
    'RightFoot'         : 'RightLeg',
    'RightToeBase'      : 'RightFoot',
    'RightToe_End'      : 'RightToeBase',
    'Spine'             : 'Hips',
    'Spine1'            : 'Spine',
    'Spine2'            : 'Spine1',
    'LeftShoulder'      : 'Spine2',
    'LeftArm'           : 'LeftShoulder',
    'LeftForeArm'       : 'LeftArm',
    'LeftHand'          : 'LeftForeArm',
    'LeftHandIndex1'    : 'LeftHand',
    'LeftHandIndex2'    : 'LeftHandIndex1',
    'LeftHandIndex3'    : 'LeftHandIndex2',
    'LeftHandIndex4'    : 'LeftHandIndex3',
    'LeftHandMiddle1'   : 'LeftHand',
    'LeftHandMiddle2'   : 'LeftHandMiddle1',
    'LeftHandMiddle3'   : 'LeftHandMiddle2',
    'LeftHandMiddle4'   : 'LeftHandMiddle3',
    'LeftHandPinky1'    : 'LeftHand',
    'LeftHandPinky2'    : 'LeftHandPinky1',
    'LeftHandPinky3'    : 'LeftHandPinky2',
    'LeftHandPinky4'    : 'LeftHandPinky3',
    'LeftHandRing1'     : 'LeftHand',
    'LeftHandRing2'     : 'LeftHandRing1',
    'LeftHandRing3'     : 'LeftHandRing2',
    'LeftHandRing4'     : 'LeftHandRing3',
    'LeftHandThumb1'    : 'LeftHand',
    'LeftHandThumb2'    : 'LeftHandThumb1',
    'LeftHandThumb3'    : 'LeftHandThumb2',
    'LeftHandThumb4'    : 'LeftHandThumb3',
    'Neck'              : 'Spine2',
    'Head'              : 'Neck',
    'HeadTop_End'       : 'Head',
    'RightShoulder'     : 'Spine2',
    'RightArm'          : 'RightShoulder',
    'RightForeArm'      : 'RightArm',
    'RightHand'         : 'RightForeArm',
    'RightHandIndex1'   : 'RightHand',
    'RightHandIndex2'   : 'RightHandIndex1',
    'RightHandIndex3'   : 'RightHandIndex2',
    'RightHandIndex4'   : 'RightHandIndex3',
    'RightHandMiddle1'  : 'RightHand',
    'RightHandMiddle2'  : 'RightHandMiddle1',
    'RightHandMiddle3'  : 'RightHandMiddle2',
    'RightHandMiddle4'  : 'RightHandMiddle3',
    'RightHandPinky1'   : 'RightHand',
    'RightHandPinky2'   : 'RightHandPinky1',
    'RightHandPinky3'   : 'RightHandPinky2',
    'RightHandPinky4'   : 'RightHandPinky3',
    'RightHandRing1'    : 'RightHand',
    'RightHandRing2'    : 'RightHandRing1',
    'RightHandRing3'    : 'RightHandRing2',
    'RightHandRing4'    : 'RightHandRing3',
    'RightHandThumb1'   : 'RightHand',
    'RightHandThumb2'   : 'RightHandThumb1',
    'RightHandThumb3'   : 'RightHandThumb2',
    'RightHandThumb4'   : 'RightHandThumb3',
}

# Mixamo bone -> the superset bone it is cut from. Read from
# data/rigs/mixamo_retarget.json at run time so the two cannot drift; this
# comment records only that the file is the source.
import argparse
import json
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
RIGS = ROOT / "data" / "rigs"
# NO `mixamorig:` PREFIX, and that is a decision rather than an omission.
#
# X Bot carries one on every bone, because that is what Mixamo WRITES when it
# hands a rig back. It is not what Mixamo requires on the way IN: a file from
# Blender or Maya arrives with plain `Hips`, `Spine`, `LeftArm`, and Mixamo
# maps those -- it is the ordinary workflow.
#
# Unprefixed is also the only choice that works with the rest of this project.
# `data/rigs/mixamo_retarget.json` is written in plain names, and it is what
# `--rig-names mixamo` uses to rename a .bvh's joints on import. With the
# prefix, `--pose tpose` drove 0 of 65 bones and the app said so: "it names
# none of this skeleton's joints, so the character will stay at rest".
PREFIX = ""


def load(name):
    return json.loads((RIGS / name).read_text(encoding="utf-8"))


def owning_bone(bone, superset_parent, target_of):
    """The Mixamo bone a superset bone's weights belong to.

    Walks UP until it reaches a bone Mixamo has. `spine04` has no Mixamo
    counterpart, so its influence belongs to whichever ancestor does -- losing
    it instead would leave a band of the torso weighted to nothing.
    """
    seen = set()
    while bone is not None and bone not in seen:
        if bone in target_of:
            return target_of[bone]
        seen.add(bone)
        bone = superset_parent.get(bone)
    return None


def build():
    skel = load("mixamo_superset.mhskel")
    weights = load("mixamo_superset_weights.mhw")
    mapping = load("mixamo_retarget.json")["mapping"]

    missing = [m for m in MIXAMO_PARENT if m not in mapping]
    if missing:
        raise SystemExit(f"no superset counterpart for: {missing}")

    target_of = {v: k for k, v in mapping.items()}
    if len(target_of) != len(mapping):
        raise SystemExit("the mapping is not injective; two Mixamo bones share a target")

    # THE BONES KEEP THEIR NATIVE NAMES, and that is the whole trick.
    #
    # The obvious thing is to name them `Hips`, `Spine`, `LeftArm` -- Mixamo's
    # own names -- and it does not work: `data/poses/tpose.bvh` and every
    # shipped animation name NATIVE joints, so a Mixamo-named rig is driven by
    # none of them. Measured, with the app saying so itself: "tpose.bvh drives
    # 0 of 65 bones -- it names none of this skeleton's joints, so the
    # character will stay at rest".
    #
    # Native names here instead, and `--rig-names mixamo` renames them on the
    # way OUT. That machinery already exists and already works; what broke it
    # on the superset was not the renaming but the 114 EXTRA bones, which have
    # no Mixamo counterpart and so survived under native names in a file that
    # was supposed to be Mixamo's. A rig of exactly the 65 has no leftovers:
    # every bone renames, and Mixamo is handed a skeleton it can map whole.
    bones = {}
    for mixamo, parent in MIXAMO_PARENT.items():
        native = mapping[mixamo]
        src = skel["bones"][native]
        bones[native] = {
            "head": src["head"],
            "tail": src["tail"],
            "parent": mapping[parent] if parent else None,
            "reference": None,
            "rotation_plane": src.get("rotation_plane"),
        }

    superset_parent = {b: v.get("parent") for b, v in skel["bones"].items()}
    merged = {}
    for bone, pairs in weights["weights"].items():
        owner = owning_bone(bone, superset_parent, target_of)
        if owner is None:
            continue
        into = merged.setdefault(mapping[owner], {})
        for vertex, w in pairs:
            into[vertex] = into.get(vertex, 0.0) + w

    # Renormalise per VERTEX. Merging several superset bones into one Mixamo
    # bone can only raise a vertex's total, and a vertex whose weights sum to
    # more than 1 is scaled up by its own skinning.
    total = {}
    for pairs in merged.values():
        for v, w in pairs.items():
            total[v] = total.get(v, 0.0) + w
    out_weights = {}
    for bone, pairs in merged.items():
        rows = []
        for v, w in sorted(pairs.items()):
            s = total[v]
            rows.append([v, round(w / s if s > 0 else w, 6)])
        if rows:
            out_weights[bone] = rows

    note = ("Generated by tools/make_mixamo_rig.py -- do not hand-edit. "
            "Bone names and hierarchy follow Mixamo's own X Bot rig; joint "
            "positions and weights are derived from mixamo_superset, so the "
            "skeleton follows this project's base mesh rather than X Bot's.")
    out_skel = {
        "name": "Mixamo",
        "description": ("The 65 bones Mixamo uses, named as Mixamo names them, so Mixamo "
                        "can map an uploaded character instead of refusing it. Use "
                        "mixamo_superset to keep every MakeHuman bone."),
        "version": skel.get("version", 1),
        "tags": ["mixamo", "retarget"],
        "copyright": skel.get("copyright", ""),
        "license": skel.get("license", "CC0"),
        "joints": skel["joints"],
        "planes": skel["planes"],
        "bones": bones,
        "weights_file": "mixamo_weights.mhw",
        "_provenance": note,
    }
    out_mhw = {
        "name": "Mixamo",
        "description": note,
        "version": weights.get("version", 1),
        "copyright": weights.get("copyright", ""),
        "license": weights.get("license", "CC0"),
        "weights": out_weights,
    }
    return out_skel, out_mhw


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--check", action="store_true",
                    help="fail if the committed files differ from a fresh build")
    args = ap.parse_args()

    skel, mhw = build()
    targets = [(RIGS / "mixamo.mhskel", skel), (RIGS / "mixamo_weights.mhw", mhw)]

    if args.check:
        stale = []
        for path, want in targets:
            if not path.exists():
                stale.append(f"{path.name} is missing")
            elif json.loads(path.read_text(encoding="utf-8")) != want:
                stale.append(f"{path.name} differs from a fresh build")
        if stale:
            print("the Mixamo rig is stale:", file=sys.stderr)
            for s in stale:
                print(f"  {s}", file=sys.stderr)
            print("re-run tools/make_mixamo_rig.py", file=sys.stderr)
            return 1
        print(f"mixamo rig up to date: {len(skel['bones'])} bones, "
              f"{len(mhw['weights'])} weighted")
        return 0

    for path, want in targets:
        path.write_text(json.dumps(want, indent=4, sort_keys=True) + "\n", encoding="utf-8")
    print(f"wrote {len(skel['bones'])} bones and {len(mhw['weights'])} weighted groups")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
