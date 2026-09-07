# SPDX-License-Identifier: Apache-2.0
"""Independent validation of exported files, run inside Maya (`mayapy`).

Why a SECOND third-party reader
-------------------------------
`blender_validate.py` already reads what we write with an implementation that
has never seen this codebase. Maya adds something Blender cannot: for FBX it is
**Autodesk's own** importer, the reference implementation of the format we are
writing. When the two disagree about an FBX, Blender is the one to doubt.

It answers one question Blender can only answer by inference: is a rigged file a
LIVE RIG or a baked statue? A skinCluster's input ("Orig") shape is the geometry
BEFORE the deformer runs. If that already has the posed silhouette, the pose is
baked into the vertices and the skeleton is decoration -- which is what a user
discovers when they try to re-pose the character and nothing moves.

Measured 2026-09-07: our .glb ships rest geometry (arms down) with a posed
armature, and our .fbx -- written through assimp -- ships the posed geometry
with an inert one. Same final shape, and only one of them can be re-posed.

Usage (from the repo root):
    /Applications/Autodesk/maya2027/Maya.app/Contents/bin/mayapy \
        tools/maya_validate.py <file> [<file> ...]

Prints one JSON object per file, prefixed with MAYA_VALIDATE: so the caller can
pick it out of Maya's own chatter.
"""
import json
import sys

import maya.standalone

maya.standalone.initialize(name="python")

# Imported after initialize() on purpose: maya.cmds does not exist until the
# standalone interpreter is up.
import maya.cmds as cmds


#: Maya's "every vertex" component selector.
ALL_VERTS = ".vtx[*]"


def _extent(points):
    """The bounding-box size of a flat [x,y,z,x,y,z,...] list."""
    lo = [1e30] * 3
    hi = [-1e30] * 3
    for i in range(0, len(points), 3):
        for a in range(3):
            lo[a] = min(lo[a], points[i + a])
            hi[a] = max(hi[a], points[i + a])
    return [round(hi[a] - lo[a], 4) for a in range(3)]


def _import(path):
    cmds.file(new=True, force=True)
    cmds.loadPlugin("fbxmaya", quiet=True)
    cmds.file(path, i=True, ignoreVersion=True, ra=True,
              mergeNamespacesOnClash=False, pr=True)


def _rest_and_deformed():
    """Extents before and after the skin deformer, or (None, None).

    The intermediate ("Orig") mesh is what the skinCluster reads; the visible
    mesh is what it produces. Comparing them is the whole point of this file.
    """
    rest = None
    deformed = None
    for sc in cmds.ls(type="skinCluster") or []:
        for g in cmds.skinCluster(sc, q=True, geometry=True) or []:
            pts = cmds.xform(g + ALL_VERTS, q=True, ws=True, t=True)
            deformed = _extent(pts)
    for orig in cmds.ls(type="mesh", long=True, intermediateObjects=True) or []:
        if not isinstance(cmds.polyEvaluate(orig, vertex=True), int):
            continue
        pts = cmds.xform(orig + ALL_VERTS, q=True, os=True, t=True)
        rest = _extent(pts)
    return rest, deformed


def _deforms_when_posed():
    """Does rotating a joint actually move the mesh?

    The positive control this file needs. `live_rig: false` on its own could
    mean the skin is broken, the weights are missing, or the probe is reading
    the wrong shape -- all of which look identical to "the pose is baked". If
    the mesh MOVES when a joint is turned, the skinCluster is real and bound,
    and `live_rig: false` means what it says: the geometry simply arrived
    already posed.
    """
    joints = cmds.ls(type="joint", long=True) or []
    clusters = cmds.ls(type="skinCluster") or []
    if not joints or not clusters:
        return None
    geo = (cmds.skinCluster(clusters[0], q=True, geometry=True) or [None])[0]
    if geo is None:
        return None

    before = _extent(cmds.xform(geo + ALL_VERTS, q=True, ws=True, t=True))

    # The joint with the most DESCENDANTS, below the root. That is a limb or
    # spine root, and turning it moves a large part of the body.
    #
    # Picking "some joint with a parent" is not good enough, and the first
    # version of this proved it: it landed on `orbicularis04.L`, an eyelid
    # muscle whose 45 degrees moves a few face vertices -- far under the
    # centimetre threshold on a 168 cm figure -- and reported the rig as dead.
    best = None
    best_count = -1
    for j in joints:
        if not cmds.listRelatives(j, parent=True, type="joint"):
            continue
        kids = cmds.listRelatives(j, allDescendents=True, type="joint") or []
        if len(kids) > best_count:
            best, best_count = j, len(kids)
    # `> best_count` starting from -1, not 0: a two-joint rig has exactly one
    # joint with a parent and it has no descendants, so starting from 0 chose
    # nothing and reported `None` -- which reads as "no rig" for a rig that is
    # perfectly fine.
    if best is None:
        return None

    cmds.setAttr(best + ".rotateX", 45.0)
    # Batch Maya does not push the graph on its own, and reading without this
    # returns the values from before the rotation -- which looks exactly like a
    # rig that does not deform.
    cmds.dgdirty(allPlugs=True)
    cmds.dgeval(geo + ".outMesh")
    after = _extent(cmds.xform(geo + ALL_VERTS, q=True, ws=True, t=True))
    return max(abs(a - b) for a, b in zip(before, after)) > 1.0


def _shading():
    """UV sets, materials and file textures, as Maya resolves them.

    Blender reports UV layers too, but Maya is the reference implementation of
    this format: if the two disagree about whether a texture arrived, Maya is
    the one to believe.
    """
    meshes = [m for m in (cmds.ls(type="mesh", long=True, noIntermediate=True) or [])
              if isinstance(cmds.polyEvaluate(m, vertex=True), int)]
    uv_sets = 0
    for m in meshes:
        names = cmds.polyUVSet(m, query=True, allUVSets=True) or []
        uv_sets = max(uv_sets, len(names))
    files = cmds.ls(type="file") or []
    return {
        "uv_sets": uv_sets,
        # NAMES, not a count. An empty Maya scene already ships defaults --
        # `lambert1` and friends -- and which ones depends on the version, so a
        # count is a number nobody can check. A name is checkable: the caller
        # asserts that the material WE wrote is in the list.
        "material_names": sorted(cmds.ls(materials=True) or []),
        "file_textures": [cmds.getAttr(f + ".fileTextureName") for f in files],
    }


def describe(path):
    _import(path)
    # NOT intermediate shapes. A skinCluster creates an "Orig" mesh holding the
    # pre-deformation geometry, so counting every `mesh` node reports a rigged
    # file as twice the vertices it has -- which it did, until this filter.
    meshes = [m for m in (cmds.ls(type="mesh", long=True, noIntermediate=True) or [])
              if isinstance(cmds.polyEvaluate(m, vertex=True), int)]
    verts = sum(cmds.polyEvaluate(m, vertex=True) for m in meshes)
    rest, deformed = _rest_and_deformed()

    # A live rig is one where the deformer CHANGES something. The threshold is
    # generous on purpose: float32 storage and Maya's own unit conversion move
    # the last decimal, and 1 cm on a 170 cm figure is far below any real pose.
    live = None
    if rest is not None and deformed is not None:
        live = max(abs(r - d) for r, d in zip(rest, deformed)) > 1.0

    return {
        "file": path,
        "ok": True,
        "meshes": len(meshes),
        "vertices": verts,
        "joints": len(cmds.ls(type="joint") or []),
        "skin_clusters": len(cmds.ls(type="skinCluster") or []),
        "rest_extent": rest,
        "deformed_extent": deformed,
        "live_rig": live,
        "deforms_when_posed": _deforms_when_posed(),
        **_shading(),
    }


def main():
    for path in sys.argv[1:]:
        try:
            out = describe(path)
        # Report a bad file and carry on: one unreadable export must not hide
        # the results for every other one in the batch.
        except Exception as exc:
            out = {"file": path, "ok": False, "error": str(exc)}
        print("MAYA_VALIDATE:" + json.dumps(out))
    return 0


if __name__ == "__main__":
    sys.exit(main())
