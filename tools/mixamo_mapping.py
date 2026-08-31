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
import pathlib
import re
import sys

REPO = pathlib.Path(__file__).resolve().parent.parent
SKELETON = REPO / "data" / "rigs" / "default.mhskel"
MIXAMO_DOC = REPO / "docs" / "rig" / "mixamo_bone_order.md"

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
    MAPPING[f"{side}Arm"] = f"upperarm01{suffix}"
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
    print("\nmapping is consistent: every Mixamo parent relationship survives it")
    return 0


if __name__ == "__main__":
    sys.exit(main())
