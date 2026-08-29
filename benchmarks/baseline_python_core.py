#!/usr/bin/env python3
"""
Baseline performance harness for the LEGACY Python MakeHuman core.

Measures the operations the C++/Qt port must beat. Runs headless (no Qt, no GL).
Numbers land in benchmarks/baseline_python.json and are the reference the
C++ benchmarks in benchmarks/ are compared against.

Run:  ../.venv-mh/bin/python benchmarks/baseline_python_core.py
"""
import json
import os
import statistics
import sys
import time
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
LEGACY = REPO / "legacy-python"

# MakeHuman resolves system data via getSysPath() == "." (legacy-python/lib/getpath.py:224)
os.chdir(LEGACY)
for p in ("", "lib", "core", "apps", "shared"):
    sys.path.insert(0, str(LEGACY / p) if p else str(LEGACY))

import numpy as np  # noqa: E402


def bench(label, fn, repeat=5, warmup=1):
    for _ in range(warmup):
        fn()
    samples = []
    for _ in range(repeat):
        t0 = time.perf_counter()
        fn()
        samples.append((time.perf_counter() - t0) * 1000.0)
    return {
        "label": label,
        "median_ms": round(statistics.median(samples), 3),
        "min_ms": round(min(samples), 3),
        "max_ms": round(max(samples), 3),
        "runs": repeat,
    }


def main():
    results = {
        "harness": "legacy python core",
        "python": sys.version.split()[0],
        "numpy": np.__version__,
        "platform": sys.platform,
        "results": [],
    }

    import files3d  # noqa: E402
    import module3d  # noqa: E402
    import algos3d  # noqa: E402

    obj_path = "data/3dobjs/base.obj"

    # --- 1. cold OBJ parse (no npz cache) -------------------------------
    npz = Path("data/3dobjs/base.npz")
    had_npz = npz.exists()
    if had_npz:
        npz.rename(npz.with_suffix(".npz.bak"))
    try:
        mesh_holder = {}

        def load_text():
            mesh_holder["m"] = files3d.loadMesh(obj_path, maxFaces=8)

        results["results"].append(bench("load base.obj (text parse + _update_faces)", load_text, repeat=3))
    finally:
        if had_npz:
            npz.with_suffix(".npz.bak").rename(npz)

    mesh = mesh_holder["m"]
    results["mesh"] = {
        "verts": int(len(mesh.coord)),
        "faces": int(len(mesh.fvert)),
        "uvs": int(len(mesh.texco)) if mesh.has_uv else 0,
        "verts_per_primitive": int(mesh.vertsPerPrimitive),
        "max_faces": int(mesh.MAX_FACES),
    }

    # --- 2. normals ------------------------------------------------------
    results["results"].append(
        bench("calcNormals full mesh (face+vertex)", lambda: mesh.calcNormals(1, 1), repeat=10)
    )
    results["results"].append(
        bench("calcFaceNormals only", lambda: mesh.calcFaceNormals(), repeat=10)
    )
    results["results"].append(
        bench("calcVertexNormals only", lambda: mesh.calcVertexNormals(), repeat=10)
    )

    # --- 3. index buffer rebuild ----------------------------------------
    results["results"].append(
        bench("updateIndexBuffer (unweld + group sort)", lambda: mesh.updateIndexBuffer(), repeat=10)
    )

    # --- 4. target load + apply ------------------------------------------
    tdir = Path("data/targets")
    target_files = sorted(str(p) for p in tdir.rglob("*.target"))
    results["targets_on_disk"] = len(target_files)

    sample = target_files[:200]

    def load_targets():
        algos3d._targetBuffer.clear()
        for t in sample:
            algos3d.getTarget(mesh, t)

    results["results"].append(bench(f"load {len(sample)} targets (text parse)", load_targets, repeat=3))

    loaded = [algos3d.getTarget(mesh, t) for t in sample]
    results["target_stats"] = {
        "sampled": len(loaded),
        "mean_affected_verts": round(statistics.mean(len(t.verts) for t in loaded), 1),
        "max_affected_verts": max(len(t.verts) for t in loaded),
    }

    def apply_targets():
        mesh.coord[...] = mesh.orig_coord
        for t in loaded:
            t.apply(mesh, 0.5, update=False, calcNormals=False)

    results["results"].append(
        bench(f"apply {len(loaded)} targets @0.5 (full stack rebuild)", apply_targets, repeat=10)
    )

    def apply_one():
        loaded[0].apply(mesh, 0.5, update=False, calcNormals=False)

    results["results"].append(bench("apply 1 target (slider delta)", apply_one, repeat=200))

    # --- 5. subdivision ---------------------------------------------------
    try:
        import catmull_clark_subdivision as cks

        # SubdivisionObject copies mesh.object (catmull_clark_subdivision.py:67),
        # which is the guicommon.Object wrapper the full app supplies. Object3D.object
        # is a *weakref* property (core/module3d.py:459-464), so the stub must be kept
        # alive by a strong reference here or it is collected before use.
        import material as _material

        class _StubObject:
            def __init__(self):
                self.material = _material.Material()

        stub = _StubObject()          # strong ref, must outlive the benchmark
        mesh.object = stub
        assert mesh.object is not None, "stub was garbage collected"

        sub_holder = {}

        def build_subdiv():
            sub_holder["s"] = cks.createSubdivisionObject(mesh, None)

        results["results"].append(bench("Catmull-Clark subdivide (build)", build_subdiv, repeat=3))
        sub = sub_holder["s"]
        results["subdiv_mesh"] = {"verts": int(len(sub.coord)), "faces": int(len(sub.fvert))}
        results["results"].append(
            bench("subdiv update_coords", lambda: sub.update_coords(), repeat=10)
        )
        results["results"].append(
            bench("subdiv calcNormals", lambda: sub.calcNormals(1, 1), repeat=10)
        )
    except Exception as e:  # pragma: no cover - diagnostic path
        results["subdiv_error"] = f"{type(e).__name__}: {e}"

    # --- 6. skeleton build + skinning -------------------------------------
    try:
        import skeleton as mhskeleton
        import animation

        skel_holder = {}

        def load_skel():
            s = mhskeleton.load("data/rigs/default.mhskel", mesh.coord)
            skel_holder["s"] = s

        results["results"].append(bench("load default.mhskel (163 bones)", load_skel, repeat=3))
        skel = skel_holder["s"]
        results["skeleton"] = {
            "bones": int(skel.getBoneCount()),
            "joints": len(skel.joint_pos_idxs),
        }

        results["results"].append(
            bench("skeleton.build() (rest matrices)", lambda: skel.build(), repeat=10)
        )

        vw = skel.getVertexWeights()
        results["results"].append(
            bench("compile vertex weights (6 influences)",
                  lambda: vw.compileData(skel, 6), repeat=3)
        )
        compiled = vw.compiled(6)

        # rest pose = identity skinning matrices, correct shape for skinMesh
        n_bones = skel.getBoneCount()
        pose = np.tile(np.eye(4, dtype=np.float32)[:3, :4], (n_bones, 1, 1))
        coords4 = np.ascontiguousarray(
            np.hstack([mesh.coord, np.ones((len(mesh.coord), 1), dtype=np.float32)])
        )

        results["results"].append(
            bench("skinMesh 19158 verts x 6 influences (CPU LBS)",
                  lambda: animation.skinMesh(coords4, compiled, pose), repeat=20)
        )
    except Exception as e:
        results["skeleton_error"] = f"{type(e).__name__}: {e}"

    out = REPO / "benchmarks" / "baseline_python.json"
    out.write_text(json.dumps(results, indent=2))

    print(f"mesh: {results['mesh']}")
    if "subdiv_mesh" in results:
        print(f"subdiv: {results['subdiv_mesh']}")
    if "target_stats" in results:
        print(f"targets: {results['target_stats']}")
    print()
    print(f"{'operation':<52} {'median':>10} {'min':>10} {'max':>10}")
    print("-" * 86)
    for r in results["results"]:
        print(f"{r['label']:<52} {r['median_ms']:>9.2f}ms {r['min_ms']:>9.2f}ms {r['max_ms']:>9.2f}ms")
    print(f"\nwritten: {out}")


if __name__ == "__main__":
    main()
