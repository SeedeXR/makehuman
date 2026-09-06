#!/usr/bin/env python3
"""Fail if shipped data asks for a shader the viewport does not implement.

`.mhmat` carries a `shader` line naming one of the reference's GLSL programs.
This port reads that line down to a path stem (`src/core/Material.cpp:59`) and
then **ignores it**: the viewport picks its shading model from settings, not
from the material. So an asset asking for `toon` gets PBR or the matcap, looks
wrong, and nothing anywhere says why.

That is fine while the only two stems in `data/` are accounted for, and it stops
being fine the moment a new asset arrives. Hence this: every stem is either
IMPLEMENTED or EXCLUDED with a reason that must appear in the file which records
the decision, so the code and the write-up cannot drift apart.

Reopening one of the exclusions means deleting its line here, which fails until
the model actually exists.
"""

from __future__ import annotations

import re
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
DATA = REPO / "data"

#: `shader data/shaders/glsl/xray` -> `xray`.
SHADER_LINE = re.compile(r"^\s*shader\s+(\S+)\s*$", re.IGNORECASE | re.MULTILINE)

#: Stems the viewport really has. `litsphere` is ShadingModel::Litsphere,
#: ported in M6 and pixel-checked against the reference.
IMPLEMENTED = {"litsphere"}

#: stem -> (file that records the decision, a phrase that must appear in it).
EXCLUDED = {
    "xray": ("LICENSING.md", "The xray shader is MeshLab"),
}


def stems_in_data() -> dict[str, list[str]]:
    """Every shader stem named by a shipped material, and who names it."""
    out: dict[str, list[str]] = {}
    for path in sorted(DATA.rglob("*.mhmat")):
        for named in SHADER_LINE.findall(path.read_text(errors="replace")):
            stem = Path(named).name
            out.setdefault(stem, []).append(path.relative_to(REPO).as_posix())
    return out


def main() -> int:
    found = stems_in_data()
    offences: list[str] = []

    for stem, users in sorted(found.items()):
        if stem in IMPLEMENTED:
            continue
        record = EXCLUDED.get(stem)
        if record is None:
            offences.append(
                f"{stem}: requested by {', '.join(users)}, "
                f"neither implemented nor excluded")
            continue
        doc, phrase = record
        if phrase not in (REPO / doc).read_text():
            offences.append(
                f"{stem}: excluded on the strength of \"{phrase}\" in {doc}, "
                f"which no longer says it")

    if offences:
        print("shipped materials ask for shading this port does not have:", file=sys.stderr)
        for offence in offences:
            print(f"  {offence}", file=sys.stderr)
        print(
            "\nEither implement the model, or record why not and add it to "
            "EXCLUDED here.\nSilently substituting a different shader is how an "
            "asset ends up looking wrong with nothing to explain it.",
            file=sys.stderr,
        )
        return 1

    implemented = sorted(found.keys() & IMPLEMENTED)
    print(f"shading: {len(found)} shader stem(s) requested by data -- "
          f"implemented {implemented}, excluded {sorted(EXCLUDED)}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
