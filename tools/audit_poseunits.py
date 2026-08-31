#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Reports how much of `body-poseunits.json` the shipped rigs can actually use.

The asset was authored against a richer skeleton than MakeHuman ships. Building
a pose-unit consumer on it without knowing that would produce poses that
silently do nothing -- so the mismatch is measured here and gated, rather than
discovered later as "the foot pose has no effect".

Exits non-zero if resolvability gets WORSE than the recorded baseline. Adding
the missing bones to a rig makes it better, which is not a failure.
"""
import json
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
UNITS = REPO / "data" / "poseunits" / "body-poseunits.json"
RIGS = {
    "default": REPO / "data" / "rigs" / "default.mhskel",
    "mixamo_superset": REPO / "data" / "rigs" / "mixamo_superset.mhskel",
}

# Measured 2026-08-31. These are a floor, not a target: more is fine.
BASELINE_FULLY_RESOLVABLE = 29
BASELINE_TOTAL_POSES = 61


def main() -> int:
    poses = json.loads(UNITS.read_text())["poses"]
    referenced: set[str] = set()
    for mapping in poses.values():
        referenced |= set(mapping)

    problems: list[str] = []
    print(f"body-poseunits.json: {len(poses)} poses, "
          f"{len(referenced)} distinct bones referenced")

    if len(poses) != BASELINE_TOTAL_POSES:
        problems.append(f"expected {BASELINE_TOTAL_POSES} poses, found {len(poses)}")

    best_fully = 0
    for rig_name, rig_path in RIGS.items():
        bones = set(json.loads(rig_path.read_text())["bones"])
        fully = partial = dead = 0
        dead_names: list[str] = []
        for name, mapping in poses.items():
            have = sum(1 for b in mapping if b in bones)
            if have == len(mapping):
                fully += 1
            elif have == 0:
                dead += 1
                dead_names.append(name)
            else:
                partial += 1
        missing = sorted(referenced - bones)
        best_fully = max(best_fully, fully)
        print(f"  {rig_name:<16} fully {fully:2d}  partial {partial:2d}  "
              f"dead {dead:2d}   bones missing from rig: {len(missing)}")
        if dead_names:
            print(f"      poses that would do NOTHING: {', '.join(sorted(dead_names))}")
        if missing:
            print(f"      missing bones: {', '.join(missing)}")

    if best_fully < BASELINE_FULLY_RESOLVABLE:
        problems.append(f"resolvability regressed: best rig resolves {best_fully} poses "
                        f"fully, baseline is {BASELINE_FULLY_RESOLVABLE}")

    # Two poses drive MOUTH bones. Raising an arm must not move the lips; this
    # is an authoring error in the reference asset, recorded so a future reader
    # does not treat it as our bug. Pinned so we notice if the data changes.
    mouth = {"oris01", "oris02", "oris03", "oris04", "oris05", "oris06", "oris07"}
    suspect = sorted(p for p, m in poses.items()
                     if set(m) & mouth and not p.lower().startswith(("mouth", "lip", "jaw")))
    print(f"  poses driving mouth bones despite a body name: {len(suspect)} {suspect}")
    if suspect != ["UpperArmUpLeft1", "UpperArmUpLeft2"]:
        problems.append(f"the set of mouth-driving body poses changed: {suspect}")

    if problems:
        print("\nPROBLEMS", file=sys.stderr)
        for p in problems:
            print("  " + p, file=sys.stderr)
        return 1
    print("\npose-unit resolvability is no worse than the recorded baseline")
    return 0


if __name__ == "__main__":
    sys.exit(main())
