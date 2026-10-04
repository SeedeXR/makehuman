#!/usr/bin/env python3
"""Assert that `--for-autorig` writes a file an auto-rigger will accept.

WHAT THIS EXISTS FOR. Mixamo and mesh2motion both BUILD a skeleton from the
geometry, so a file that already carries one is not a head start -- it is the
thing they refuse. Mixamo says "unable to map skeleton", which reads like a
mapping bug and actually means "this is already rigged".

Measured through mesh2motion on our own output before the flag existed:
3 meshes, 3 of them skinned, 179 bones -- and it graded `fail`.

THREE PROPERTIES, and the third is the one that was got wrong.

  1. ONE mesh. Eyes, teeth and tongue are separate objects, and these services
     require the file to hold the body and nothing else.
  2. NO skin. See above.
  3. THE POSE IS BAKED. A rigged format ships REST geometry and lets the
     armature carry the pose -- but with no armature there is nothing to carry
     it, so `--pose tpose` wrote an A-posed mesh and said nothing. Fitting a
     T-pose template to it put 44 of 66 joints outside the mesh: every arm and
     finger joint, both sides, while the torso and legs were fine.

     So this compares the t-posed export against the rest-posed one and
     requires them to DIFFER. Checking only "0 skins" passes on the broken
     file, which is exactly what happened.

glTF rather than FBX because its structure is readable without a parser, and
the property under test -- does the file carry a rig -- is the same either way.
"""
import json
import struct
import subprocess
import sys
import tempfile
from pathlib import Path


def gltf_json(path: Path) -> dict:
    """The JSON chunk of a .glb."""
    blob = path.read_bytes()
    if blob[:4] != b"glTF":
        raise SystemExit(f"{path} is not a .glb")
    length = struct.unpack("<I", blob[12:16])[0]
    return json.loads(blob[20 : 20 + length])


def export(app: str, out: Path, *extra: str) -> dict:
    run = subprocess.run([app, "--export", str(out), *extra],
                         capture_output=True, text=True)
    if run.returncode != 0 or not out.exists():
        raise SystemExit(f"export failed ({run.returncode}):\n{run.stderr[-2000:]}")
    return gltf_json(out)


def positions(doc: dict, path: Path) -> bytes:
    """The POSITION accessor's raw bytes, for comparing two exports."""
    blob = path.read_bytes()
    json_len = struct.unpack("<I", blob[12:16])[0]
    bin_start = 20 + json_len + 8          # skip the BIN chunk header
    acc = doc["accessors"][doc["meshes"][0]["primitives"][0]["attributes"]["POSITION"]]
    view = doc["bufferViews"][acc["bufferView"]]
    off = bin_start + view.get("byteOffset", 0)
    return blob[off : off + view["byteLength"]]


def main() -> int:
    if len(sys.argv) < 2:
        print("usage: check_autorig_export.py <makehuman>", file=sys.stderr)
        return 2
    app = sys.argv[1]
    problems = []

    with tempfile.TemporaryDirectory() as tmp:
        tmp = Path(tmp)
        rigged = tmp / "rigged.glb"
        auto = tmp / "auto.glb"
        auto_rest = tmp / "auto_rest.glb"

        # The CONTROL, first. If an ordinary export does not carry a rig then
        # the assertions below mean nothing -- they would pass on a build whose
        # exporter had stopped writing skins entirely.
        ordinary = export(app, rigged)
        if not ordinary.get("skins"):
            problems.append("an ORDINARY export carries no skin; this check "
                            "cannot tell the flag from a broken exporter")
        if len(ordinary.get("meshes", [])) < 2:
            problems.append("an ORDINARY export has one mesh; the proxies are "
                            "not being worn, so dropping them proves nothing")

        doc = export(app, auto, "--for-autorig", "--pose", "tpose")
        if doc.get("skins"):
            problems.append(f"--for-autorig wrote {len(doc['skins'])} skin(s); "
                            "an auto-rigger refuses a rigged mesh")
        if len(doc.get("meshes", [])) != 1:
            problems.append(f"--for-autorig wrote {len(doc.get('meshes', []))} meshes; "
                            "the file must hold the body and nothing else")

        rest = export(app, auto_rest, "--for-autorig")
        if positions(doc, auto) == positions(rest, auto_rest):
            problems.append("--pose tpose changed nothing under --for-autorig. "
                            "With no armature to carry it the pose must be BAKED "
                            "into the vertices; a T-pose template fitted to an "
                            "A-posed mesh puts every arm joint outside the body.")

    if problems:
        print("--for-autorig does not produce an auto-rigger-ready file:", file=sys.stderr)
        for p in problems:
            print(f"  {p}", file=sys.stderr)
        return 1

    print("--for-autorig: 1 mesh, no skin, pose baked")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
