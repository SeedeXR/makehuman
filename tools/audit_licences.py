#!/usr/bin/env python3
"""Fail if a bundled file carries a third-party licence LICENSING.md does not record.

`LICENSING.md` is the file this project answers licence questions from, and a
blanket row in it is the easiest thing in the repository to get wrong: it
describes a whole directory, and nobody re-reads 1,787 files to check.

One was wrong. Row 42 says "UI images, icons, themes, GLSL shaders |
AGPL-3.0", citing `LICENSE.md` §B. That holds for the shaders the MakeHuman
team wrote -- litsphere, phong, toon all carry `**Licensing:** AGPL3` in their
own headers -- and NOT for `data/shaders/glsl/xray_*`, which is MeshLab, (C)
2005/2009 Visual Computing Lab / ISTI-CNR, "GNU General Public License ...
version 2 ... or (at your option) any later version"
(`xray_fragment_shader.txt:5-19`).

So this re-derives the claim from the files instead of trusting the row. Every
match is either in ALLOWED -- recorded in LICENSING.md, with the row named here
so the two cannot drift apart silently -- or an offence.

Deliberately narrow. It looks for the licence families this project has a rule
about (`CLAUDE.md` hard rule 6) in the text files we bundle and might port from.
It is not a substitute for reading a licence; it is what stops a NEW one
arriving unnoticed.
"""

from __future__ import annotations

import re
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent

#: Everything we ship. `legacy/python/` is deliberately out of scope: it is the
#: reference, it is never shipped, and LICENSING.md already itemises its GPL
#: corners (the pyFBX plugins).
SEARCH = ("data", "resources")

TEXT_SUFFIXES = (".txt", ".glsl", ".vert", ".frag", ".comp", ".md", ".py",
                 ".mhmat", ".mhclo", ".mhskel", ".mhw", ".target", ".obj",
                 ".mhpose", ".mhm", ".json", ".bvh", ".proxy")

#: Licence families CLAUDE.md rule 6 makes a decision about. The patterns match
#: the licence GRANT, not a mere mention: "GNU General Public License" appears
#: in the AGPL notice too, so GPL is only claimed when the version follows.
GPL2 = "GPL-2.0"

FAMILIES = {
    GPL2: re.compile(
        r"GNU General Public License.{0,200}?version 2", re.IGNORECASE | re.DOTALL),
    "GPL-3.0": re.compile(
        r"GNU General Public License.{0,200}?version 3", re.IGNORECASE | re.DOTALL),
    "non-commercial": re.compile(r"non-?commercial (?:use )?only", re.IGNORECASE),
}

#: path -> (family, the LICENSING.md row that records it).
#:
#: An entry here is a promise that a human read the file and wrote the finding
#: down. Adding one without touching LICENSING.md is the mistake this table
#: exists to make visible.
ALLOWED = {
    "data/shaders/glsl/xray_fragment_shader.txt": (GPL2, "The xray shader is MeshLab"),
    "data/shaders/glsl/xray_vertex_shader.txt": (GPL2, "The xray shader is MeshLab"),
}


def families_in(text: str) -> set[str]:
    """Every licence family the text actually GRANTS."""
    return {name for name, pattern in FAMILIES.items() if pattern.search(text)}


def sources() -> list[Path]:
    return [
        path
        for root in SEARCH
        for path in sorted((REPO / root).rglob("*"))
        if path.is_file() and path.suffix in TEXT_SUFFIXES
    ]


def main() -> int:
    licensing = (REPO / "LICENSING.md").read_text()
    offences: list[str] = []
    checked = 0

    for path in sources():
        rel = path.relative_to(REPO).as_posix()
        found = families_in(path.read_text(errors="replace"))
        checked += 1
        if not found:
            continue
        allowed = ALLOWED.get(rel)
        if allowed is None:
            offences.append(f"{rel}: {', '.join(sorted(found))}, not recorded in LICENSING.md")
        elif allowed[0] not in found:
            offences.append(f"{rel}: recorded as {allowed[0]}, but carries {', '.join(sorted(found))}")

    # The other half of the promise: the row must exist in LICENSING.md.
    for rel, (family, reason) in sorted(ALLOWED.items()):
        if reason not in licensing:
            offences.append(
                f"{rel}: allowed as {family} because of \"{reason}\", "
                f"but LICENSING.md does not mention it")

    if offences:
        print("bundled files carry licences LICENSING.md does not account for:", file=sys.stderr)
        for offence in offences:
            print(f"  {offence}", file=sys.stderr)
        print(
            "\nRead the file, record the finding in LICENSING.md, then add it to "
            "ALLOWED here with the row's wording.\nNever port from a file listed "
            "here -- see the pyFBX precedent (LICENSING.md, FBX written from the "
            "published spec).",
            file=sys.stderr,
        )
        return 1

    print(f"licences: {checked} bundled text files scanned, "
          f"{len(ALLOWED)} recorded third-party grants, 0 unaccounted")
    return 0


if __name__ == "__main__":
    sys.exit(main())
