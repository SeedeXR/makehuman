#!/usr/bin/env python3
# SPDX-License-Identifier: CC0-1.0
"""Regenerates tests/golden/scene/two_cubes.glb.

    blender -b --python tools/make_scene_fixture.py -- tests/golden/scene/two_cubes.glb

Two identical cubes whose ONLY difference is their node translation, which is
exactly what an importer that ignores the node graph gets wrong: both meshes
come back stacked at the origin and nothing errors.

The geometry is two Blender primitives -- no third-party asset -- so the fixture
is CC0 like the rest of `data/`.
"""
import bpy, sys
out = sys.argv[sys.argv.index("--")+1]
bpy.ops.wm.read_factory_settings(use_empty=True)
# Two cubes at known, DIFFERENT node translations. Same geometry, so the only
# thing distinguishing them in the file is the node transform.
for name, x in (("left", -5.0), ("right", 5.0)):
    bpy.ops.mesh.primitive_cube_add(size=2.0, location=(x, 0.0, 0.0))
    bpy.context.active_object.name = name
bpy.ops.export_scene.gltf(filepath=out, export_format='GLB', export_yup=False,
                          export_apply=False)
print("WROTE", out)
for o in bpy.data.objects:
    print("OBJ", o.name, tuple(round(v,2) for v in o.location))
