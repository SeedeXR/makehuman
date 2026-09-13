#!/usr/bin/env python3
"""Emit data/rigs/makehuman1_retarget.json: MakeHuman 1.x bone -> superset bone.

The shipped `data/animations/**/*.bvh` are MakeHuman 1.x BVH exports. They name
75 joints and match **0** bones of either rig this port ships, so they pose
nothing at all -- `mh::rig::bonesDrivenBy` reports "drives 0 of 179 bones".
This table is what makes them usable.

EVERY target is the one `data/rigs/mixamo_retarget.json` already uses for the
same anatomical role. That table was verified injective and descent-correct, so
reusing its choices means this one inherits a correspondence somebody checked
rather than one invented here: MakeHuman 1.x `Clavicle_L` and Mixamo
`LeftShoulder` are the same bone, so both map to `clavicle.L`; `UpArm_L` and
`LeftArm` both map to `shoulder01.L`; `Toe_L` and `LeftToeBase` both map to
`ball.L`.

The 16 `__`-prefixed joints are NOT mapped, deliberately. They are the
zero-length connectors the MakeHuman 1.x exporter inserts, they carry rotation
channels, and there is no verified counterpart for them: the reference tree
defines no skeleton with these names (checked), so any target would be a guess.
Mapping a metacarpal wrongly twists a finger; leaving it unmapped loses a joint
that barely moves. The conservative choice is recorded here so the next person
knows it was a decision and not an oversight -- if a render shows the hands or
hips missing motion, this is the line to revisit.
"""
import json
import pathlib

ROOT = pathlib.Path(__file__).resolve().parent.parent
OUT = ROOT / "data" / "rigs" / "makehuman1_retarget.json"

SPINE = {"Root": "root", "Hips": "hips", "Spine1": "spine03", "Spine2": "spine02",
         "Spine3": "spine01", "Neck": "neck01", "Head": "head"}
HEAD = {"Jaw": "jaw", "Tongue1": "tongue01", "Tongue2": "tongue02", "Tongue3": "tongue03",
        "Eye_L": "eye.L", "Eye_R": "eye.R"}
# Thumb..Pinky are finger1..finger5, exactly as the Mixamo table has them.
FINGERS = {"Thumb": 1, "Index": 2, "Middle": 3, "Ring": 4, "Pinky": 5}


def mapping() -> dict[str, str]:
    out = dict(SPINE)
    out.update(HEAD)
    for side in ("L", "R"):
        out[f"Clavicle_{side}"] = f"clavicle.{side}"
        out[f"UpArm_{side}"] = f"shoulder01.{side}"
        out[f"LoArm_{side}"] = f"lowerarm01.{side}"
        out[f"Hand_{side}"] = f"wrist.{side}"
        for name, index in FINGERS.items():
            for segment in (1, 2, 3):
                out[f"{name}{segment}_{side}"] = f"finger{index}-{segment}.{side}"
        out[f"UpLeg_{side}"] = f"upperleg01.{side}"
        out[f"LoLeg_{side}"] = f"lowerleg01.{side}"
        out[f"Foot_{side}"] = f"foot.{side}"
        out[f"Toe_{side}"] = f"ball.{side}"
    return out


def main() -> int:
    m = mapping()
    doc = {
        "_provenance": {
            "generated_by": "tools/makehuman1_mapping.py",
            "rig": "data/rigs/mixamo_superset.mhskel",
            "source": "data/animations/walks/walk1.bvh (MakeHuman 1.x BVH export)",
            "makehuman1_bones": len(m),
            "note": ("MakeHuman 1.x bone -> superset bone. Targets reuse the roles "
                     "data/rigs/mixamo_retarget.json already verified. The 16 `__` "
                     "connector joints are deliberately unmapped; see the module "
                     "docstring."),
        },
        "mapping": dict(sorted(m.items())),
    }
    OUT.write_text(json.dumps(doc, indent=2) + "\n")
    print(f"{len(m)} pairs -> {OUT}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
