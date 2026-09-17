#!/usr/bin/env python3
"""Declare the bundled ARKit face units as modifiers and as a slider view.

    python3 tools/make_faceunits.py [--check]

`data/targets/faceunits/*.target` are 52 CC0 morphs contributed by Mika
Suominen to the MakeHuman community asset repository ("Faceunits 01",
files.makehumancommunity.org/functional/faceunits01.zip), each declaring
`"license": "CC0"` in the pack's own metadata. They are ARKit's blendshape
names -- `jawOpen`, `cheekPuff`, `eyeLookDownLeft` -- which is what makes them
worth having: a character morphed with them maps straight onto ARKit, Live Link
and every other consumer of that vocabulary.

The two JSON files are DERIVED from the target filenames rather than written by
hand, for the reason every other generated asset here is: 52 names repeated
across two files in two different shapes is 104 chances to disagree. `--check`
regenerates and fails if the committed files no longer match, so the modifier
list cannot drift from the targets on disk.

A face unit is UNIDIRECTIONAL -- 0 to 1, no negative side -- so each entry is a
bare `{"target": ...}`. `Modifier.cpp:232` turns that into `right = <group>-
<target>` and a modifier named `<group>/<target>`, which is why the target files
live in `data/targets/faceunits/` and the group is `faceunits`.
"""
import argparse
import json
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
TARGETS = ROOT / "data" / "targets" / "faceunits"
MODIFIERS = ROOT / "data" / "modifiers" / "faceunits_modifiers.json"
SLIDERS = ROOT / "data" / "modifiers" / "faceunits_sliders.json"
GROUP = "faceunits"

# THE 52 NAMES, pinned. ARKit-name fidelity is the entire reason to bundle this
# pack -- a character morphed with them maps straight onto ARKit and Live Link --
# and without this list nothing checks them: rename `jawOpen.target` to
# `jawOpn.target` and the parity test still counts 52, `--check` regenerates the
# JSON from the typo, and every gate stays green.
ARKIT_NAMES = frozenset({
    "browDownLeft", "browDownRight", "browInnerUp", "browOuterUpLeft", "browOuterUpRight",
    "cheekPuff", "cheekSquintLeft", "cheekSquintRight",
    "eyeBlinkLeft", "eyeBlinkRight", "eyeLookDownLeft", "eyeLookDownRight",
    "eyeLookInLeft", "eyeLookInRight", "eyeLookOutLeft", "eyeLookOutRight",
    "eyeLookUpLeft", "eyeLookUpRight", "eyeSquintLeft", "eyeSquintRight",
    "eyeWideLeft", "eyeWideRight",
    "jawForward", "jawLeft", "jawOpen", "jawRight",
    "mouthClose", "mouthDimpleLeft", "mouthDimpleRight", "mouthFrownLeft", "mouthFrownRight",
    "mouthFunnel", "mouthLeft", "mouthLowerDownLeft", "mouthLowerDownRight",
    "mouthPressLeft", "mouthPressRight", "mouthPucker", "mouthRight",
    "mouthRollLower", "mouthRollUpper", "mouthShrugLower", "mouthShrugUpper",
    "mouthSmileLeft", "mouthSmileRight", "mouthStretchLeft", "mouthStretchRight",
    "mouthUpperUpLeft", "mouthUpperUpRight",
    "noseSneerLeft", "noseSneerRight",
    "tongueOut",
})

# Which ARKit region a unit belongs to, by name. Every pattern is anchored and
# they are mutually exclusive, so the order is presentation only -- an earlier
# comment here claimed first-match-wins mattered, which would mislead whoever
# adds a pattern that is NOT anchored.
REGIONS = (
    ("Brows", re.compile(r"^brow", re.I)),
    ("Eyes", re.compile(r"^eye", re.I)),
    ("Cheeks", re.compile(r"^cheek", re.I)),
    ("Nose", re.compile(r"^nose", re.I)),
    ("Jaw", re.compile(r"^jaw", re.I)),
    ("Mouth", re.compile(r"^mouth", re.I)),
    ("Tongue", re.compile(r"^tongue", re.I)),
)


def label(stem: str) -> str:
    """`eyeLookDownLeft` -> `Eye look down left`.

    The names are ARKit's and stay ARKit's in the modifier id -- that is the
    whole point of bundling them. Only the SLIDER caption is humanised, because
    a panel of `eyeLookDownLeft` reads as a dump of an API.
    """
    spaced = re.sub(r"(?<!^)(?=[A-Z])", " ", stem)
    return spaced[0].upper() + spaced[1:].lower()


def region(stem: str) -> str:
    for name, pattern in REGIONS:
        if pattern.match(stem):
            return name
    return "Other"


def derive(stems):
    modifiers = [{"group": GROUP, "modifiers": [{"target": s} for s in stems]}]

    by_region: dict[str, list] = {}
    for s in stems:
        by_region.setdefault(region(s), []).append(
            {"cam": "frontView", "label": label(s), "mod": f"{GROUP}/{s}"})
    # The KEY is what the panel prints as a heading (`SliderLayout.cpp:120` ->
    # `ModifierPanel.cpp:275`), so it is the region's prose name. Keyed on
    # `faceunits/brows` at first, which put seven headings reading like API
    # paths on screen next to the reference's "Neck" and "Torso".
    sliders = {
        "Face units": {
            "cameraView": "faceCamera",
            "modifiers": {r: by_region[r] for r in sorted(by_region)},
        }
    }
    return (json.dumps(modifiers, indent=4, sort_keys=False) + "\n",
            json.dumps(sliders, indent=4, sort_keys=False) + "\n")


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--check", action="store_true",
                    help="fail if the committed JSON no longer matches the targets on disk")
    args = ap.parse_args()

    stems = sorted(p.stem for p in TARGETS.glob("*.target"))
    if not stems:
        print(f"no .target files in {TARGETS}", file=sys.stderr)
        return 1
    if set(stems) != ARKIT_NAMES:
        for n in sorted(set(stems) - ARKIT_NAMES):
            print(f"{n} is not an ARKit blendshape name", file=sys.stderr)
        for n in sorted(ARKIT_NAMES - set(stems)):
            print(f"{n} is missing from data/targets/faceunits", file=sys.stderr)
        return 1

    mods, slid = derive(stems)
    wanted = {MODIFIERS: mods, SLIDERS: slid}

    if args.check:
        stale = [p.name for p, text in wanted.items()
                 if not p.exists() or p.read_text() != text]
        for n in stale:
            print(f"{n} no longer matches data/targets/faceunits", file=sys.stderr)
        if stale:
            return 1
        print(f"faceunits: {len(stems)} targets, {len(wanted)} files match a fresh derivation")
        return 0

    for p, text in wanted.items():
        p.write_text(text)
    print(f"faceunits: {len(stems)} targets -> {MODIFIERS.name}, {SLIDERS.name}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
