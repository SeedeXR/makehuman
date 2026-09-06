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
    if lower.endswith((".glb", ".gltf")):
        bpy.ops.import_scene.gltf(filepath=path)
    elif lower.endswith(".fbx"):
        bpy.ops.import_scene.fbx(filepath=path)
    elif lower.endswith(".obj"):
        bpy.ops.wm.obj_import(filepath=path)
    elif lower.endswith(".dae"):
        bpy.ops.wm.collada_import(filepath=path)
    elif lower.endswith(".stl"):
        bpy.ops.wm.stl_import(filepath=path)
    elif lower.endswith((".usda", ".usdc", ".usdz")):
        bpy.ops.wm.usd_import(filepath=path)
    else:
        raise RuntimeError("unsupported extension: " + path)


def _is_helper(obj) -> bool:
    """Blender's glTF importer creates its own helper geometry -- an Icosphere
    used as the custom bone shape for every bone -- and parks it in a collection
    named "glTF_not_exported". Counting it makes a correct rigged export look
    like it has 42 stray vertices and a second mesh."""
    return any(c.name == "glTF_not_exported" for c in obj.users_collection)


def _geometry(meshes):
    """Vertex/triangle counts, UV layer count and the world-space bounds."""
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
    return verts, tris, uv_layers, lo, hi


def _uv_anchor(w):
    """Sort key picking one vertex out of a world-space extreme, unambiguously.

    `-z` first, then x and y, because ties are the norm rather than the
    exception: the crown of the head carries a left/right pair at the same
    height and both feet sit flat at z=0. Taking whichever tied vertex an
    importer happened to list first reported base.fbx at u 0.9205 against
    base.obj at u 0.9112 -- mirrored about the seam, v identical to 1e-6. That
    reads as a convention bug and is not one.
    """
    return (-round(w[2], 5), round(w[0], 5), round(w[1], 5))


def _uv_extremes(meshes):
    """EVERY UV on the highest vertex in world space.

    All of them, sorted, not "the first one". A vertex on a UV seam carries
    several, and which one an importer lists first is its own business -- the
    top of the head is on a seam and has two, (0.660913, 0.421668) and
    (0.660913, 0.544667). Reporting one produced a false alarm twice: once on
    base.fbx against base.obj, and once on a Draco GLB against a plain one,
    where it looked like a mirrored V and was a different corner of the same
    seam.

    `uv_layers` only counts layers. It cannot see a mirrored V, which is the
    one UV mistake an exporter actually makes: glTF puts (0,0) at the image's
    UPPER-left while OBJ, USD, FBX and Blender itself put it at the lower-left,
    so a writer either flips V or ships every texture upside down. Both files
    look equally valid to a counter.

    Anchoring on world POSITION rather than a vertex index is what makes this
    comparable across formats: the importers renumber vertices, but the top of
    the head is the top of the head in both.
    """
    # Grouped by anchor, not "best so far". A seam is TWO vertices at one
    # position after a glTF import -- the format unwelds -- so the winner of the
    # tie is again whichever the importer listed first, and picking it puts us
    # back where we started.
    at_anchor = {}
    for o in meshes:
        m = o.data
        if not m.uv_layers:
            continue
        uv = m.uv_layers[0].data
        for poly in m.polygons:
            for li in poly.loop_indices:
                vi = m.loops[li].vertex_index
                key = _uv_anchor(o.matrix_world @ m.vertices[vi].co)
                at_anchor.setdefault(key, set()).add(
                    (round(uv[li].uv[0], 5), round(uv[li].uv[1], 5)))
    if not at_anchor:
        return {"highest": None}
    best = min(at_anchor)
    return {"highest": {"z": -best[0], "uv": [list(p) for p in sorted(at_anchor[best])]}}


def _shape_keys(meshes):
    """Blender's morph targets. key_blocks[0] is the Basis, so a file with N
    morph targets reports N+1 -- hence the [1:]. Also counts how many vertices
    each key actually MOVES: a shape key that exists but displaces nothing is
    the failure a name-only check cannot see."""
    out = []
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
            out.append({"name": kb.name, "value": round(kb.value, 6), "moved": moved})
    return out


def _evaluated_extents(meshes, armatures):
    """The bounds of the mesh AFTER the armature is applied, or None.

    For a LIVE RIG this is the whole point: the file ships rest geometry and the
    consumer deforms it, so these numbers are a third party independently
    computing our skinning. Compare them against what we would have baked.
    """
    if not armatures or not meshes:
        return None
    dg = bpy.context.evaluated_depsgraph_get()
    lo = [1e30, 1e30, 1e30]
    hi = [-1e30, -1e30, -1e30]
    for o in meshes:
        ev = o.evaluated_get(dg)
        me = ev.to_mesh()
        for v in me.vertices:
            w = o.matrix_world @ v.co
            for i in range(3):
                lo[i] = min(lo[i], w[i])
                hi[i] = max(hi[i], w[i])
        ev.to_mesh_clear()
    return [round(hi[i] - lo[i], 4) for i in range(3)]


def _armature_shift(meshes, armatures) -> float:
    """Does applying the armature CHANGE the mesh?

    For every export we write, it must not: the exported skeleton is in the same
    state as the exported geometry, so skinning = global * inverse(bind) =
    identity. GltfWriter derives the node transforms and the inverse-bind
    matrices from ONE array to guarantee it (GltfWriter.cpp:330-374).

    A file that fails here is double-deformed in every DCC, while our own tests
    -- which never apply an armature -- all stay green.

    Measured as the largest vertex displacement between the evaluated mesh
    (modifiers applied, armature included) and the raw one.
    """
    if not armatures:
        return 0.0
    worst = 0.0
    dg = bpy.context.evaluated_depsgraph_get()
    for o in meshes:
        raw = o.data.vertices
        ev = o.evaluated_get(dg)
        me = ev.to_mesh()
        if len(me.vertices) == len(raw):
            worst = max(worst, max((a.co - b.co).length for a, b in zip(me.vertices, raw)))
        ev.to_mesh_clear()
    return worst


def describe(path: str) -> dict:
    _clear()
    _import(path)

    meshes = [o for o in bpy.data.objects if o.type == "MESH" and not _is_helper(o)]
    armatures = [o for o in bpy.data.objects if o.type == "ARMATURE" and not _is_helper(o)]

    verts, tris, uv_layers, lo, hi = _geometry(meshes)

    # Vertex groups are how Blender represents skin weights. A rigged mesh with
    # an armature but no groups is bound to nothing and will not deform -- which
    # looks fine in a static screenshot.
    vertex_groups = sum(len(o.vertex_groups) for o in meshes)
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
        "armature_shift": round(_armature_shift(meshes, armatures), 6),
        "evaluated_extents": _evaluated_extents(meshes, armatures),
        "meshes": len(meshes),
        "vertices": verts,
        "triangles": tris,
        "uv_layers": uv_layers,
        "uv_extremes": _uv_extremes(meshes),
        "armatures": len(armatures),
        "bones": sum(len(a.data.bones) for a in armatures),
        "vertex_groups": vertex_groups,
        "shape_keys": _shape_keys(meshes),
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
        # Broad by intent: one bad file must not abort the batch, and the
        # exception type is reported in the JSON line below.
        except Exception as exc:  # noqa: BLE001
            result = {"file": path, "ok": False, "error": f"{type(exc).__name__}: {exc}"}
        print("BLENDER_VALIDATE:" + json.dumps(result))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
