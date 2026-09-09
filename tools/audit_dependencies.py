#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Every build dependency is recorded in LICENSING.md section 5.1, and none is
one section 5.2 refuses.

This replaces an inline CI step that grepped the WHOLE of LICENSING.md for each
dependency name. That was fine while the file only ever named dependencies it
had accepted. It stopped being fine the moment section 5.2.1 recorded four
libraries as REFUSED and named them: adding `find_package(FFTW)` then passed
the gate, because "FFTW" appears in LICENSING.md -- in the row that exists to
forbid it. Measured, not theorised: with FFTW appended to CMakeLists.txt the
old check exited 0.

So the section a name appears in is the whole point, and the check has to read
structure rather than the file as one blob.
"""

from __future__ import annotations

import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
LICENSING = ROOT / "LICENSING.md"

# The Forbidden table itself, and nothing under it. Naming a dependency here is
# a refusal, and saying so beats reporting it as merely unrecorded -- the fix is
# a different one. Deliberately NOT every section numbered 5.2*: 5.2.1 explains
# engineering refusals that are not licence refusals, and 5.2a lists things that
# were licence-CLEARED. Nor 5.3, which is prose about one refused SDK and names
# the accepted alternative in passing -- matching it reported assimp as refused
# the first time this ran.
REFUSING_SECTION = "5.2"

# What may be depended on: section 5.1 and any subsection of it.
ALLOWING_PREFIX = "5.1"

# `find_package(Qt6)` against a LICENSING.md row that says "Qt": strip trailing
# version digits before matching, or the project's oldest and best-documented
# dependency reports as unrecorded.
#
# `str.rstrip` rather than a regex. `re.compile(r"[0-9]+$")` was flagged by
# SonarQube for super-linear backtracking (python:S8786) -- a real property of
# `X+$`, even if no dependency name is long enough for it to matter -- and this
# has no backtracking to have.
DIGITS = "0123456789"

# Guards against this gate silently checking nothing, which is the failure mode
# of the version it replaces. Today it finds exactly five: Catch2,
# nlohmann_json, Qt6, assimp, draco. The floor is below that on purpose -- a
# dependency legitimately going away should not need this number edited, only a
# scan that has stopped finding anything should fail.
MINIMUM_DEPS = 4


def sections(text: str) -> dict[str, str]:
    """LICENSING.md split by its numbered headings, each body ending where the
    next heading starts.

    The trailing-letter group is for `### 5.2a`, which is a section of its own
    and must not be swallowed by the one above it.
    """
    out: dict[str, str] = {}
    heads = list(re.finditer(r"^#{2,4}\s+(\d+(?:\.\d+)*[a-z]?)\s", text, re.MULTILINE))
    for i, h in enumerate(heads):
        end = heads[i + 1].start() if i + 1 < len(heads) else len(text)
        out[h.group(1)] = text[h.end() : end]
    return out


COMMENT = re.compile(r"#.*")


def code(path: Path) -> str:
    """@p path with CMake comments stripped.

    The sibling "legacy tree is not part of the build" gate carries a note that
    it matched its own explanatory prose three separate times before stripping
    was made uniform, and this tool repeated the mistake within the hour: the
    ctest registration for it has a comment containing the literal
    `find_package(FFTW)`, which made it report FFTW as a forbidden dependency
    of a clean tree. A gate that greps for a forbidden string will always match
    the document forbidding it.
    """
    return "\n".join(COMMENT.sub("", line) for line in path.read_text().splitlines())


def declared_dependencies() -> set[str]:
    """Both ways a dependency arrives: fetched, or found on the system."""
    deps: set[str] = set()
    # `\w` rather than the explicit class (SonarQube python:S6353). Under
    # Unicode it is broader than `[A-Za-z0-9_]`, which for a gate is the safe
    # direction: a stranger name is still reported rather than skipped.
    deps |= set(re.findall(r"FetchContent_Declare\((\w+)", code(ROOT / "CMakeLists.txt")))
    for f in ROOT.rglob("CMakeLists.txt"):
        if "build" in f.relative_to(ROOT).parts:
            continue
        deps |= set(re.findall(r"find_package\((\w+)", code(f)))
    return deps


def named_in(section: str, dep: str) -> bool:
    stem = dep.rstrip(DIGITS)
    low = section.lower()
    return dep.lower() in low or (bool(stem) and stem.lower() in low)


def main() -> int:
    parts = sections(LICENSING.read_text())
    allow = "".join(
        body
        for number, body in parts.items()
        if number == ALLOWING_PREFIX or number.startswith(ALLOWING_PREFIX + ".")
    )
    refuse = parts.get(REFUSING_SECTION, "")
    if not allow or not refuse:
        print(
            f"LICENSING.md is missing section {ALLOWING_PREFIX} or "
            f"{REFUSING_SECTION}; this gate cannot tell a recorded dependency "
            f"from an unrecorded or a refused one.",
            file=sys.stderr,
        )
        return 1

    deps = declared_dependencies()
    if len(deps) < MINIMUM_DEPS:
        print(
            f"audit_dependencies found only {len(deps)} dependencies, expected at "
            f"least {MINIMUM_DEPS} -- the CMake layout has moved and this gate is "
            f"checking nothing.",
            file=sys.stderr,
        )
        return 1

    refused: list[str] = []
    missing: list[str] = []
    for dep in sorted(deps):
        if named_in(refuse, dep):
            refused.append(dep)
        elif not named_in(allow, dep):
            missing.append(dep)

    if refused:
        print(
            f"A dependency LICENSING.md {REFUSING_SECTION} FORBIDS is in the build:",
            file=sys.stderr,
        )
        for dep in refused:
            print("  " + dep, file=sys.stderr)
        print(
            f"\nForbidden is not the same as unrecorded. Either the dependency goes,\n"
            f"or the decision is revisited and the row moved into {ALLOWING_PREFIX} first.",
            file=sys.stderr,
        )
        return 1
    if missing:
        print(f"Dependencies not recorded in LICENSING.md {ALLOWING_PREFIX}:", file=sys.stderr)
        for dep in missing:
            print("  " + dep, file=sys.stderr)
        print(
            f"\nRecord the licence, the version and how it is linked in section "
            f"{ALLOWING_PREFIX} before depending on it (memory/instruction.md 7).\n"
            f"A library discussed elsewhere in the file is NOT recorded by being\n"
            f"mentioned -- that is the hole this gate was written to close.",
            file=sys.stderr,
        )
        return 1

    print(f"dependencies: {len(deps)} declared, all recorded in LICENSING.md {ALLOWING_PREFIX}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
