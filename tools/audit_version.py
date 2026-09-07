#!/usr/bin/env python3
"""Fail unless every product-version declaration agrees with `VERSION`.

There is one product version and it lives in `/VERSION`. Everything else must
DERIVE from it, and the two places that cannot -- because their file formats
have no way to read another file -- are checked here instead.

Why this gate exists, measured 2026-09-07: the repository had THREE independent
declarations and one of them was dead.

  * `/VERSION` said `0.1.0`, was untracked, had never been committed, and was
    read by nothing at all. A decoy for the next person who goes looking.
  * `CMakeLists.txt` said `2.0.0` and was the de facto source of truth.
  * `sonar-project.properties` said `2.0.0`, hand-copied.

Two of those agreeing was luck, not wiring. This makes disagreement a build
failure with a message that names the file and both values, because a version
that is wrong in one place is the kind of defect that ships and is then found by
a user reading an About box.

Exit status is the point: 0 when everything agrees, 1 otherwise.
"""

from __future__ import annotations

import re
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent

#: MAJOR.MINOR.PATCH and nothing else. CMake's `project(VERSION)` accepts a
#: fourth "tweak" component and a bare `1.2`, but a version that is sometimes
#: three numbers and sometimes two or four cannot be compared or formatted
#: consistently, so this is deliberately narrower than CMake allows.
SEMVER = re.compile(r"^(\d+)\.(\d+)\.(\d+)$")


def read_version_file() -> tuple[str | None, list[str]]:
    """The declared version, plus any complaint about the file itself."""
    path = REPO / "VERSION"
    if not path.exists():
        return None, [f"{path.relative_to(REPO)}: missing -- it is the source of truth"]
    raw = path.read_text(encoding="utf-8")
    text = raw.strip()
    problems: list[str] = []
    if not SEMVER.match(text):
        problems.append(
            f"VERSION: {text!r} is not MAJOR.MINOR.PATCH -- "
            "CMake, the macOS bundle and SonarQube all parse this"
        )
        return None, problems
    # A trailing newline is what every editor and `echo` writes, and what
    # `file(STRINGS)` strips either way. Say so rather than fail on it.
    if raw != text + "\n":
        problems.append("VERSION: should end with exactly one newline and no other whitespace")
    return text, problems


def check_cmake() -> list[str]:
    """The root `project(... VERSION ...)` must be read FROM the file.

    Checked as source text, not as a configured value -- which is why this
    takes no argument: a literal here is exactly the drift this gate exists to
    stop, and it would still produce a build whose every derived version agrees
    with itself and disagrees with `VERSION`.
    """
    path = REPO / "CMakeLists.txt"
    text = path.read_text(encoding="utf-8")
    problems: list[str] = []

    literal = re.search(r"project\s*\(\s*MakeHuman\s+VERSION\s+(\d[\d.]*)", text)
    if literal:
        problems.append(
            f"CMakeLists.txt: project(VERSION {literal.group(1)}) is a literal; "
            "it must read ${MH_VERSION} from the VERSION file"
        )
    if "${MH_VERSION}" not in text:
        problems.append("CMakeLists.txt: nothing feeds ${MH_VERSION} into project()")
    if 'file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/VERSION"' not in text:
        problems.append("CMakeLists.txt: the VERSION file is never read")
    return problems


def check_sonar(want: str) -> list[str]:
    """SonarQube's properties file cannot interpolate, so it is checked.

    Not deleted: the server's "new code" period can be defined as "since the
    previous version", which needs this key to mean anything. So it stays a
    duplicate, and staying correct is this gate's job.
    """
    path = REPO / "sonar-project.properties"
    problems: list[str] = []
    found = None
    for line in path.read_text(encoding="utf-8").splitlines():
        if line.startswith("sonar.projectVersion="):
            found = line.split("=", 1)[1].strip()
    if found is None:
        problems.append("sonar-project.properties: sonar.projectVersion is missing")
    elif found != want:
        problems.append(
            f"sonar-project.properties: sonar.projectVersion={found} but VERSION says {want}"
        )
    return problems


def check_no_private_macro() -> list[str]:
    """`MH_VERSION_STRING` used to be PRIVATE to one library.

    It was compiled into `mh_core` only, so `main.cpp` could not reach it --
    which is why `makehuman --version` did not work while the version sat in
    the binary the whole time. The generated header replaces it; this stops the
    macro coming back.
    """
    problems: list[str] = []
    for path in sorted(REPO.glob("src/*/CMakeLists.txt")):
        text = path.read_text(encoding="utf-8")
        if "MH_VERSION_STRING" in text:
            problems.append(
                f"{path.relative_to(REPO)}: defines MH_VERSION_STRING -- "
                "use the generated makehuman/foundation/Version.h instead"
            )
    return problems


def main() -> int:
    want, problems = read_version_file()
    if want is not None:
        problems += check_cmake()
        problems += check_sonar(want)
    problems += check_no_private_macro()

    if problems:
        print("product version declarations disagree:", file=sys.stderr)
        for p in problems:
            print(f"  {p}", file=sys.stderr)
        print(
            "\nThere is ONE product version, in /VERSION. Change it there and "
            "re-run CMake;\nanything that cannot read it is listed above and "
            "must be updated by hand.",
            file=sys.stderr,
        )
        return 1

    print(f"version: {want}, and every declaration agrees")
    return 0


if __name__ == "__main__":
    sys.exit(main())
