#!/usr/bin/env python3
"""Fail if a compile-time asset path is used where a runtime one belongs.

`MH_DATA_DIR`, `MH_SHADER_DIR` and `MH_RESOURCE_DIR` are absolute paths into
whichever source and build trees compiled the binary. They are the right thing
to pass to `foundation::resolve*Dir` as a last-resort fallback, and the wrong
thing everywhere else: a bundled or installed copy carries its own assets, and
code that reads the macro directly ignores them.

This is not hypothetical. `buildAssetGroups` scanned `MH_DATA_DIR / "litspheres"`,
`/ "poses"` and `/ "eyes"` directly, so a `.app` from the DMG -- which had all
three inside `Contents/Resources/data` -- still went looking in the build
machine's source tree and failed the moment that tree was gone. Everything
routed through `dataDir()` kept working. The difference is invisible on the
machine that built it, which is exactly the class of bug a packaging step ships.

Tests are exempt: they address fixture data by absolute path deliberately, and
they never run from a bundle.
"""

from __future__ import annotations

import re
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
MACROS = ("MH_DATA_DIR", "MH_SHADER_DIR", "MH_RESOURCE_DIR")
SEARCH = ("src",)

#: How many real uses each file may have, and why. Anything else is an offence.
ALLOWED = {
    # 4: the dataRoot() fallback initialiser, plus one resolve*Dir call each for
    # data, resources and shaders.
    "src/app/main.cpp": 4,
    # 1: the icon directory's fallback initialiser. setIconDir() overwrites it
    # from the resolved resources tree at start-up.
    "src/ui/Theme.cpp": 1,
}

#: A double-quoted string literal, escapes included.
STRING_LITERAL = re.compile(r'"(?:[^"\\]|\\.)*"')


def code_only(line: str) -> str:
    """The line with its comment and string literals removed.

    Both are full of these names: comments explain them constantly, and
    `DataDir.cpp` passes them as STRINGS because they double as environment
    variable names. Only a bare identifier is a use.
    """
    return STRING_LITERAL.sub('""', line.split("//", 1)[0])


def uses_in(path: Path) -> list[tuple[int, str]]:
    """Line number and text of every real use in one file."""
    hits = []
    for number, line in enumerate(path.read_text().splitlines(), 1):
        code = code_only(line)
        if any(re.search(r"\b" + macro + r"\b", code) for macro in MACROS):
            hits.append((number, line.strip()))
    return hits


def sources() -> list[Path]:
    return [
        path
        for root in SEARCH
        for path in sorted((REPO / root).rglob("*"))
        if path.suffix in (".cpp", ".h", ".mm")
    ]


def main() -> int:
    offences: list[str] = []
    total = 0
    for path in sources():
        rel = path.relative_to(REPO).as_posix()
        hits = uses_in(path)
        total += len(hits)
        if len(hits) > ALLOWED.get(rel, 0):
            offences += [f"{rel}:{number}: {text}" for number, text in hits]

    if offences:
        print("compile-time asset paths used outside the resolver:", file=sys.stderr)
        for offence in offences:
            print(f"  {offence}", file=sys.stderr)
        print(
            "\nUse dataDir(), the resolved resources tree, or the resolved shader "
            "dir instead.\nIf a new site is genuinely a resolver fallback, raise "
            "its budget in ALLOWED with a reason.",
            file=sys.stderr,
        )
        return 1

    print(f"runtime paths: {total} compile-time macro uses, all inside the resolver budget")
    return 0


if __name__ == "__main__":
    sys.exit(main())
