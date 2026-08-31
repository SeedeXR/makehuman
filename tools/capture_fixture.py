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
import math
import os
import subprocess
import sys
from datetime import datetime, timezone
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
LEGACY = REPO / "legacy/python"
GOLDEN = REPO / "tests" / "golden"

# The reference resolves system data via getSysPath() == "." and therefore
# requires CWD == the install dir (legacy/python/lib/getpath.py:224).
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


def capture_subdiv_masked() -> None:
    """Catmull-Clark with the static face mask, which is what the app does.

    `guicommon.py:433` passes `staticFaceMask` into `createSubdivisionObject`,
    and the docstring there is explicit that masked faces "are not included as
    geometry in this subdivision object (higher performance)". The unmasked
    capture above therefore does NOT exercise the path the application uses --
    it subdivides all 18,486 faces where the app subdivides 13,378.

    The mask is built exactly as `apps/human.py:274-289` builds it: hide any
    face whose group is named `joint-*` or `helper-*`.
    """
    import catmull_clark_subdivision as cks
    import files3d
    import material as _material
    import numpy as np

    print("capturing: subdiv_masked")
    mesh = files3d.loadMesh("data/3dobjs/base.obj", maxFaces=8)

    class _StubObject:
        def __init__(self):
            self.material = _material.Material()

    stub = _StubObject()
    mesh.object = stub

    group_mask = np.ones(len(mesh._faceGroups), dtype=bool)
    for g in mesh._faceGroups:
        g.name = str(g.name)
        if g.name.startswith("joint-") or g.name.startswith("helper-"):
            group_mask[g.idx] = False
    face_mask = group_mask[mesh.group]

    sub = cks.createSubdivisionObject(mesh, face_mask)
    sub.update_coords()

    out = GOLDEN / "subdiv_masked"
    out.mkdir(parents=True, exist_ok=True)
    entries = {
        "coord": _write_blob(out / "coord.bin", sub.coord, "f4"),
        "fvert": _write_blob(out / "fvert.bin", sub.fvert, "u4"),
        "texco": _write_blob(out / "texco.bin", sub.texco, "f4"),
        "fuvs": _write_blob(out / "fuvs.bin", sub.fuvs, "u4"),
    }
    _finish(
        "subdiv_masked",
        entries,
        {
            "source": "data/3dobjs/base.obj",
            "parent_verts": int(len(mesh.coord)),
            "parent_faces": int(len(mesh.fvert)),
            "visible_faces": int(face_mask.sum()),
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



def capture_weights() -> None:
    """Vertex bone weights: normalised per-bone lists, and the compiled 4-influence form.

    Two stages are captured because they fail differently:

      * `data` is the normalised per-bone mapping -- doubles merged, every
        vertex's weights summing to 1, sub-threshold entries dropped, and
        unweighted vertices assigned to the root bone.
      * `compiled(4)` is the per-vertex form an exporter or GPU needs: the four
        strongest influences, re-normalised after truncation.

    `_compileVertexWeights` only uses the skeleton for a name -> index lookup,
    so a stub built from the already-captured bone order serves; that order was
    itself captured from the reference, so this is not circular.
    """
    import animation

    print("capturing: weights")
    order = json.loads((GOLDEN / "skeleton" / "bone_order.json").read_text())
    names = [b["name"] for b in order]

    class _StubBone:
        def __init__(self, name):
            self.name = name

    class _StubSkel:
        def __init__(self, names):
            self._bones = [_StubBone(n) for n in names]

        def getBones(self):
            return self._bones

    vw = animation.VertexBoneWeights.fromFile(
        "data/rigs/default_weights.mhw", 19158, rootBone="root")

    out = GOLDEN / "weights"
    out.mkdir(parents=True, exist_ok=True)

    # Stage 1: the normalised per-bone mapping, flattened in bone order so the
    # C++ side can compare without depending on dict iteration order.
    bone_names, offsets, verts, wghts = [], [], [], []
    for bname in names:
        if bname not in vw.data:
            continue
        v, w = vw.data[bname]
        bone_names.append(bname)
        offsets.append(len(verts))
        verts.extend(int(x) for x in v)
        wghts.extend(float(x) for x in w)
    offsets.append(len(verts))

    entries = {
        "verts": _write_blob(out / "verts.bin", np.asarray(verts, dtype=np.uint32), "u4"),
        "weights": _write_blob(out / "weights.bin", np.asarray(wghts, dtype=np.float32), "f4"),
        "offsets": _write_blob(out / "offsets.bin", np.asarray(offsets, dtype=np.uint32), "u4"),
    }
    (out / "bone_names.json").write_text(json.dumps(bone_names, indent=2))

    # Stage 2: the compiled 4-influence form.
    compiled = vw._compileVertexWeights(vw.data, _StubSkel(names), 4, 19158)
    b4 = np.stack([compiled["b_idx%d" % (i + 1)] for i in range(4)], axis=1)
    w4 = np.stack([compiled["wght%d" % (i + 1)] for i in range(4)], axis=1)
    entries["compiled4_bones"] = _write_blob(out / "compiled4_bones.bin", b4, "u4")
    entries["compiled4_weights"] = _write_blob(out / "compiled4_weights.bin", w4, "f4")

    _finish("weights", entries, {
        "source": "data/rigs/default_weights.mhw",
        "vertex_count": int(vw.vertexCount),
        "bones_with_weights": len(bone_names),
        "max_weights_per_vertex": int(vw.getMaxNumberVertexWeights()),
        "weight_threshold": 1e-4,
        "root_bone": "root",
        "note": "compiled4 truncates to the 4 strongest influences and "
                "re-normalises; vertices with fewer keep bone index 0 and "
                "weight 0 in the unused slots.",
    })


def capture_mhm_save() -> None:
    """The reference's own Human.save() output, byte for byte.

    The two .mhm fixtures already in tests/golden/mhm were added without a
    generator, so nothing supported the claim that the reference wrote them --
    and both end with a newline, which `human.py:1640` does not emit. This
    drives the real thing so the writer can be checked against it, and includes
    a uuid, tags and plugin-contributed lines, none of which the old fixtures
    had.
    """
    import humanmodifier
    import files3d
    from core import G

    print("capturing: mhm_save")
    mesh = files3d.loadMesh("data/3dobjs/base.obj", maxFaces=8)

    class _StubCamera:
        def getRotation(self):
            return [12.0, 34.0, 0.0]

        translation = [1.0, 2.0, 3.0]
        zoomFactor = 1.5

    class _StubApp:
        modelCamera = _StubCamera()
        saveHandlers = []

        def progress(self, *args, **kwargs):
            pass

        def callAsync(self, fn, *args):
            fn(*args)

        def callEvent(self, *args, **kwargs):
            pass

        def addSetting(self, *args, **kwargs):
            pass

        def getSetting(self, name):
            return {}.get(name, False)

    G.app = _StubApp()

    import human as human_mod

    h = human_mod.Human(mesh)
    for f in ("modeling_modifiers.json", "measurement_modifiers.json",
              "bodyshapes_modifiers.json"):
        humanmodifier.loadModifiers("data/modifiers/" + f, h)

    h.setName("hero")
    h.setUuid("1234-abcd")
    # Deliberately mixed case, out of order, with a duplicate: the reference
    # lower-cases, truncates to 25 and stores in a set.
    for t in ("Zulu", "alpha", "MIKE", "alpha"):
        h.addTag(t)

    for name, value in (("head/head-oval", 0.4), ("macrodetails/Gender", 1.0)):
        h.getModifier(name).setValue(value)

    # A plugin-contributed line, so the unhandled write-back path is covered.
    def _handler(_human, f):
        f.write("skeleton default.mhskel")
        f.write("material Braid01 mhmat")

    G.app.saveHandlers = [_handler]

    out = GOLDEN / "mhm"
    out.mkdir(parents=True, exist_ok=True)
    target = out / "reference_save.mhm"
    h.save(str(target))

    raw = target.read_bytes()
    _finish("mhm_save", {"saved": {"file": "reference_save.mhm"}}, {
        "source": "the reference's Human.save()",
        "bytes": len(raw),
        "ends_with_newline": raw.endswith(b"\n"),
        "line_count": len(raw.decode("utf-8").split("\n")),
        "tags_line": next((l for l in raw.decode("utf-8").split("\n")
                           if l.startswith("tags ")), None),
    })


def capture_slider_layout() -> None:
    """The slider task-view registry: views, sections, order and every label.

    `modifiers/*_sliders.json` is the reference's task-view definition -- which
    sliders appear on which tab, in which section, in which order, and under what
    name. The label is only sometimes in the file; the rest are derived by a rule
    in `modifierslider.py:46-56`, and that rule is what this captures, applied by
    the reference itself to all 291 modifiers.
    """
    import json as _json
    from collections import OrderedDict

    import humanmodifier

    print("capturing: slider_layout")

    def guess_label(mod_name, group_name):
        """modifierslider.py:46-56, verbatim."""
        tlabel = mod_name.split('-')
        if "|" in tlabel[len(tlabel) - 1]:
            tlabel = tlabel[:-1]
        if len(tlabel) > 1 and tlabel[0] == group_name:
            label = tlabel[1:]
        else:
            label = tlabel
        return ' '.join([word.capitalize() for word in label])

    # Modifiers keyed by fullName, so the layout can be resolved against them.
    mods = {}
    for name in ("modeling_modifiers.json", "bodyshapes_modifiers.json",
                 "measurement_modifiers.json"):
        for m in humanmodifier.loadModifiers("data/modifiers/" + name, None):
            mods[m.fullName] = m

    out_views = OrderedDict()
    for fname in ("modeling_sliders.json", "bodyshapes_sliders.json",
                  "measurement_sliders.json"):
        data = _json.load(open("data/modifiers/" + fname, encoding="utf-8"),
                          object_pairs_hook=OrderedDict)
        for task_name, props in data.items():
            view = OrderedDict()
            view["file"] = fname
            view["sortOrder"] = props.get("sortOrder", None)
            view["cameraView"] = props.get("cameraView", None)
            view["saveName"] = props.get("saveName", None)
            sections = OrderedDict()
            for section_name, defs in props["modifiers"].items():
                entries = []
                for d in defs:
                    full = d["mod"]
                    m = mods.get(full)
                    label = d.get("label", None)
                    if label is None and m is not None:
                        label = guess_label(m.name, m.groupName)
                    entries.append(OrderedDict([
                        ("mod", full),
                        ("label", label),
                        ("cam", d.get("cam", None)),
                        ("known", m is not None),
                        ("min", float(m.getMin()) if m is not None else None),
                        ("max", float(m.getMax()) if m is not None else None),
                        ("default", float(m.getDefaultValue()) if m is not None else None),
                    ]))
                sections[section_name] = entries
            view["sections"] = sections
            out_views[task_name] = view

    out = GOLDEN / "slider_layout"
    out.mkdir(parents=True, exist_ok=True)
    (out / "layout.json").write_text(_json.dumps(out_views, indent=2))

    n_sections = sum(len(v["sections"]) for v in out_views.values())
    n_sliders = sum(len(e) for v in out_views.values() for e in v["sections"].values())
    n_unknown = sum(1 for v in out_views.values() for e in v["sections"].values()
                    for d in e if not d["known"])
    _finish("slider_layout", {"layout": {"file": "layout.json"}}, {
        "source": "data/modifiers/*_sliders.json + *_modifiers.json",
        "task_views": len(out_views),
        "sections": n_sections,
        "sliders": n_sliders,
        "sliders_with_no_modifier": n_unknown,
    })


def capture_body_pose() -> None:
    """The shipped T-pose applied to the default rig, through the reference.

    `capture_skinning` uses a synthetic pose because at the time nothing shipped
    that posed the rig -- but `data/poses/tpose.bvh` does, and it is the asset
    a user actually loads. This captures the reference's own answer for it, so
    "our T-pose looks about right" can be replaced with a number.
    """
    import animation
    import bvh
    import files3d
    import skeleton as mhskel

    print("capturing: body_pose")
    mesh = files3d.loadMesh("data/3dobjs/base.obj", maxFaces=8)

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

    skel = mhskel.load("data/rigs/default.mhskel", mesh)
    skel.build()
    bones = skel.getBones()

    # Exactly what plugins/3_libraries_pose.py:177-183 does for a .bvh pose.
    bvh_file = bvh.load("data/poses/tpose.bvh", convertFromZUp="auto")
    anim = bvh_file.createAnimationTrack(skel)
    posedata = anim.getAtFramePos(0, noBake=True)

    skel.setPose(posedata)
    pose_verts = np.stack([b.matPoseVerts for b in bones]).astype(np.float32)
    mat_pose = np.stack([b.matPose for b in bones]).astype(np.float32)

    vw = animation.VertexBoneWeights.fromFile(
        "data/rigs/default_weights.mhw", len(mesh.coord), rootBone="root")
    compiled = vw._compileVertexWeights(vw.data, skel, 4, len(mesh.coord))

    coords4 = np.concatenate(
        [mesh.coord, np.ones((len(mesh.coord), 1), dtype=np.float32)], axis=1)
    skinned = animation.skinMesh(coords4, compiled, pose_verts[:, :3, :4])

    out = GOLDEN / "body_pose"
    out.mkdir(parents=True, exist_ok=True)
    entries = {
        "matPose": _write_blob(out / "mat_pose.bin", mat_pose, "f4"),
        "skinned": _write_blob(out / "skinned.bin", skinned.astype(np.float32), "f4"),
    }

    # The measurement the C++ test makes, taken here too, so the two are
    # comparing the same quantity rather than two plausible ones.
    rest_span = mesh.coord.max(axis=0) - mesh.coord.min(axis=0)
    posed_span = skinned[:, :3].max(axis=0) - skinned[:, :3].min(axis=0)
    moved = np.linalg.norm(skinned[:, :3] - mesh.coord, axis=1)
    _finish("body_pose", entries, {
        "source": "data/poses/tpose.bvh + default.mhskel + default_weights.mhw",
        "bvh_joints": len(bvh_file.joints),
        "converted_from_z_up": bool(bvh_file.convertFromZUp),
        "bone_count": len(bones),
        "rest_span_all_verts": [float(v) for v in rest_span],
        "posed_span_all_verts": [float(v) for v in posed_span],
        "max_displacement": float(moved.max()),
        "vertices_moved": int(np.sum(moved > 1e-6)),
    })


def capture_skinning() -> None:
    """Linear blend skinning: a deterministic pose, its matrices, and the result.

    Three stages are captured because each can be wrong independently:

      * `matPose`   -- the local pose we set, so C++ starts from identical input
      * `matPoseVerts` -- matPoseGlobal * inv(matRestGlobal), per bone
      * `skinned`   -- the deformed vertices from the reference's own skinMesh

    The pose is synthetic and fixed: a handful of named bones rotated by set
    angles about set axes. Nothing shipped poses the rig, so a real pose asset
    is not available, and a random one would not be reproducible.
    """
    import animation
    import files3d
    import skeleton as mhskel

    print("capturing: skinning")
    mesh = files3d.loadMesh("data/3dobjs/base.obj", maxFaces=8)

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

    skel = mhskel.load("data/rigs/default.mhskel", mesh)
    skel.build()
    bones = skel.getBones()

    def _rot(axis, degrees):
        t = np.radians(degrees, dtype=np.float64)
        c, s = np.cos(t), np.sin(t)
        m = np.identity(4, dtype=np.float32)
        if axis == "x":
            m[1, 1], m[1, 2], m[2, 1], m[2, 2] = c, -s, s, c
        elif axis == "y":
            m[0, 0], m[0, 2], m[2, 0], m[2, 2] = c, s, -s, c
        else:
            m[0, 0], m[0, 1], m[1, 0], m[1, 1] = c, -s, s, c
        return m

    # Bones chosen to move large, well-separated parts of the body, so a wrong
    # parent chain or a transposed matrix shows up as a gross displacement
    # rather than a rounding difference.
    pose_spec = [
        ("upperarm01.L", "z", 35.0),
        ("upperarm01.R", "z", -35.0),
        ("lowerarm01.L", "x", 25.0),
        ("upperleg01.L", "x", -20.0),
        ("upperleg02.R", "x", 15.0),
        ("spine01", "y", 10.0),
        ("head", "y", -12.0),
    ]
    by_name = {b.name: b for b in bones}
    applied = []
    for bname, axis, deg in pose_spec:
        if bname not in by_name:
            continue
        by_name[bname].matPose = _rot(axis, deg)
        applied.append({"bone": bname, "index": bones.index(by_name[bname]),
                        "axis": axis, "degrees": deg})
    skel.update()

    mat_pose = np.stack([b.matPose for b in bones]).astype(np.float32)
    pose_verts = np.stack([b.matPoseVerts for b in bones]).astype(np.float32)

    vw = animation.VertexBoneWeights.fromFile(
        "data/rigs/default_weights.mhw", len(mesh.coord), rootBone="root")
    compiled = vw._compileVertexWeights(vw.data, skel, 4, len(mesh.coord))

    coords4 = np.concatenate(
        [mesh.coord, np.ones((len(mesh.coord), 1), dtype=np.float32)], axis=1)
    skinned = animation.skinMesh(coords4, compiled, pose_verts[:, :3, :4])

    out = GOLDEN / "skinning"
    out.mkdir(parents=True, exist_ok=True)
    entries = {
        "matPose": _write_blob(out / "mat_pose.bin", mat_pose, "f4"),
        "matPoseVerts": _write_blob(out / "pose_verts.bin", pose_verts, "f4"),
        "skinned": _write_blob(out / "skinned.bin", skinned.astype(np.float32), "f4"),
    }
    (out / "pose.json").write_text(json.dumps(applied, indent=2))

    moved = np.linalg.norm(skinned - mesh.coord, axis=1)
    _finish("skinning", entries, {
        "source": "data/rigs/default.mhskel + default_weights.mhw",
        "vertex_count": int(len(mesh.coord)),
        "bone_count": len(bones),
        "influences": 4,
        "posed_bones": len(applied),
        "max_displacement": float(moved.max()),
        "mean_displacement": float(moved.mean()),
        "vertices_moved": int(np.sum(moved > 1e-6)),
        "note": "matPoseVerts = matPoseGlobal * inv(matRestGlobal); skinning "
                "blends the MATRICES then applies once (accumulated matrix "
                "skinning), not the transformed positions.",
    })


def capture_transform() -> None:
    """Euler and quaternion conversions, over all 24 axis conventions.

    `core/transformations.py` is Christoph Gohlke's BSD-3-Clause library,
    vendored into MakeHuman (which then stamped its AGPL boilerplate into the
    module docstring -- see LICENSING.md). The maths is standard, but the 24
    conventions differ from each other only by an axis permutation, a parity
    flip and a static/rotating frame bit, so an implementation can be right for
    `sxyz` and wrong for the other 23. Every one is captured.
    """
    import transformations as tm

    print("capturing: transform")

    conventions = sorted(tm._AXES2TUPLE.keys())

    # Deliberately awkward angles: no zeros, no equal pairs, nothing near a
    # gimbal-lock singularity for the first set, and one set that IS near it.
    # The last two MUST use math.pi/2 exactly, not a truncated decimal.
    # _EPS is 8.9e-16; with 1.5707963 the guard value is ~1e-8 and the singular
    # branch is never reached, so a fixture written that way tests the
    # gimbal-lock path zero times while appearing to cover it. Verified: with
    # math.pi/2, 24 of 120 cases take the singular branch; with the decimal, 0.
    half_pi = math.pi / 2.0
    angle_sets = [
        (0.1, 0.2, 0.3),
        (-0.7, 1.1, 2.4),
        (2.9, -1.3, 0.45),
        (0.0, half_pi, 0.0),    # exactly +90 deg on the middle axis
        (1.2, -half_pi, -0.8),  # exactly -90 deg
    ]

    mats, quats, back = [], [], []
    for axes in conventions:
        for (ai, aj, ak) in angle_sets:
            m = tm.euler_matrix(ai, aj, ak, axes)
            mats.append(m)
            quats.append(tm.quaternion_from_matrix(m))
            back.append(tm.euler_from_matrix(m, axes))

    # Quaternion algebra, on values that are not unit-adjacent.
    q0 = tm.quaternion_from_matrix(tm.euler_matrix(0.3, -0.8, 1.7, "sxyz"))
    q1 = tm.quaternion_from_matrix(tm.euler_matrix(-1.1, 0.5, 0.9, "sxyz"))
    fractions = [0.0, 0.15, 0.5, 0.85, 1.0]
    slerps = [tm.quaternion_slerp(q0, q1, f) for f in fractions]
    product = tm.quaternion_multiply(q0, q1)

    # rotation_matrix about an arbitrary, non-axis-aligned direction.
    rot_axis = [0.3, -0.5, 0.81]
    rots = [tm.rotation_matrix(a, rot_axis) for a in (0.0, 0.6, -1.9, 3.14159)]

    out = GOLDEN / "transform"
    out.mkdir(parents=True, exist_ok=True)

    entries = {
        "euler_matrices": _write_blob(
            out / "euler_matrices.bin", np.stack(mats).astype(np.float64), "f8"),
        "quaternions": _write_blob(
            out / "quaternions.bin", np.stack(quats).astype(np.float64), "f8"),
        "euler_back": _write_blob(
            out / "euler_back.bin", np.asarray(back, dtype=np.float64), "f8"),
        "slerps": _write_blob(out / "slerps.bin", np.stack(slerps).astype(np.float64), "f8"),
        "product": _write_blob(out / "product.bin", np.asarray(product, dtype=np.float64), "f8"),
        "rotations": _write_blob(
            out / "rotations.bin", np.stack(rots).astype(np.float64), "f8"),
    }
    (out / "cases.json").write_text(json.dumps({
        "conventions": conventions,
        "angle_sets": [list(a) for a in angle_sets],
        "slerp_fractions": fractions,
        "slerp_q0": [float(x) for x in q0],
        "slerp_q1": [float(x) for x in q1],
        "rotation_axis": rot_axis,
        "rotation_angles": [0.0, 0.6, -1.9, 3.14159],
    }, indent=2))

    _finish("transform", entries, {
        "source": "legacy/python/core/transformations.py (BSD-3-Clause, C. Gohlke)",
        "convention_count": len(conventions),
        "angle_sets": len(angle_sets),
        "cases": len(conventions) * len(angle_sets),
        "quaternion_layout": "[w, x, y, z] -- scalar FIRST",
        "note": "float64 throughout: these are conversions, and capturing them "
                "at float32 would hide real error behind rounding.",
    })


def capture_bvh() -> None:
    """BVH poses: hierarchy, the Z-up conversion, and the per-frame matrices.

    The shipped pose data is BVH, not .mhpose -- there are no .mhpose files in
    data/ at all. tpose.bvh is a single frame; face-poseunits.bvh carries the
    60 facial pose units in 60 frames.

    Both auto-detect as **Z-up**, so the conversion is on for both: offsets
    become (x, z, -y), the Y and Z position channels swap with a sign flip, and
    the rotation order letters are remapped -- 'szyx' in the file becomes
    'syzx'. A reader that skips the guess produces a plausible skeleton lying on
    its side.
    """
    import bvh as bvh_mod

    print("capturing: bvh")
    out = GOLDEN / "bvh"
    out.mkdir(parents=True, exist_ok=True)

    entries: dict = {}
    described: list = []

    for src, stem in (("data/poses/tpose.bvh", "tpose"),
                      ("data/poseunits/face-poseunits.bvh", "faceposeunits")):
        b = bvh_mod.load(src)
        joints = b.getJoints()

        meta = [{
            "name": j.name,
            "parent": j.parent.name if j.parent else None,
            "channels": list(j.channels),
            "rotOrder": getattr(j, "rotOrder", ""),
            "isEndConnector": j.isEndConnector(),
        } for j in joints]
        (out / f"{stem}_joints.json").write_text(json.dumps(meta, indent=1))

        offsets = np.stack([j.offset for j in joints]).astype(np.float32)
        positions = np.stack([j.position for j in joints]).astype(np.float32)
        # matrixPoses is (frames, 3, 4) per joint -> (joints, frames, 3, 4)
        mats = np.stack([j.matrixPoses for j in joints]).astype(np.float32)

        entries[f"{stem}_offsets"] = _write_blob(out / f"{stem}_offsets.bin", offsets, "f4")
        entries[f"{stem}_positions"] = _write_blob(out / f"{stem}_positions.bin", positions, "f4")
        entries[f"{stem}_matrices"] = _write_blob(out / f"{stem}_matrices.bin", mats, "f4")

        described.append({
            "file": src,
            "stem": stem,
            "joints": len(joints),
            "frames": int(b.frameCount),
            "frame_time": float(b.frameTime),
            "convertFromZUp": bool(b.convertFromZUp),
            "allowTranslation": b.allowTranslation,
            "matrix_shape": list(mats.shape),
        })

    _finish("bvh", entries, {
        "sources": described,
        "note": "matrices are (joints, frames, 3, 4) -- the reference stores "
                "only the top 3 rows. Rotation order letters are remapped by "
                "the Z-up conversion: a file with Xrotation Yrotation Zrotation "
                "yields 'syzx', not 'szyx'.",
    })


def capture_poseunits() -> None:
    """Face pose units: the 60 named units mapped onto the rig, and a blend.

    `face-poseunits.bvh` carries 60 frames; `face-poseunits.json` names them via
    `framemapping`. Mapping onto the skeleton walks the rig's own 163 bones and
    takes each one's matching BVH joint by name, or identity where the BVH has
    none -- the BVH has 212 joints, the rig 163, and they are not the same set.

    The blend is the interesting part and is order-dependent:

        q_i  = slerp(REST, quat(pose_i), weight_i)
        quat = q2 * q1, then q_i * quat for each subsequent i

    Quaternion multiplication does not commute, so reordering the inputs
    changes the result. That is replicated, not corrected.
    """
    import animation
    import bvh as bvh_mod
    import files3d
    import skeleton as mhskel

    print("capturing: poseunits")
    mesh = files3d.loadMesh("data/3dobjs/base.obj", maxFaces=8)

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

    skel = mhskel.load("data/rigs/default.mhskel", mesh)
    skel.build()

    b = bvh_mod.load("data/poseunits/face-poseunits.bvh", allowTranslation="none")
    track = b.createAnimationTrack(skel, name="face-poseunits")

    names = json.loads(
        (REPO / "data/poseunits/face-poseunits.json").read_text(encoding="utf-8")
    )["framemapping"]
    if len(names) != b.frameCount:
        raise RuntimeError("framemapping/frame count mismatch")

    unit = animation.PoseUnit(track.name, track._data, names)

    # A deterministic blend. Chosen to be asymmetric (left/right differ) and to
    # use more than two units, so the multiplication ORDER matters.
    blend_names = ["LeftBrowDown", "RightOuterBrowUp", "NoseWrinkler",
                   "LeftUpperLidClosed", "CheeksPump"]
    blend_weights = [0.8, 0.35, 1.0, 0.6, 0.25]
    blended = unit.getBlendedPose(list(blend_names), list(blend_weights), only_data=True)

    # And the reverse order, to prove non-commutativity is real rather than
    # assumed -- if these matched, replicating the order would not matter.
    reversed_blend = unit.getBlendedPose(list(reversed(blend_names)),
                                         list(reversed(blend_weights)), only_data=True)

    out = GOLDEN / "poseunits"
    out.mkdir(parents=True, exist_ok=True)

    data = np.asarray(unit._data, dtype=np.float32)  # (frames*bones, 3, 4)
    entries = {
        "unit_data": _write_blob(out / "unit_data.bin", data, "f4"),
        "blended": _write_blob(out / "blended.bin", np.asarray(blended, dtype=np.float32), "f4"),
        "blended_reversed": _write_blob(
            out / "blended_reversed.bin", np.asarray(reversed_blend, dtype=np.float32), "f4"),
    }
    (out / "cases.json").write_text(json.dumps({
        "names": names,
        "bone_count": int(skel.getBoneCount()),
        "frame_count": int(b.frameCount),
        "blend_names": blend_names,
        "blend_weights": blend_weights,
    }, indent=2))

    delta = float(np.abs(np.asarray(blended) - np.asarray(reversed_blend)).max())
    _finish("poseunits", entries, {
        "source": "data/poseunits/face-poseunits.{bvh,json} + data/rigs/default.mhskel",
        "units": len(names),
        "bones": int(skel.getBoneCount()),
        "unit_data_shape": list(data.shape),
        "blend_order_matters_by": delta,
        "note": "blend is additive by default (weights are NOT normalised) and "
                "order-dependent: quat = q_i * quat, left-multiplied.",
    })


def capture_mask() -> None:
    """Face hiding: vertex mask -> face mask -> filtered index buffer.

    No shipped proxy declares `delete_verts` (verified: 0 hits across all four
    shipped .mhclo/.proxy files), so the mask cannot be sourced from an asset.
    These masks are synthetic and deterministic, chosen to pin the one rule
    that is easy to get backwards.

    `changeVertexMask` (guicommon.py:532-557) derives the face mask via
    `getFaceMaskForVertices` (module3d.py:1149-1159), which marks every face
    incident to a *visible* vertex. So a face survives if ANY of its corners is
    still visible, and dies only when ALL of them are hidden -- the docstring's
    "a face is masked if all of the vertices that define it are masked".

    `stride` exists to make that falsifiable: hiding every 7th vertex leaves
    almost no face with all four corners hidden, so the "any" rule keeps nearly
    every face while an "all" rule would delete most of the mesh. The two
    readings cannot both pass.
    """
    import files3d

    print("capturing: mask")
    mesh = files3d.loadMesh("data/3dobjs/base.obj", maxFaces=8)
    nverts = len(mesh.coord)

    # Visible = True, matching the reference's convention (guicommon.py:536-538).
    ys = mesh.coord[:, 1]
    masks = {
        # Nothing hidden: the filtered buffer must equal the unfiltered one.
        "none": np.ones(nverts, dtype=bool),
        # A contiguous region with a real boundary -- the interesting edge is
        # the ring of faces straddling it.
        "upper": ys <= float(np.median(ys)),
        # Scattered single vertices: discriminates "any" from "all" (see above).
        "stride": (np.arange(nverts) % 7) != 0,
        # Degenerate: everything hidden, so the index buffer must go empty.
        "all": np.zeros(nverts, dtype=bool),
    }

    out = GOLDEN / "mask"
    out.mkdir(parents=True, exist_ok=True)
    entries: dict = {}
    described: list = []

    for name, vertsMask in masks.items():
        verts = np.argwhere(vertsMask)[..., 0]
        face_mask = mesh.getFaceMaskForVertices(verts)

        mesh.changeFaceMask(face_mask)
        mesh.updateIndexBufferFaces()

        entries[f"{name}_vertmask"] = _write_blob(
            out / f"{name}_vertmask.bin", vertsMask.astype(np.uint8), "u1")
        entries[f"{name}_facemask"] = _write_blob(
            out / f"{name}_facemask.bin", face_mask.astype(np.uint8), "u1")
        entries[f"{name}_index"] = _write_blob(
            out / f"{name}_index.bin", mesh.index, "u4")
        entries[f"{name}_grpix"] = _write_blob(
            out / f"{name}_grpix.bin", mesh.grpix, "u4")

        described.append({
            "name": name,
            "visible_verts": int(np.count_nonzero(vertsMask)),
            "hidden_verts": int(nverts - np.count_nonzero(vertsMask)),
            "visible_faces": int(np.count_nonzero(face_mask)),
            "hidden_faces": int(len(face_mask) - np.count_nonzero(face_mask)),
            "index_count": int(mesh.index.size),
            "group_range_count": int(mesh.grpix.shape[0]),
        })

    _finish("mask", entries, {
        "source": "data/3dobjs/base.obj",
        "vertex_count": int(nverts),
        "face_count": int(len(mesh.fvert)),
        "note": "index is QUAD corners here (reference submits GL_QUADS); the "
                "C++ side triangulates, so tests compare face-mask parity "
                "exactly and index counts through the quad->tri relation.",
        "masks": described,
    })


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

    # The fit's only body-dependent term is the TMatrix scale, and for the eye
    # proxies that scale is read off HEAD vertices:
    #     x_scale 5399 11998 / y_scale 791 881 / z_scale 962 5320
    # So the shapes worth capturing are the ones that move head proportions,
    # not merely the ones that look different. The macro extremes bracket the
    # range, and head-scale-* drives exactly the three axes the matrix measures.
    bodies = [("neutral", {}),
              ("mixed", {"macrodetails/Gender": 1.0, "macrodetails/Age": 0.8,
                         "macrodetails-universal/Muscle": 0.9,
                         "macrodetails-height/Height": 0.75}),
              # Age 0.0 is an infant: the largest head-to-body ratio the model
              # produces, and the far end of the scale numerator.
              ("extreme_min", {"macrodetails/Gender": 0.0, "macrodetails/Age": 0.0,
                               "macrodetails-universal/Muscle": 0.0,
                               "macrodetails-universal/Weight": 0.0,
                               "macrodetails-height/Height": 0.0,
                               "macrodetails-proportions/BodyProportions": 0.0}),
              ("extreme_max", {"macrodetails/Gender": 1.0, "macrodetails/Age": 1.0,
                               "macrodetails-universal/Muscle": 1.0,
                               "macrodetails-universal/Weight": 1.0,
                               "macrodetails-height/Height": 1.0,
                               "macrodetails-proportions/BodyProportions": 1.0}),
              # Straight at the three axes the TMatrix divides by.
              ("head_small", {"head/head-scale-depth-decr|incr": -1.0,
                              "head/head-scale-horiz-decr|incr": -1.0,
                              "head/head-scale-vert-decr|incr": -1.0}),
              ("head_large", {"head/head-scale-depth-decr|incr": 1.0,
                              "head/head-scale-horiz-decr|incr": 1.0,
                              "head/head-scale-vert-decr|incr": 1.0})]

    out = GOLDEN / "proxy"
    out.mkdir(parents=True, exist_ok=True)
    entries: dict = {}
    described = []
    scales: dict = {}

    def stem_of(rel):
        return os.path.basename(rel).replace(".mhclo", "").replace(".proxy", "")

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
            mtx = pxy.tmatrix.getMatrix(h.getRestposeCoordinates())
            scales.setdefault(bodyName, {})[stem_of(rel)] = \
                [float(mtx[0][0]), float(mtx[1][1]), float(mtx[2][2])]
            stem = os.path.basename(rel).replace(".mhclo", "").replace(".proxy", "")
            key = f"{stem}_{bodyName}"
            entries[key] = _write_blob(out / (key + ".bin"), coords, "f4")
            if bodyName == bodies[0][0]:
                described.append({"file": rel, "stem": stem,
                                  "vertices": int(len(coords)),
                                  "uuid": pxy.uuid or "",
                                  "z_depth": int(pxy.z_depth)})

    (out / "proxies.json").write_text(json.dumps(described, indent=2))
    (out / "tmatrix_scales.json").write_text(json.dumps(scales, indent=2, sort_keys=True))
    _finish("proxy", entries,
            {"bodies": [b[0] for b in bodies], "proxies": len(described),
             "note": "each .bin is getCoords() for that proxy on that body",
             "tmatrix_scales": "tmatrix_scales.json -- the per-body scale diagonal, "
                               "so a body that exercises nothing new is visible"})


SUBSYSTEMS = {
    "mesh": capture_mesh,
    "subdiv": capture_subdiv,
    "subdiv_masked": capture_subdiv_masked,
    "targets": capture_targets,
    "skeleton": capture_skeleton,
    "character": capture_character,
    "proxy": capture_proxy,
    "mask": capture_mask,
    "weights": capture_weights,
    "skinning": capture_skinning,
    "body_pose": capture_body_pose,
    "slider_layout": capture_slider_layout,
    "mhm_save": capture_mhm_save,
    "transform": capture_transform,
    "bvh": capture_bvh,
    "poseunits": capture_poseunits,
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
