#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-or-later
"""
Verify that a `.mhmat` written by the C++ writer is still readable by the
Python reference, field for field.

Interoperability is the point of keeping the format: a MakeHuman 1.x install
must be able to open what this port writes. The C++ round-trip test proves we
can read our own output; only the reference can prove the other direction.

Usage:
    ./.venv-mh/bin/python tools/verify_material_roundtrip.py <written.mhmat>...

Each argument is a file the C++ writer produced from a shipped material of the
same stem. Exits non-zero on the first mismatch.
"""
from __future__ import annotations

import os
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
LEGACY = REPO / "legacy/python"

os.chdir(LEGACY)
for _p in ("", "lib", "core", "apps", "shared"):
    sys.path.insert(0, str(LEGACY / _p) if _p else str(LEGACY))

import material  # noqa: E402

# autoBlendSkin makes the reference's diffuseColor getter reach into the app's
# skin blender (material.py:1414), which raises headlessly. Compare the stored
# attribute instead of the property for those.
SCALARS = [
    "name", "shininess", "opacity", "translucency", "shadeless", "wireframe",
    "transparent", "alphaToCoverage", "backfaceCull", "depthless",
    "castShadows", "receiveShadows", "autoBlendSkin", "sssEnabled",
    "sssRScale", "sssGScale", "sssBScale",
]
COLORS = ["_ambientColor", "_specularColor", "_emissiveColor"]


def compare(original: Path, written: Path) -> list[str]:
    a = material.fromFile(str(original))
    b = material.fromFile(str(written))
    bad: list[str] = []

    for f in SCALARS:
        if getattr(a, f) != getattr(b, f):
            bad.append(f"{f}: {getattr(a, f)!r} -> {getattr(b, f)!r}")
    for f in COLORS:
        if getattr(a, f).asTuple() != getattr(b, f).asTuple():
            bad.append(f"{f}: {getattr(a, f).asTuple()} -> {getattr(b, f).asTuple()}")

    if a.tags != b.tags:
        bad.append(f"tags: {sorted(a.tags)} -> {sorted(b.tags)}")
    # shaderParameters folds diffuseColor in (material.py:1076), so for an
    # autoBlendSkin material it hits the same headless crash as toFile. Read the
    # stored dict directly there; it is the part the file actually carries.
    pa = a._shaderParameters if a.autoBlendSkin else a.shaderParameters
    pb = b._shaderParameters if b.autoBlendSkin else b.shaderParameters
    if sorted(pa) != sorted(pb):
        bad.append(f"shaderParams: {sorted(pa)} -> {sorted(pb)}")
    if sorted(a.shaderConfig.items()) != sorted(b.shaderConfig.items()):
        bad.append(f"shaderConfig: {sorted(a.shaderConfig.items())} -> "
                   f"{sorted(b.shaderConfig.items())}")
    if bool(a.shader) != bool(b.shader):
        bad.append(f"shader: {a.shader!r} -> {b.shader!r}")
    if bool(a.diffuseTexture) != bool(b.diffuseTexture):
        bad.append(f"diffuseTexture: {a.diffuseTexture!r} -> {b.diffuseTexture!r}")
    return bad


def main() -> int:
    if len(sys.argv) < 2:
        print(__doc__)
        return 2

    failures = 0
    for arg in sys.argv[1:]:
        written = Path(arg).resolve()
        if not written.exists():
            print(f"FAIL {written}: does not exist")
            failures += 1
            continue
        # The original is the same stem without the .roundtrip suffix.
        original = written.with_name(written.name.replace(".roundtrip", ""))
        bad = compare(original, written)
        if bad:
            failures += 1
            print(f"FAIL {written.name}")
            for line in bad:
                print(f"       {line}")
        else:
            print(f"ok   {written.name}  (reference reads it identically)")
    return 1 if failures else 0


if __name__ == "__main__":
    raise SystemExit(main())
