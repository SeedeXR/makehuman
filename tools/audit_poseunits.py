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


def _report_rig(rig_name, rig_path, poses, referenced) -> int:
    """Prints one rig's resolvability line and returns how many poses it
    resolves FULLY."""
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
    print(f"  {rig_name:<16} fully {fully:2d}  partial {partial:2d}  "
          f"dead {dead:2d}   bones missing from rig: {len(missing)}")
    if dead_names:
        print(f"      poses that would do NOTHING: {', '.join(sorted(dead_names))}")
    if missing:
        print(f"      missing bones: {', '.join(missing)}")
    return fully


def mouth_suspects(poses) -> list[str]:
    """Poses driving a MOUTH bone without being named for the mouth.

    Split out from the check that PINS the answer so the detection can be
    exercised on its own. A pinned-set check only fires when the shipped data
    moves, so nothing notices if its teeth are pulled -- see `self_test`.
    """
    mouth = {"oris01", "oris02", "oris03", "oris04", "oris05", "oris06", "oris07"}
    return sorted(p for p, m in poses.items()
                  if set(m) & mouth and not p.lower().startswith(("mouth", "lip", "jaw")))


def foot_suspects(poses) -> list[str]:
    """Poses driving NOTHING BUT foot bones without being named for the foot.

    A pose that merely touches a foot bone is not mis-named; one that drives
    only foot bones while called something else is.
    """
    # By PREFIX, not a fixed list: the asset happens to reference only left-side
    # heel and metatarsal bones today, so a hardcoded set would miss a right-side
    # pose, and miss the toe bones entirely -- a pose named for a finger driving
    # only toes is mis-named in exactly the same way. MEASURED: the broader test
    # flags the same two poses, so this costs nothing and stops being accidental.
    def is_foot(bone: str) -> bool:
        return bone.startswith(("heel", "metatarsal", "toe"))

    return sorted(p for p, m in poses.items()
                  if set(m) and all(is_foot(b) for b in m)
                  and not p.lower().startswith(("foot", "toe", "heel")))


def _mouth_driving_problems(poses) -> list[str]:
    """Two poses drive MOUTH bones. Raising an arm must not move the lips; this
    is an authoring error in the reference asset, recorded so a future reader
    does not treat it as our bug. Pinned so we notice if the data changes."""
    suspect = mouth_suspects(poses)
    print(f"  poses driving mouth bones despite a body name: {len(suspect)} {suspect}")
    if suspect != ["UpperArmUpLeft1", "UpperArmUpLeft2"]:
        return [f"the set of mouth-driving body poses changed: {suspect}"]
    return []


def _foot_driving_problems(poses) -> list[str]:
    """Two poses named for a FINGER drive nothing but foot bones.

    `Finger1CloseLeft` and `Finger2CloseLeft` each map exactly the six bones
    `FootDownLeft` maps -- heel plus all five metatarsals -- and nothing else.
    They are not finger poses with a stray foot bone; they are duplicates of a
    foot pose wearing the wrong name.

    Same class as the mouth error above, and recorded for the same reason: a
    consumer trusting the NAME would ship a "close the finger" control that
    moves the foot. This rig has no heel or metatarsal bones, so today they
    resolve to nothing and the error is harmless -- it stops being harmless the
    moment anyone adds foot detail.

    Found 2026-09-14 while scoping the bone table these pose units need.
    """
    suspect = foot_suspects(poses)
    print(f"  poses driving only foot bones despite another name: {len(suspect)} {suspect}")
    if suspect != ["Finger1CloseLeft", "Finger2CloseLeft"]:
        return [f"the set of foot-driving mis-named poses changed: {suspect}"]
    return []


def self_test() -> list[str]:
    """Both detectors, on data that must and must not be flagged.

    MEASURED: breaking either DETECTOR -- making it match nothing, or match
    everything -- is caught here. Breaking the PINNING above it is not, and
    cannot be: "the shipped set must still be exactly these two" only fires when
    the asset changes, which is its whole job. So the split is deliberate and
    the coverage is partial on purpose: the logic is tested, the pin is a
    tripwire. Saying so beats implying both are covered.
    """
    problems: list[str] = []
    # One sample foot bone, named once: the detector matches by PREFIX, so any
    # heel bone exercises the same path and repeating the literal only invites
    # them to drift apart.
    heel = "heel.L"

    if mouth_suspects({"ElbowBendLeft": {"oris01": 1}}) != ["ElbowBendLeft"]:
        problems.append("the mouth detector missed a body pose driving a mouth bone")
    if mouth_suspects({"MouthOpen": {"oris01": 1}}):
        problems.append("the mouth detector flags a correctly named pose")
    if foot_suspects({"Finger9CloseLeft": {heel: 1}}) != ["Finger9CloseLeft"]:
        problems.append("the foot detector missed a mis-named foot pose")
    if foot_suspects({"FootDownLeft": {heel: 1}}):
        problems.append("the foot detector flags a correctly named pose")
    if foot_suspects({"LegLift": {heel: 1, "upperleg01.L": 1}}):
        problems.append("the foot detector flags a pose that only touches a foot bone")
    # Side-agnostic, and toes count: the shipped asset exercises neither, so
    # without these the prefix match could regress to left-heel-only unnoticed.
    if foot_suspects({"Finger9CloseRight": {"heel.R": 1}}) != ["Finger9CloseRight"]:
        problems.append("the foot detector misses right-side foot bones")
    if foot_suspects({"Finger9CurlLeft": {"toe1-1.L": 1}}) != ["Finger9CurlLeft"]:
        problems.append("the foot detector misses toe bones")
    return problems


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

    # default=0 preserves the original behaviour if RIGS is ever empty; max()
    # on an empty generator raises where the old accumulator stayed at zero.
    best_fully = max((_report_rig(name, path, poses, referenced)
                      for name, path in RIGS.items()), default=0)

    if best_fully < BASELINE_FULLY_RESOLVABLE:
        problems.append(f"resolvability regressed: best rig resolves {best_fully} poses "
                        f"fully, baseline is {BASELINE_FULLY_RESOLVABLE}")

    problems += _mouth_driving_problems(poses)
    problems += _foot_driving_problems(poses)
    problems += self_test()

    if problems:
        print("\nPROBLEMS", file=sys.stderr)
        for p in problems:
            print("  " + p, file=sys.stderr)
        return 1
    print("\npose-unit resolvability is no worse than the recorded baseline")
    return 0


if __name__ == "__main__":
    sys.exit(main())
