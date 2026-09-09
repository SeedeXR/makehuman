#!/usr/bin/env python3
"""Fail if the headless modules reach for Qt.

M10 wants `parameters -> mesh` callable with no GUI stack: a batch sampler, a
training loop, a server. That is only true while **foundation, core, rig and
io** stay free of Qt, and today they are -- measured, not assumed. Nothing
enforced it, which is the same position `<charconv>` was in when it reached CI
twice.

`render` is deliberately NOT in the list: it draws through QRhi and is Qt by
design. `ui` and `app` are Qt by definition.

The check is the INCLUDE and the CMake link, not the word: several files in
these modules explain in prose why they must not depend on Qt, and a naive
grep flags the explanation instead of the offence -- the trap the licence gate
in ci.yml documents twice.
"""

from __future__ import annotations

import re
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent

#: Modules that must build and run without Qt.
HEADLESS = ("foundation", "core", "rig", "io")

#: `#include <QFoo>` or `#include <QtCore/...>` or the quoted forms.
INCLUDE = re.compile(r'^\s*#\s*include\s*[<"](Q[A-Z]\w*|Qt\w+/)')

#: A CMake link or lookup. `Qt6::Core`, `find_package(Qt6 ...)`.
CMAKE = re.compile(r"Qt\d*::|find_package\s*\(\s*Qt")

#: An audit that scans nothing passes, silently, forever. There are 80+ sources
#: across the four modules today; a floor well under that catches a renamed
#: directory or an emptied module list without tracking the real number.
MINIMUM_FILES = 50


def scan_sources(root: Path) -> tuple[list[str], int]:
    """Qt includes under one directory, and how many files were looked at."""
    found: list[str] = []
    scanned = 0
    if not root.is_dir():
        return found, scanned
    for path in sorted(root.rglob("*")):
        if path.suffix not in (".cpp", ".h"):
            continue
        scanned += 1
        for n, line in enumerate(path.read_text(encoding="utf-8").splitlines(), 1):
            if INCLUDE.match(line):
                found.append(f"{path.relative_to(REPO)}:{n}: {line.strip()}")
    return found, scanned


def scan_cmake(path: Path) -> list[str]:
    """Qt links in one CMakeLists. Comments explain the rule, so they are
    stripped first -- flagging the explanation instead of the offence is the
    trap ci.yml documents twice."""
    if not path.is_file():
        return []
    found: list[str] = []
    for n, line in enumerate(path.read_text(encoding="utf-8").splitlines(), 1):
        if CMAKE.search(line.split("#", 1)[0]):
            found.append(f"{path.relative_to(REPO)}:{n}: {line.strip()}")
    return found


def offences() -> tuple[list[str], int]:
    found: list[str] = []
    scanned = 0
    for module in HEADLESS:
        for root in (REPO / "src" / module, REPO / "include" / "makehuman" / module):
            hits, seen = scan_sources(root)
            found += hits
            scanned += seen
        found += scan_cmake(REPO / "src" / module / "CMakeLists.txt")
    return found, scanned


def main() -> int:
    bad, scanned = offences()
    if scanned < MINIMUM_FILES:
        print(
            f"audit_headless scanned only {scanned} files, expected at least "
            f"{MINIMUM_FILES} -- the module list or the layout has moved, and "
            f"this gate is checking nothing.",
            file=sys.stderr,
        )
        return 1
    if bad:
        print("Qt reached a headless module (foundation, core, rig, io):", file=sys.stderr)
        for line in bad:
            print("  " + line, file=sys.stderr)
        print(
            "\nThese four must build without a GUI stack -- that is what makes the\n"
            "headless parameters -> mesh path usable from a batch tool. Put the Qt\n"
            "dependency in ui, render or app instead.",
            file=sys.stderr,
        )
        return 1
    print(f"headless: {', '.join(HEADLESS)} are free of Qt ({scanned} files)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
