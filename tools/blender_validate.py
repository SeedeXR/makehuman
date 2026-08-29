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

    # Blender's glTF importer creates its own helper geometry -- an Icosphere
    # used as the custom bone shape for every bone -- and parks it in a
    # collection named "glTF_not_exported". Counting it makes a correct rigged
    # export look like it has 42 stray vertices and a second mesh. Skip
    # anything Blender has marked as not-for-export.
    def _is_helper(obj) -> bool:
        return any(c.name == "glTF_not_exported" for c in obj.users_collection)

    meshes = [o for o in bpy.data.objects if o.type == "MESH" and not _is_helper(o)]
    armatures = [o for o in bpy.data.objects if o.type == "ARMATURE" and not _is_helper(o)]

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

    # Vertex groups are how Blender represents skin weights. A rigged mesh with
    # an armature but no groups is bound to nothing and will not deform -- which
    # looks fine in a static screenshot.
    vertex_groups = sum(len(o.vertex_groups) for o in meshes)

    # Shape keys are Blender's morph targets. key_blocks[0] is the Basis, so a
    # file with N morph targets reports N+1. Also count how many vertices each
    # key actually MOVES: a shape key that exists but displaces nothing is the
    # failure mode a name-only check cannot see.
    shape_keys = []
    for o in meshes:
        sk = o.data.shape_keys
        if not sk:
            continue
        basis = sk.key_blocks[0]
        for kb in sk.key_blocks[1:]:
            moved = sum(
                1
                for i, pt in enumerate(kb.data)
                if (pt.co - basis.data[i].co).length > 1e-6
            )
            shape_keys.append({"name": kb.name, "value": round(kb.value, 6), "moved": moved})
    skinned_verts = sum(
        1 for o in meshes for v in o.data.vertices if len(v.groups) > 0
    )

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
        "vertex_groups": vertex_groups,
        "shape_keys": shape_keys,
        "skinned_vertices": skinned_verts,
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
