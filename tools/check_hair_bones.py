# SPDX-License-Identifier: AGPL-3.0-or-later
"""Gate: hair that hangs must not be welded to the skull.

    ./.venv-mh/bin/python tools/check_hair_bones.py

A proxy inherits the skin weights of the body vertices it binds to
(`VertexWeights::proxyWeights`), so WHERE a hair vertex binds decides which
bones drive it. Bound entirely to the scalp, a fall that reaches the waist
still hangs off one bone and cannot swing however it is posed.

MEASURED before `make_hair_styles.graded_binds` existed: 97.5% of the long
hair's weight and 97.9% of the locs' sat on `head`. Nothing was missing from
the rig -- the spine and neck bones are among the 65 the export already
carries. The hair had simply never told the rig that it hangs.

This is not a shape test and deliberately not a tight one. It asks only that a
falling style carries real influence from below the neck, because that is the
property that silently disappears the moment somebody simplifies the binding
back to one region.
"""

import collections
import json
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
WEIGHTS = ROOT / "data" / "rigs" / "mixamo_superset_weights.mhw"

# stem -> (how much of the asset is the FALL, the share of weight that must sit
# below the neck). The cap bed under the rope styles is most of their vertices
# and correctly stays on the scalp, so the rope block is measured on its own.
# THE FLOORS ARE MEASURED, with margin, and the margin is wide on purpose: this
# gate exists to catch a fall going back to one bone, not to pin a number that
# shifts whenever a style's length changes.
#
#                     welded (one region)   graded      floor here
#   hair                     ~2%             20.8%         14%
#   locs                     ~2%             13.0%          8%
#   dreadlocks               ~2%             25.1%         16%
#
# The locs sit lowest because their ropes stop at the shoulder (y 5.85) while
# the long hair reaches the waist -- less of a loc hangs below the neck, which
# is anatomy and not a fault.
CASES = (
    ("hair", 0, 0.14),
    ("locs", 69950, 0.08),
    ("dreadlocks", 69950, 0.16),
)

# Bones that are NOT the head or the face. A fall reaching the shoulders must
# be driven partly by these or it is welded to the skull.
def below_the_neck(bone: str) -> bool:
    return bone.startswith(("spine", "clavicle", "breast", "shoulder"))


def bindings(path: Path):
    rows, started = [], False
    for line in path.read_text().splitlines():
        s = line.strip()
        if s.startswith("verts"):
            started = True
            continue
        if started and s:
            p = s.split()
            if len(p) != 9:
                break
            try:
                rows.append(tuple(int(x) for x in p[:3]) + tuple(float(x) for x in p[3:6]))
            except ValueError:
                break
    return rows


def main() -> int:
    by_vertex = collections.defaultdict(list)
    for bone, entries in json.loads(WEIGHTS.read_text())["weights"].items():
        for vertex, weight in entries:
            by_vertex[vertex].append((bone, weight))

    failures = []
    for stem, skip, floor in CASES:
        rows = bindings(ROOT / "data" / "hair" / f"{stem}.mhclo")
        if len(rows) <= skip:
            failures.append(f"{stem}: {len(rows)} bindings, expected more than {skip}")
            continue
        tally = collections.Counter()
        for v0, v1, v2, a, b, c in rows[skip:]:
            for vertex, share in ((v0, a), (v1, b), (v2, c)):
                if share <= 0.0:
                    continue
                for bone, weight in by_vertex.get(vertex, ()):
                    tally[bone] += weight * share
        total = sum(tally.values())
        if total <= 0.0:
            failures.append(f"{stem}: no bone drives it at all")
            continue
        torso = sum(w for b, w in tally.items() if below_the_neck(b)) / total
        head = tally.get("head", 0.0) / total
        print(f"{stem}: {100 * torso:.1f}% below the neck, {100 * head:.1f}% head "
              f"(floor {100 * floor:.0f}%)")
        if torso < floor:
            failures.append(f"{stem}: only {100 * torso:.1f}% of its fall is driven from "
                            f"below the neck, under the {100 * floor:.0f}% floor -- it is "
                            f"welded to the skull and cannot swing")
    for f in failures:
        print(f, file=sys.stderr)
    return 1 if failures else 0


if __name__ == "__main__":
    raise SystemExit(main())
