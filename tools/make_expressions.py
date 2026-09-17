#!/usr/bin/env python3
"""Author the shipped facial expressions from FACS Action Units.

    python3 tools/make_expressions.py [--check] [--app <binary>]

WHY THESE ARE GENERATED RATHER THAN DOWNLOADED. Upstream ships none: measured,
`legacy/python/data/expressions/` does not exist in the reference and there is
not one `.mhpose` in it. MakeHuman2's three demo expressions name pose units
(`LeftBrow`, `UpperLip`) that do not exist in this rig's sixty, so they would
load and drive nothing. Expressions are community content upstream, so there
was nothing to port -- but the machinery to AUTHOR them already shipped here:
`--facs` composes Action Units and `--save-expression` writes the result as a
`.mhpose` naming this rig's own units.

WHY FACS. The Action Unit is the unit of the published coding system (Ekman &
Friesen), so "Happy = AU6 + AU12" is a citable description of a face rather
than one person's idea of a smile. The six emotions below are the basic set
those authors code; the AU lists are theirs, the weights are ours. Each written
file records the AUs it came from in its `description`, so any of them can be
re-derived or argued with.

The result is OUR work under this project's CC0 asset licence -- no third-party
asset is copied, only a published description of which muscles move.
"""
import argparse
import json
import os
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
OUT = ROOT / "data" / "expressions"

# emotion -> (AU, weight). Ekman & Friesen's basic emotions, by Action Unit.
# Weights are this project's: an AU is present or absent in the coding system,
# and a renderer needs an intensity.
EXPRESSIONS = {
    "happy": [("AU6", 0.7), ("AU12", 1.0)],
    "sad": [("AU1", 0.8), ("AU4", 0.6), ("AU15", 0.9)],
    "surprised": [("AU1", 0.9), ("AU2", 0.9), ("AU5", 0.8), ("AU26", 0.7)],
    "afraid": [("AU1", 0.9), ("AU2", 0.7), ("AU4", 0.7), ("AU5", 1.0),
               ("AU7", 0.4), ("AU20", 0.8), ("AU26", 0.5)],
    "angry": [("AU4", 1.0), ("AU5", 0.6), ("AU7", 0.8), ("AU23", 0.9)],
    "disgusted": [("AU9", 1.0), ("AU15", 0.6), ("AU16", 0.7)],
}


def app_binary(override: str) -> str:
    """The built application, which owns the AU-to-pose-unit mapping.

    Shelling out rather than reimplementing: a second copy of that mapping in
    Python would drift, and the drift would show as an expression that is right
    in the generator and wrong in the application.
    """
    return override or os.environ.get("MH_APP") or str(
        ROOT / "build" / "macos-arm64-debug" / "src" / "app" / "makehuman.app" / "Contents"
        / "MacOS" / "makehuman")


def derive(app: str, into: Path) -> dict[str, str]:
    out = {}
    for stem, units in EXPRESSIONS.items():
        target = into / f"{stem}.mhpose"
        cmd = [app]
        for code, weight in units:
            cmd += ["--facs", f"{code}={weight}"]
        cmd += ["--save-expression", str(target)]
        subprocess.run(cmd, capture_output=True, text=True, check=True)
        out[f"{stem}.mhpose"] = target.read_text()
    return out


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--check", action="store_true",
                    help="regenerate into a temporary directory and fail if the "
                         "committed expressions differ")
    ap.add_argument("--app", default="", help="the built application to compose with")
    args = ap.parse_args()

    app = app_binary(args.app)
    try:
        if args.check:
            import tempfile
            with tempfile.TemporaryDirectory() as d:
                wanted = derive(app, Path(d))
        else:
            OUT.mkdir(parents=True, exist_ok=True)
            wanted = derive(app, OUT)
    # OSError alone: FileNotFoundError derives from it, so naming both is
    # redundant (python:S5713). CalledProcessError does NOT -- it comes from
    # SubprocessError -- so it stays.
    except (subprocess.CalledProcessError, OSError) as exc:
        print(f"cannot compose the expressions without the application: {exc}", file=sys.stderr)
        return 2

    if args.check:
        stale = [n for n, text in wanted.items()
                 if not (OUT / n).exists() or (OUT / n).read_text() != text]
        for n in stale:
            print(f"{n} differs from a fresh derivation", file=sys.stderr)
        if stale:
            return 1
        print(f"expressions: {len(wanted)} files match a fresh derivation")
        return 0

    for name in sorted(wanted):
        units = json.loads((OUT / name).read_text())["unit_poses"]
        print(f"  {name:20s} {len(units)} pose units")
    print(f"expressions: {len(wanted)} written to {OUT}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
