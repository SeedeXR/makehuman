#!/usr/bin/env python3
"""Refuse a forbidden dataset that has actually been ADDED to the tree.

`LICENSING.md` §5.2 and §5.2a refuse several datasets by name -- the SMPL family
of model files, Human3.6M, Human-M3, the Texel scans, Quaternius assets -- and
until now nothing checked. The existing CI step covers the Autodesk FBX SDK and
only that. A refusal recorded in a document is an intention; this is the gate.

WHY IT LOOKS FOR PAYLOAD AND NOT FOR WORDS. The obvious implementation greps the
repository for "SMPL" and fails on a hit, which would fire on `LICENSING.md`'s
own paragraph explaining why SMPL is refused -- so the gate would be red from
the day it was written, get a documentation exclusion bolted on, and then pass
on anything a future document happened to mention. This project has the
inverse failure on record already: a library recorded as forbidden made the
forbidden-dependency gate pass on it, exit 0 with the thing in the build.

So this looks for FILES that are the dataset -- a `.pkl` named like an SMPL
model, a Human3.6M archive, a Quaternius asset pack -- under the directories
that ship, and for code that would LOAD one. Prose is not payload. A file whose
name matches is a hit wherever it sits, because a licence breach is a function
of what the repository contains, not of which folder it was put in.
"""
import re
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent

# Each entry: (label, filename regex, why). The patterns name FILES the dataset
# ships, not the dataset's topic -- "smpl" alone would catch a variable called
# `smplify` in a comment.
FORBIDDEN_FILES = [
    ("SMPL / SMPL-H / SMPL-X / MANO model files",
     re.compile(r"(?i)^(smpl[_-].*\.(pkl|npz)|basicmodel.*\.pkl|mano_(left|right)\.pkl"
                r"|smplx?_(neutral|male|female)\.(pkl|npz))$"),
     "LICENSING.md 5.2: research-only licence, and 5.2a: reachable through SOMA-X's "
     "SMPL backends by a front door"),
    ("Human3.6M",
     re.compile(r"(?i)^(h36m|human3\.?6m).*\.(zip|tar|tar\.gz|cdf|mat|npz)$"),
     "LICENSING.md 5.2a: a permissive wrapper does not launder the payload"),
    ("Human-M3",
     re.compile(r"(?i)^human[_-]?m3.*\.(zip|tar|tar\.gz|npz|json)$"),
     "LICENSING.md 5.2a: the data licence bars commercial products and services"),
    ("Texel body scans",
     re.compile(r"(?i)^texel[_-].*\.(zip|tar|tar\.gz|obj|ply)$"),
     "LICENSING.md 5.2a: MIT code, CC-BY-NC data -- the split is the point"),
    ("Quaternius asset packs",
     re.compile(r"(?i)^quaternius.*\.(zip|fbx|glb|gltf|obj)$"),
     "LICENSING.md 5.2a: QAL v1.0 forbids redistributing the assets standalone"),
    # Depth-estimation WEIGHTS, audited 2026-10-03. The file extensions are the
    # giveaway: a checkpoint is what actually ships, and a paper or a comment
    # naming one of these models is not a breach.
    #
    # **MoGe is deliberately ABSENT from this pattern.** The owner chose it on
    # 2026-10-03 (LICENSING.md 5.2c) with the data concern recorded, so a gate
    # that refused it would block the project's own decision -- the same
    # failure as a gate that passes something refused, pointing the other way.
    # The rest stay refused.
    ("monocular depth model weights",
     re.compile(r"(?i)^(depth[_-]?anything.*|midas.*|dpt[_-].*|zoedepth.*"
                r"|depth[_-]?pro.*|nvdepth.*)\.(onnx|pt|pth|safetensors|bin|mlmodel|mlpackage)$"),
     "LICENSING.md 5.2b: this depth model is refused -- the permissive ones "
     "have non-commercial or unidentifiable training data, and NVIDIA's, whose "
     "data IS clean, ships under a field-of-use-restricted licence. MoGe is the "
     "one that was accepted (5.2c)"),
]

# Code that would LOAD one of the above. Deliberately narrow: a path being
# opened, not the word appearing.
FORBIDDEN_LOADS = re.compile(
    r"(?i)(open|load|read|fopen|ifstream|path)\s*\(?\s*[\"'][^\"']*"
    r"(smpl[_-][a-z]*\.(pkl|npz)|mano_(left|right)\.pkl|h36m|human3\.6m"
    r"|(depth[_-]?anything|midas|zoedepth|nvdepth)[^\"']*\.(onnx|pt|pth|safetensors))")

# Where the gate does NOT look for prose matches: these explain the refusals.
# NOT an exemption for payload -- a forbidden FILE in any of these is still a
# hit, because the filename scan does not consult this list at all.
PROSE = {"LICENSING.md"}

CODE_SUFFIXES = {".py", ".cpp", ".h", ".cmake", ".txt", ".sh", ".yml", ".yaml"}


def tracked_files() -> list[Path]:
    """Only what git tracks. An untracked scratch file is not something we ship,
    and scanning build trees would flag a dependency's own test fixtures."""
    out = subprocess.run(["git", "-C", str(ROOT), "ls-files"],
                         capture_output=True, text=True, check=True).stdout
    return [ROOT / line for line in out.splitlines() if line]


def main() -> int:
    findings: list[str] = []
    files = tracked_files()

    for path in files:
        name = path.name
        for label, pattern, why in FORBIDDEN_FILES:
            if pattern.match(name):
                findings.append(f"{path.relative_to(ROOT)}: {label} -- {why}")

    for path in files:
        rel = str(path.relative_to(ROOT))
        if rel in PROSE or rel.startswith("memory/") or path.suffix not in CODE_SUFFIXES:
            continue
        try:
            text = path.read_text(encoding="utf-8", errors="ignore")
        except OSError:
            continue
        for lineno, line in enumerate(text.splitlines(), 1):
            # A comment explaining a refusal is not a load.
            stripped = line.lstrip()
            if stripped.startswith(("#", "//", "*", "/*")):
                continue
            if FORBIDDEN_LOADS.search(line):
                findings.append(f"{rel}:{lineno}: loads a forbidden dataset -- {line.strip()[:70]}")

    if findings:
        print("forbidden dataset material is present:", file=sys.stderr)
        for f in findings:
            print(f"  {f}", file=sys.stderr)
        print("\nSee LICENSING.md 5.2, 5.2a and 5.2b. Audit a dataset or a model BEFORE adding it.",
              file=sys.stderr)
        return 1

    print(f"no forbidden dataset material in {len(files)} tracked files")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
