#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-or-later
"""
Capture golden fixtures from the LEGACY Python reference.

These fixtures are the oracle every ported subsystem is checked against
(memory/test.md section 3). A port without a captured fixture is a rewrite,
not a port, and is not accepted.

Output lands in tests/golden/<subsystem>/ as:
    *.bin           little-endian float32 or uint32 blobs
    *.json          scalar metadata
    MANIFEST.json   what was captured, from which reference commit, with which
                    interpreter and numpy -- so a fixture is never ambiguous
                    about what produced it.

Usage:
    ./.venv-mh/bin/python tools/capture_fixture.py --list
    ./.venv-mh/bin/python tools/capture_fixture.py mesh
    ./.venv-mh/bin/python tools/capture_fixture.py all
"""
from __future__ import annotations

import argparse
import hashlib
import json
import os
import subprocess
import sys
from datetime import datetime, timezone
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
LEGACY = REPO / "legacy-python"
GOLDEN = REPO / "tests" / "golden"

# The reference resolves system data via getSysPath() == "." and therefore
# requires CWD == the install dir (legacy-python/lib/getpath.py:224).
os.chdir(LEGACY)
for _p in ("", "lib", "core", "apps", "shared"):
    sys.path.insert(0, str(LEGACY / _p) if _p else str(LEGACY))

import numpy as np  # noqa: E402


# --------------------------------------------------------------------------- io
def _write_blob(path: Path, arr: np.ndarray, dtype: str) -> dict:
    """Writes a C-contiguous little-endian blob and returns its descriptor."""
    a = np.ascontiguousarray(arr, dtype=np.dtype(dtype).newbyteorder("<"))
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(a.tobytes())
    return {
        "file": path.name,
        "dtype": dtype,
        "shape": list(a.shape),
        "count": int(a.size),
        "sha256": hashlib.sha256(a.tobytes()).hexdigest(),
    }


def _reference_commit() -> str:
    try:
        return subprocess.check_output(
            ["git", "-C", str(REPO), "rev-parse", "HEAD"], text=True
        ).strip()
    except Exception:  # pragma: no cover - diagnostic path
        return "unknown"


def _manifest(subsystem: str, entries: dict, extra: dict | None = None) -> dict:
    return {
        "subsystem": subsystem,
        "captured_at": datetime.now(timezone.utc).isoformat(),
        "reference_commit": _reference_commit(),
        "python": sys.version.split()[0],
        "numpy": np.__version__,
        "platform": sys.platform,
        "entries": entries,
        **(extra or {}),
    }


def _finish(subsystem: str, entries: dict, extra: dict | None = None) -> None:
    out = GOLDEN / subsystem
    out.mkdir(parents=True, exist_ok=True)
    manifest = _manifest(subsystem, entries, extra)
    (out / "MANIFEST.json").write_text(json.dumps(manifest, indent=2))
    print(f"  -> {out.relative_to(REPO)}/  ({len(entries)} entries)")


# ---------------------------------------------------------------- subsystems
def capture_mesh() -> None:
    """Base mesh geometry: positions, faces, UVs, normals, face groups."""
    import files3d

    print("capturing: mesh")
    mesh = files3d.loadMesh("data/3dobjs/base.obj", maxFaces=8)
    out = GOLDEN / "mesh"
    out.mkdir(parents=True, exist_ok=True)

    entries = {
        "coord": _write_blob(out / "coord.bin", mesh.coord, "f4"),
        "fvert": _write_blob(out / "fvert.bin", mesh.fvert, "u4"),
        "vnorm": _write_blob(out / "vnorm.bin", mesh.vnorm, "f4"),
        "fnorm": _write_blob(out / "fnorm.bin", mesh.fnorm, "f4"),
        "group": _write_blob(out / "group.bin", mesh.group, "u2"),
    }
    if mesh.has_uv:
        entries["texco"] = _write_blob(out / "texco.bin", mesh.texco, "f4")
        entries["fuvs"] = _write_blob(out / "fuvs.bin", mesh.fuvs, "u4")

    names = [g.name for g in mesh._faceGroups]
    (out / "face_groups.json").write_text(json.dumps(names, indent=2))

    bbox = mesh.calcBBox()
    _finish(
        "mesh",
        entries,
        {
            "source": "data/3dobjs/base.obj",
            "vertex_count": int(len(mesh.coord)),
            "face_count": int(len(mesh.fvert)),
            "uv_count": int(len(mesh.texco)) if mesh.has_uv else 0,
            "verts_per_primitive": int(mesh.vertsPerPrimitive),
            "verts_per_face_for_export": int(mesh.vertsPerFaceForExport),
            "max_faces": int(mesh.MAX_FACES),
            "face_group_count": len(names),
            "bbox_min": [float(v) for v in bbox[0]],
            "bbox_max": [float(v) for v in bbox[1]],
        },
    )


def capture_targets() -> None:
    """A deterministic sample of morph targets, plus one applied result."""
    import algos3d
    import files3d

    print("capturing: targets")
    mesh = files3d.loadMesh("data/3dobjs/base.obj", maxFaces=8)
    out = GOLDEN / "targets"
    out.mkdir(parents=True, exist_ok=True)

    all_targets = sorted(str(p.relative_to(LEGACY)) for p in (LEGACY / "data/targets").rglob("*.target"))
    if not all_targets:
        print("  !! no .target files found; skipping")
        return

    # A fixed, evenly-spaced sample so the fixture is stable across runs.
    sample = [all_targets[i] for i in range(0, len(all_targets), max(1, len(all_targets) // 24))][:24]

    entries: dict = {}
    described = []
    for rel in sample:
        t = algos3d.getTarget(mesh, rel)
        stem = rel.replace("/", "_").replace(".target", "")
        entries[f"{stem}.verts"] = _write_blob(out / f"{stem}.verts.bin", t.verts, "u4")
        entries[f"{stem}.data"] = _write_blob(out / f"{stem}.data.bin", t.data, "f4")
        described.append({"path": rel, "affected_verts": int(len(t.verts))})

    # Apply the whole sample at 0.5 and capture the resulting positions --
    # this is the end-to-end check that matters.
    mesh.coord[...] = mesh.orig_coord
    for rel in sample:
        algos3d.getTarget(mesh, rel).apply(mesh, 0.5, update=False, calcNormals=False)
    entries["applied_coord"] = _write_blob(out / "applied_coord.bin", mesh.coord, "f4")

    (out / "sample.json").write_text(json.dumps(described, indent=2))
    _finish(
        "targets",
        entries,
        {
            "targets_on_disk": len(all_targets),
            "sampled": len(sample),
            "apply_weight": 0.5,
            "note": "applied_coord = orig_coord with every sampled target applied at 0.5, in listed order",
        },
    )


def capture_skeleton() -> None:
    """Default rig: bone order, rest matrices, and vertex weights."""
    import files3d
    import skeleton as mhskel

    print("capturing: skeleton")
    mesh = files3d.loadMesh("data/3dobjs/base.obj", maxFaces=8)

    # Skeleton.fromFile reaches for G.app.selectedHuman when no human is given
    # (skeleton.py:774, :1279). Headless we supply a minimal stand-in.
    from core import G

    class _StubHuman:
        def __init__(self, m):
            self.meshData = m

        def getRestposeCoordinates(self):
            return self.meshData.coord

    class _StubApp:
        pass

    app = _StubApp()
    app.selectedHuman = _StubHuman(mesh)
    G.app = app

    # Real callers pass the mesh OBJECT, not its coords
    # (core/mhmain.py:417, plugins/3_libraries_skeleton/skeletonlibrary.py:207).
    skel = mhskel.load("data/rigs/default.mhskel", mesh)
    skel.build()

    out = GOLDEN / "skeleton"
    out.mkdir(parents=True, exist_ok=True)

    bones = skel.getBones()
    rest_global = np.stack([b.matRestGlobal for b in bones]).astype(np.float32)
    rest_rel = np.stack([b.matRestRelative for b in bones]).astype(np.float32)

    entries = {
        "matRestGlobal": _write_blob(out / "rest_global.bin", rest_global, "f4"),
        "matRestRelative": _write_blob(out / "rest_relative.bin", rest_rel, "f4"),
    }
    (out / "bone_order.json").write_text(
        json.dumps([{"name": b.name, "parent": b.parent.name if b.parent else None} for b in bones], indent=2)
    )
    _finish(
        "skeleton",
        entries,
        {
            "source": "data/rigs/default.mhskel",
            "bone_count": len(bones),
            "joint_count": len(skel.joint_pos_idxs),
            "matrix_layout": "row-major storage, column vectors (v' = M*v), translation in M[:3,3]",
            "world": "Y-up, model faces +Z, right-handed",
        },
    )


def capture_subdiv() -> None:
    """One level of Catmull-Clark on the base mesh, from the reference."""
    import catmull_clark_subdivision as cks
    import files3d
    import material as _material

    print("capturing: subdiv")
    mesh = files3d.loadMesh("data/3dobjs/base.obj", maxFaces=8)

    # SubdivisionObject copies mesh.object (catmull_clark_subdivision.py:67),
    # which is a weakref property (module3d.py:459-464), so the stub must be
    # kept alive by a strong reference here.
    class _StubObject:
        def __init__(self):
            self.material = _material.Material()

    stub = _StubObject()
    mesh.object = stub
    assert mesh.object is not None

    sub = cks.createSubdivisionObject(mesh, None)
    sub.update_coords()

    out = GOLDEN / "subdiv"
    out.mkdir(parents=True, exist_ok=True)
    entries = {
        "coord": _write_blob(out / "coord.bin", sub.coord, "f4"),
        "fvert": _write_blob(out / "fvert.bin", sub.fvert, "u4"),
        "texco": _write_blob(out / "texco.bin", sub.texco, "f4"),
        "fuvs": _write_blob(out / "fuvs.bin", sub.fuvs, "u4"),
    }
    _finish(
        "subdiv",
        entries,
        {
            "source": "data/3dobjs/base.obj",
            "parent_verts": int(len(mesh.coord)),
            "parent_faces": int(len(mesh.fvert)),
            "verts": int(len(sub.coord)),
            "faces": int(len(sub.fvert)),
            "uvs": int(len(sub.texco)),
            "cbase": int(sub.cbase),
            "ebase": int(sub.ebase),
            "note": "layout: [0,cbase) base verts, [cbase,ebase) face points, [ebase,..) edge points",
        },
    )



def capture_character() -> None:
    """End-to-end: modifier values -> target stack -> final vertex positions.

    This is the fixture that validates the whole parameterisation chain at once.
    """
    import files3d
    import human as human_mod
    import humanmodifier

    print("capturing: character")

    # applyAllTargets drives a progress bar through G.app (shared/progress.py).
    # Headless there is no app, so supply the smallest stand-in that satisfies it.
    from core import G

    class _StubApp:
        def progress(self, *args, **kwargs):
            pass

        def callAsync(self, fn, *args):
            fn(*args)

        def callEvent(self, *args, **kwargs):
            pass

        def addSetting(self, *args, **kwargs):
            pass

        def getSetting(self, name):
            return {"realtimeUpdates": False, "realtimeNormalUpdates": False,
                    "realtimeFitting": False, "cameraAutoZoom": False}.get(name, False)

    G.app = _StubApp()

    mesh = files3d.loadMesh("data/3dobjs/base.obj", maxFaces=8)
    h = human_mod.Human(mesh)
    for f in ("modeling_modifiers.json", "measurement_modifiers.json",
              "bodyshapes_modifiers.json"):
        humanmodifier.loadModifiers("data/modifiers/" + f, h)

    # Parameter sets chosen to exercise: the neutral default, each macro axis at
    # both extremes, a plain shape slider on each side, and a mixed case.
    cases = [
        ("default", {}),
        ("male", {"macrodetails/Gender": 1.0}),
        ("female", {"macrodetails/Gender": 0.0}),
        ("baby", {"macrodetails/Age": 0.0}),
        ("old", {"macrodetails/Age": 1.0}),
        ("muscular", {"macrodetails-universal/Muscle": 1.0}),
        ("heavy", {"macrodetails-universal/Weight": 1.0}),
        ("tall", {"macrodetails-height/Height": 1.0}),
        ("short", {"macrodetails-height/Height": 0.0}),
        ("african", {"macrodetails/African": 1.0}),
        ("asian", {"macrodetails/Asian": 1.0}),
        ("head_age_incr", {"head/head-age-decr|incr": 1.0}),
        ("head_age_decr", {"head/head-age-decr|incr": -1.0}),
        ("mixed", {"macrodetails/Gender": 1.0, "macrodetails/Age": 0.8,
                   "macrodetails-universal/Muscle": 0.9,
                   "macrodetails-universal/Weight": 0.2,
                   "macrodetails-height/Height": 0.75,
                   "head/head-age-decr|incr": 0.5}),
    ]

    out = GOLDEN / "character"
    out.mkdir(parents=True, exist_ok=True)
    entries: dict = {}
    described = []

    for name, settings in cases:
        # Reset every modifier, then apply this case's values.
        for m in h.modifiers:
            m.resetValue()
        for full, val in settings.items():
            h.getModifier(full).setValue(val)
        h.applyAllTargets()

        entries[name] = _write_blob(out / (name + ".bin"), mesh.coord, "f4")
        stack = {os.path.relpath(k, os.path.abspath("data")): round(float(v), 6)
                 for k, v in h.targetsDetailStack.items()}
        described.append({"name": name, "settings": settings,
                          "stack_size": len(stack), "stack": stack})

    (out / "cases.json").write_text(json.dumps(described, indent=2, sort_keys=True))
    _finish("character", entries,
            {"source": "data/3dobjs/base.obj", "cases": len(cases),
             "vertex_count": int(len(mesh.coord)),
             "note": "each .bin is the full coord array after applyAllTargets for that case"})



def capture_proxy() -> None:
    """Proxy fitting: the barycentric result for each shipped proxy, on two bodies."""
    import files3d
    import human as human_mod
    import humanmodifier
    import proxy as proxy_mod

    print("capturing: proxy")
    from core import G

    class _Cam:
        def getRotation(self):
            return [0.0, 0.0, 0.0]

        translation = [0.0, 0.0, 0.0]
        zoomFactor = 1.0

    class _StubApp:
        modelCamera = _Cam()
        saveHandlers: list = []
        loadHandlers: dict = {}

        def progress(self, *args, **kwargs):
            pass

        def getSetting(self, name):
            return False

    G.app = _StubApp()

    mesh = files3d.loadMesh("data/3dobjs/base.obj", maxFaces=8)
    h = human_mod.Human(mesh)
    for f in ("modeling_modifiers.json", "measurement_modifiers.json",
              "bodyshapes_modifiers.json"):
        humanmodifier.loadModifiers("data/modifiers/" + f, h)
    G.app.selectedHuman = h

    proxies = ["data/eyes/high-poly/high-poly.mhclo",
               "data/eyes/low-poly/low-poly.mhclo",
               "data/3dobjs/base.mhclo"]

    # Two bodies, so the fit is exercised on more than the neutral shape --
    # the TMatrix rescaling only shows up when proportions change.
    bodies = [("neutral", {}),
              ("mixed", {"macrodetails/Gender": 1.0, "macrodetails/Age": 0.8,
                         "macrodetails-universal/Muscle": 0.9,
                         "macrodetails-height/Height": 0.75})]

    out = GOLDEN / "proxy"
    out.mkdir(parents=True, exist_ok=True)
    entries: dict = {}
    described = []

    for bodyName, settings in bodies:
        for m in h.modifiers:
            m.resetValue()
        for full, val in settings.items():
            h.getModifier(full).setValue(val)
        h.applyAllTargets()

        for rel in proxies:
            if not os.path.exists(rel):
                continue
            pxy = proxy_mod.loadTextProxy(h, rel)
            coords = pxy.getCoords()
            stem = os.path.basename(rel).replace(".mhclo", "").replace(".proxy", "")
            key = f"{stem}_{bodyName}"
            entries[key] = _write_blob(out / (key + ".bin"), coords, "f4")
            if bodyName == bodies[0][0]:
                described.append({"file": rel, "stem": stem,
                                  "vertices": int(len(coords)),
                                  "uuid": pxy.uuid or "",
                                  "z_depth": int(pxy.z_depth)})

    (out / "proxies.json").write_text(json.dumps(described, indent=2))
    _finish("proxy", entries,
            {"bodies": [b[0] for b in bodies], "proxies": len(described),
             "note": "each .bin is getCoords() for that proxy on that body"})


SUBSYSTEMS = {
    "mesh": capture_mesh,
    "subdiv": capture_subdiv,
    "targets": capture_targets,
    "skeleton": capture_skeleton,
    "character": capture_character,
    "proxy": capture_proxy,
}


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("subsystem", nargs="?", default="all", help="one of: " + ", ".join(SUBSYSTEMS) + ", or all")
    ap.add_argument("--list", action="store_true", help="list available subsystems and exit")
    args = ap.parse_args()

    if args.list:
        for name in SUBSYSTEMS:
            print(name)
        return 0

    todo = list(SUBSYSTEMS) if args.subsystem == "all" else [args.subsystem]
    unknown = [s for s in todo if s not in SUBSYSTEMS]
    if unknown:
        print(f"unknown subsystem(s): {', '.join(unknown)}", file=sys.stderr)
        return 2

    failed = []
    for name in todo:
        try:
            SUBSYSTEMS[name]()
        except Exception as e:
            # One failing subsystem must not lose the fixtures already captured.
            print(f"  !! {name} failed: {type(e).__name__}: {e}", file=sys.stderr)
            failed.append(name)

    if failed:
        print(f"\nfailed: {', '.join(failed)}", file=sys.stderr)
        return 1
    print("\nall fixtures captured")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
