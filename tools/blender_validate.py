# SPDX-License-Identifier: Apache-2.0
"""
Independent validation of exported files, run inside Blender headless.

Why this exists
---------------
Every other check in this project shares lineage with what it is checking:
our parity tests compare us against the Python reference we were ported from,
and our glTF/FBX tests read back through assimp, which also wrote the FBX.
A convention both sides get wrong the same way passes all of it.

Blender is a third implementation that has never seen this codebase. If it
agrees on counts, bounds and UVs, the file is very likely right; if it
disagrees, one of the two is wrong and the disagreement says where.

Usage (from the repo root):
    /Applications/Blender.app/Contents/MacOS/Blender --background \
        --python tools/blender_validate.py -- <file> [<file> ...]

Prints one JSON object per file to stdout, prefixed with BLENDER_VALIDATE:
so the caller can pick it out of Blender's own chatter.

Note on axes: Blender's world is Z-UP and every importer rotates a Y-up file on
the way in, so a human model's height arrives as Blender's Z. Compare
`tallest_extent`, not the Y component -- reading Y reports the body's depth and
looks like a unit error that is not there. (I made exactly that mistake the
first time this ran.)
"""
import json
import sys

import bpy


def _clear():
    bpy.ops.wm.read_factory_settings(use_empty=True)


def _import(path: str) -> None:
    lower = path.lower()
    if lower.endswith(".glb") or lower.endswith(".gltf"):
        bpy.ops.import_scene.gltf(filepath=path)
    elif lower.endswith(".fbx"):
        bpy.ops.import_scene.fbx(filepath=path)
    elif lower.endswith(".obj"):
        bpy.ops.wm.obj_import(filepath=path)
    elif lower.endswith(".dae"):
        bpy.ops.wm.collada_import(filepath=path)
    elif lower.endswith(".stl"):
        bpy.ops.wm.stl_import(filepath=path)
    else:
        raise RuntimeError("unsupported extension: " + path)


def describe(path: str) -> dict:
    _clear()
    _import(path)

    meshes = [o for o in bpy.data.objects if o.type == "MESH"]
    armatures = [o for o in bpy.data.objects if o.type == "ARMATURE"]

    verts = tris = 0
    uv_layers = 0
    lo = [1e30, 1e30, 1e30]
    hi = [-1e30, -1e30, -1e30]

    for o in meshes:
        m = o.data
        m.calc_loop_triangles()
        verts += len(m.vertices)
        tris += len(m.loop_triangles)
        uv_layers = max(uv_layers, len(m.uv_layers))
        for v in m.vertices:
            w = o.matrix_world @ v.co
            for i in range(3):
                lo[i] = min(lo[i], w[i])
                hi[i] = max(hi[i], w[i])

    bones = sum(len(a.data.bones) for a in armatures)

    # Blender's world is Z-UP, and every importer rotates a Y-up file on the way
    # in. So the model's height is Blender's Z, not its Y -- reading index 1
    # here reports the depth of the body and looks like a 4x unit error that is
    # not there. Report the extents and let the caller compare the largest.
    extents = [round(hi[i] - lo[i], 6) for i in range(3)] if meshes else None
    tallest = max(extents) if extents else None

    return {
        "file": path,
        "ok": True,
        "meshes": len(meshes),
        "vertices": verts,
        "triangles": tris,
        "uv_layers": uv_layers,
        "armatures": len(armatures),
        "bones": bones,
        "bbox_min": [round(v, 6) for v in lo] if meshes else None,
        "bbox_max": [round(v, 6) for v in hi] if meshes else None,
        "extents": extents,
        "tallest_extent": tallest,
        "materials": len(bpy.data.materials),
    }


def main() -> int:
    argv = sys.argv
    args = argv[argv.index("--") + 1:] if "--" in argv else []
    if not args:
        print("BLENDER_VALIDATE:" + json.dumps({"ok": False, "error": "no files given"}))
        return 2

    for path in args:
        try:
            result = describe(path)
        except Exception as exc:  # noqa: BLE001 - report, do not abort the batch
            result = {"file": path, "ok": False, "error": f"{type(exc).__name__}: {exc}"}
        print("BLENDER_VALIDATE:" + json.dumps(result))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
