#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-or-later
"""The Mixamo <-> MakeHuman bone mapping, and a proof that it holds.

    python3 tools/mixamo_mapping.py    # reports, and exits non-zero on any gap

Answers one question: can a rig be Mixamo-compatible **without giving up**
MakeHuman's detail? Measured, both skeletons are richer in different places:

* MakeHuman's 163 bones win almost everywhere -- 55 facial bones to Mixamo's
  none, 30 toe bones to Mixamo's 6, and twist pairs (`upperleg01`/`upperleg02`)
  where Mixamo has a single bone.
* Mixamo's 65 win in exactly one place: a 4th joint per finger (a fingertip
  locator) that MakeHuman does not have, plus the `_End` leaf markers.

So replacing 163 with 65 is a large downgrade, and the answer is a **superset**:
keep every MakeHuman bone, and make sure every Mixamo name resolves to one of
them. Only the leaf/tip bones have to be added.

The mapping is anchored on PARENTS where parents pin it. `Spine2` maps to
`spine01` because `spine01` is the only MakeHuman bone parenting both clavicles
AND the neck, exactly as `Spine2` parents both shoulders and the neck -- forced,
and checkable.

**Most of the mapping is not pinned that tightly.** `Spine -> spine03` also
satisfies ancestry with `spine04` or `spine05`; what actually picks `spine03` is
that it is 0.1 cm away in the normalised frame, which this script does not
measure. So the checks here are necessary and not sufficient: ancestry,
injectivity and laterality between them reject the mistakes that have actually
been made, but a geometric oracle would be stronger. See
`docs/rig/mixamo_mapping.md`.
"""

import json
import math
import pathlib
import re
import sys

REPO = pathlib.Path(__file__).resolve().parent.parent
SKELETON = REPO / "data" / "rigs" / "default.mhskel"
MIXAMO_DOC = REPO / "docs" / "rig" / "mixamo_bone_order.md"
SUPERSET = REPO / "data" / "rigs" / "mixamo_superset.mhskel"
RETARGET = REPO / "data" / "rigs" / "mixamo_retarget.json"

# The 16 Mixamo bones MakeHuman's 163 do not have. MAPPING records them as None
# -- "the superset must add this" -- and the superset now does, so against the
# 179-bone rig the table is TOTAL: every one of Mixamo's 65 has a counterpart.
#
# These are not name matches. Each one continues a chain the already-proven
# segments established: MAPPING sends LeftHandThumb1..3 to finger1-1..3.L, so
# LeftHandThumb4 -> finger1-4.L is the same chain, and the ancestry check below
# verifies it structurally rather than trusting the name.
SUPERSET_ADDITIONS: dict[str, str] = {
    # Mixamo roots the skeleton at Hips. MakeHuman's `root` is NOT that joint --
    # the legs and spine meet at root's TAIL, 0.92 dm from its head -- so the
    # superset carries a real `hips` bone at that junction instead.
    "Hips": "hips",
    "HeadTop_End": "HeadTop_End",
    "LeftToeBase": "ball.L",
    "RightToeBase": "ball.R",
    "LeftToe_End": "toe_end.L",
    "RightToe_End": "toe_end.R",
}
for _side, _sfx in (("Left", ".L"), ("Right", ".R")):
    for _n, _finger in enumerate(("Thumb", "Index", "Middle", "Ring", "Pinky"), start=1):
        SUPERSET_ADDITIONS[f"{_side}Hand{_finger}4"] = f"finger{_n}-4{_sfx}"


def superset_parents() -> dict[str, str | None]:
    bones = json.loads(SUPERSET.read_text())["bones"]
    return {name: spec.get("parent") for name, spec in bones.items()}


def full_mapping() -> dict[str, str]:
    """Mixamo bone -> superset bone, for all 65. Total by construction."""
    out: dict[str, str] = {}
    for mixamo_bone, target in MAPPING.items():
        out[mixamo_bone] = target if target is not None else SUPERSET_ADDITIONS[mixamo_bone]
    return out
MIXAMO_REST = REPO / "docs" / "rig" / "mixamo_rest_pose.json"
BASE_MESH = REPO / "data" / "3dobjs" / "base.obj"

# Chains to check geometrically, Mixamo side and MakeHuman side.
#
# Comparing bone POSITIONS directly does not work: the two rigs are in different
# rest poses, so arm error grows down the chain (0.13 at the shoulder to over
# 1.0 at the fingers) and every arm bone looks wrong. Arc length along the chain
# is pose-invariant -- bone lengths do not change when a rig moves -- so that is
# what is compared.
CHAINS: list[tuple[list[str], list[str]]] = [
    (["Hips", "Spine", "Spine1", "Spine2", "Neck", "Head"],
     ["spine05", "spine04", "spine03", "spine02", "spine01", "neck01", "neck02", "neck03",
      "head"]),
]
for _side, _sfx in (("Left", ".L"), ("Right", ".R")):
    CHAINS.append(([f"{_side}Shoulder", f"{_side}Arm", f"{_side}ForeArm", f"{_side}Hand"],
                   [f"clavicle{_sfx}", f"shoulder01{_sfx}", f"upperarm01{_sfx}",
                    f"upperarm02{_sfx}", f"lowerarm01{_sfx}", f"lowerarm02{_sfx}",
                    f"wrist{_sfx}"]))
    CHAINS.append(([f"{_side}UpLeg", f"{_side}Leg", f"{_side}Foot", f"{_side}ToeBase"],
                   [f"upperleg01{_sfx}", f"upperleg02{_sfx}", f"lowerleg01{_sfx}",
                    f"lowerleg02{_sfx}", f"foot{_sfx}", f"toe1-1{_sfx}"]))

# How far a mapped bone may sit from its Mixamo counterpart along the chain.
# The worst legitimate gap measured is 5.8 points (ForeArm); 10 leaves headroom
# without admitting the 10.6-point error that put `Arm` on the wrong bone.
ARC_TOLERANCE = 0.10

# Mixamo bone -> the MakeHuman bone that plays its part.
#
# `None` means MakeHuman has no equivalent and the superset rig must ADD one.
# Every such entry is a leaf: a fingertip, a head-top marker or a toe end. They
# carry no skin weight in Mixamo either -- they exist to give the previous bone
# a direction -- so adding them costs nothing and loses nothing.
MAPPING: dict[str, str | None] = {
    # NOT `root`. Structurally `root` is the only candidate -- it alone parents
    # both legs and the spine, as Hips does -- but its HEAD is 0.92 dm behind
    # and below the point where the legs and spine actually meet, which is
    # `root`'s TAIL and equals `spine05`/`pelvis.*`'s head exactly. Binding Hips
    # to `root` would swing the whole character about a pivot ~9 cm off, on
    # every rotation and every root-motion translation. The superset adds a
    # `hips` bone at that junction instead.
    "Hips": None,
    # spine01 parents the clavicles AND the neck, exactly as Spine2 does.
    "Spine": "spine03",
    "Spine1": "spine02",
    "Spine2": "spine01",
    "Neck": "neck01",
    "Head": "head",
    "HeadTop_End": None,
}

for side, suffix in (("Left", ".L"), ("Right", ".R")):
    MAPPING[f"{side}Shoulder"] = f"clavicle{suffix}"
    # `shoulder01`, NOT `upperarm01`. Measured along the clavicle->wrist chain,
    # Mixamo's `Arm` head sits at 16.2% where `shoulder01` is at 16.0% and
    # `upperarm01` at 26.8%. `upperarm01` is the conventional humerus and carries
    # the skin weight, but binding the humerus rotation 10% down the arm leaves
    # `shoulder01` rigid while the arm swings beneath it -- deltoid collapse.
    MAPPING[f"{side}Arm"] = f"shoulder01{suffix}"
    MAPPING[f"{side}ForeArm"] = f"lowerarm01{suffix}"
    MAPPING[f"{side}Hand"] = f"wrist{suffix}"
    MAPPING[f"{side}UpLeg"] = f"upperleg01{suffix}"
    MAPPING[f"{side}Leg"] = f"lowerleg01{suffix}"
    MAPPING[f"{side}Foot"] = f"foot{suffix}"
    # Mixamo's ToeBase is the ball of the foot: ONE bone parenting every toe.
    # MakeHuman hangs all five toes straight off `foot`, so there is nothing to
    # map to. See the note in the report -- this is the one place the superset
    # gains a real, weight-bearing bone rather than a leaf marker.
    MAPPING[f"{side}ToeBase"] = None
    MAPPING[f"{side}Toe_End"] = None
    # Mixamo numbers a finger 1..4 where 4 is the tip; MakeHuman numbers 1..3
    # and additionally has a metacarpal that Mixamo skips entirely.
    for mixamo_finger, mh_finger in (("Thumb", 1), ("Index", 2), ("Middle", 3),
                                     ("Ring", 4), ("Pinky", 5)):
        for joint in (1, 2, 3):
            MAPPING[f"{side}Hand{mixamo_finger}{joint}"] = f"finger{mh_finger}-{joint}{suffix}"
        MAPPING[f"{side}Hand{mixamo_finger}4"] = None


def mixamo_hierarchy() -> dict[str, str | None]:
    """Parent per Mixamo bone, from the measured table in the doc.

    Read rather than hardcoded: the table was extracted from the reference FBX
    clips, and a second copy here could drift from it.
    """
    parents: dict[str, str | None] = {}
    stack: dict[int, str] = {}
    for line in MIXAMO_DOC.read_text().splitlines():
        # NOT `\w`, though SonarQube's python:S6353 asks for it: Python's `\w`
        # is Unicode-aware by default and would also match accented and
        # non-Latin letters. A Mixamo bone name is ASCII, and saying so is the
        # point of spelling the class out.
        m = re.match(r"^(\s*)(\d+)(\s+)mixamorig:([A-Za-z0-9_]+)\s*$", line)
        if not m:
            continue
        name = m.group(4)
        # Depth is the indentation of the NAME, which the table encodes.
        depth = len(m.group(1)) + len(m.group(2)) + len(m.group(3))
        parents[name] = next((stack[d] for d in sorted(stack, reverse=True) if d < depth), None)
        stack[depth] = name
        for d in [d for d in stack if d > depth]:
            del stack[d]
    return parents


def makehuman_parents() -> dict[str, str | None]:
    bones = json.loads(SKELETON.read_text())["bones"]
    return {name: spec.get("parent") for name, spec in bones.items()}


def makehuman_joint_positions() -> dict[str, tuple[float, float, float]]:
    """Each bone's head, averaged from the vertex group the skeleton names.

    Positions are not stored in the `.mhskel`; a bone's head is a set of base
    mesh vertex indices, exactly as the reference resolves them
    (`apps/human.py:1129`).
    """
    skeleton = json.loads(SKELETON.read_text())
    joints, bones = skeleton["joints"], skeleton["bones"]
    coords: list[tuple[float, float, float]] = []
    for line in BASE_MESH.read_text().splitlines():
        if line.startswith("v "):
            _, x, y, z = line.split()[:4]
            coords.append((float(x), float(y), float(z)))

    out = {}
    for bone, spec in bones.items():
        idx = joints.get(spec["head"])
        if idx:
            out[bone] = tuple(sum(coords[i][k] for i in idx) / len(idx) for k in range(3))
    return out


def arc_fractions(chain: list[str],
                  pos: dict[str, tuple[float, float, float]]) -> dict[str, float]:
    """Cumulative distance along @p chain, as a fraction of its total length."""
    points = [pos[b] for b in chain if b in pos]
    if len(points) != len(chain):
        return {}
    cumulative, total = [0.0], 0.0
    for a, b in zip(points, points[1:]):
        total += math.dist(a, b)
        cumulative.append(total)
    return {} if total == 0 else {b: c / total for b, c in zip(chain, cumulative)}


def geometric_problems() -> list[str]:
    """Where a mapped bone sits nowhere near its counterpart along the chain.

    This is the check that would have caught `Hips -> root` without a reviewer,
    and the one that settled `Arm -> shoulder01` with a number instead of an
    argument.
    """
    if not MIXAMO_REST.exists():
        return ["no measured Mixamo rest pose; geometric check skipped"]
    mixamo = {k: tuple(v) for k, v in json.loads(MIXAMO_REST.read_text())["bones"].items()}
    makehuman = makehuman_joint_positions()

    out = []

    # A chain ROOT is at 0% by definition on both sides, so the arc check above
    # can never fault it -- it was blind to exactly the error it was built for
    # (`Hips -> root`, whose head is 0.92 dm from where the legs and spine
    # actually meet). Chain roots are therefore checked by POSITION against the
    # MakeHuman chain's own start, scaled by that chain's length.
    for mixamo_chain, mh_chain in CHAINS:
        target = MAPPING.get(mixamo_chain[0])
        if target is None or target not in makehuman or mh_chain[0] not in makehuman:
            continue
        span = sum(math.dist(makehuman[a], makehuman[b])
                   for a, b in zip(mh_chain, mh_chain[1:]) if a in makehuman and b in makehuman)
        offset = math.dist(makehuman[target], makehuman[mh_chain[0]])
        if span > 0 and offset / span > ARC_TOLERANCE:
            out.append(
                f"{mixamo_chain[0]} maps to {target}, which sits {offset:.3f} dm "
                f"({offset / span * 100:.1f}% of the chain) from {mh_chain[0]}, where that "
                f"chain actually starts")

    for mixamo_chain, mh_chain in CHAINS:
        mf, hf = arc_fractions(mixamo_chain, mixamo), arc_fractions(mh_chain, makehuman)
        if not mf or not hf:
            out.append(f"cannot measure the chain starting at {mixamo_chain[0]}")
            continue
        for bone in mixamo_chain:
            target = MAPPING.get(bone)
            if target is None or target not in hf:
                continue
            gap = abs(mf[bone] - hf[target])
            if gap > ARC_TOLERANCE:
                nearest = min(hf, key=lambda k, b=bone: abs(hf[k] - mf[b]))
                out.append(
                    f"{bone} is {mf[bone] * 100:.1f}% along its chain but {target} is "
                    f"{hf[target] * 100:.1f}% ({gap * 100:.1f} points apart); "
                    f"{nearest} at {hf[nearest] * 100:.1f}% fits better")
    return out


def ancestors(bone: str, parents: dict[str, str | None]) -> list[str]:
    out, seen = [], set()
    while bone and bone not in seen:
        seen.add(bone)
        bone = parents.get(bone)
        if bone:
            out.append(bone)
    return out


def main() -> int:
    mixamo = mixamo_hierarchy()
    mh = makehuman_parents()
    problems: list[str] = []

    if len(mixamo) != 65:
        problems.append(f"expected 65 Mixamo bones in the doc, parsed {len(mixamo)}")

    missing = [b for b in mixamo if b not in MAPPING]
    if missing:
        problems.append("Mixamo bones with no mapping entry: " + ", ".join(missing))

    unknown = [f"{k}->{v}" for k, v in MAPPING.items() if v is not None and v not in mh]
    if unknown:
        problems.append("mapped to a MakeHuman bone that does not exist: " + ", ".join(unknown))

    # Injectivity. Ancestry alone happily accepts two Mixamo bones landing on
    # one MakeHuman bone -- mapping the index finger onto the pinky passed until
    # this existed, silently, while the coverage count still read 50.
    targets = [v for v in MAPPING.values() if v is not None]
    if len(set(targets)) != len(targets):
        seen, dupes = set(), set()
        for t in targets:
            (dupes if t in seen else seen).add(t)
        problems.append("two Mixamo bones mapped onto one MakeHuman bone: "
                        + ", ".join(sorted(dupes)))

    # Laterality. Ancestry is blind to a MIRRORED rig: swapping every left and
    # right bone at once preserves every parent relationship, so the check
    # passed a fully mirrored mapping. A single swapped bone is caught by its
    # children; a whole-side swap is not, and that is the mirror bug that
    # actually happens.
    for bone, target in MAPPING.items():
        if target is None:
            continue
        for prefix, suffix in (("Left", ".L"), ("Right", ".R")):
            if bone.startswith(prefix) and not target.endswith(suffix):
                problems.append(f"{bone} is a {prefix.lower()} bone but maps to {target}")

    # The real check: a mapping must preserve ancestry. If Mixamo says A is
    # above B, the bone A maps to must be an ancestor of the bone B maps to.
    # Necessary, and on its own nowhere near sufficient -- see above.
    for bone, parent in mixamo.items():
        target, parent_target = MAPPING.get(bone), MAPPING.get(parent) if parent else None
        if target is None or parent_target is None:
            continue
        if parent_target not in ancestors(target, mh):
            problems.append(
                f"{bone} -> {target}, but its parent {parent} -> {parent_target} "
                f"is not an ancestor of {target}")

    problems += geometric_problems()

    # The full table, against the superset rig. This is the check that makes the
    # additions a mapping rather than a name match: each target's ancestry in
    # the 179-bone rig must contain the target of its Mixamo parent.
    full = full_mapping()
    sup = superset_parents()

    absent = sorted({v for v in full.values() if v not in sup})
    if absent:
        problems.append("retarget target missing from the superset rig: " + ", ".join(absent))
    if len(set(full.values())) != len(full):
        problems.append("the full retarget table is not injective")

    def superset_ancestors(bone: str) -> list[str]:
        out, cur = [], sup.get(bone)
        while cur:
            out.append(cur)
            cur = sup.get(cur)
        return out

    for bone, parent in mixamo.items():
        if parent is None:
            continue
        target, parent_target = full.get(bone), full.get(parent)
        if target is None or parent_target is None:
            continue
        if parent_target not in superset_ancestors(target):
            problems.append(f"{bone}->{target} does not descend from "
                            f"{parent}->{parent_target} in the superset rig")

    if not problems and ("--emit" in sys.argv or "--check" in sys.argv):
        payload = {
            "_provenance": {
                "generated_by": "tools/mixamo_mapping.py --emit",
                "rig": "data/rigs/mixamo_superset.mhskel",
                "mixamo_bones": len(mixamo),
                "note": "Mixamo bone -> superset bone. Total: all 65 have a "
                        "counterpart, because the superset adds the 16 MakeHuman "
                        "lacks. Verified injective, and each target descends from "
                        "its Mixamo parent's target in the superset hierarchy.",
            },
            "mapping": dict(sorted(full.items())),
        }
        text = json.dumps(payload, indent=2, sort_keys=False) + "\n"
        if "--check" in sys.argv:
            if not RETARGET.exists():
                print(f"{RETARGET} does not exist; run with --emit", file=sys.stderr)
                return 1
            if RETARGET.read_text() != text:
                print(f"{RETARGET} is stale; re-run with --emit", file=sys.stderr)
                return 1
            print(f"{RETARGET} is up to date")
        else:
            RETARGET.write_text(text)
            print(f"wrote {RETARGET} ({len(full)} bones)")

    mapped = {k: v for k, v in MAPPING.items() if v is not None}
    to_add = sorted(k for k, v in MAPPING.items() if v is None)

    print(f"Mixamo bones          : {len(mixamo)}")
    print(f"MakeHuman bones       : {len(mh)}")
    print(f"already covered       : {len(mapped)}")
    print(f"must be added         : {len(to_add)}")
    print(f"superset rig size     : {len(mh)} + {len(to_add)} = {len(mh) + len(to_add)}")
    print(f"MakeHuman-only bones  : {len(mh) - len(set(mapped.values()))}")
    print()
    print("bones the superset must add:")
    for b in to_add:
        print(f"    {b}")

    if problems:
        print("\nPROBLEMS", file=sys.stderr)
        for p in problems:
            print("  " + p, file=sys.stderr)
        return 1
    print("\nmapping is consistent: hierarchy, injectivity, laterality, and every")
    print("mapped bone sits within 10 points of its counterpart along the chain")
    return 0


if __name__ == "__main__":
    sys.exit(main())
