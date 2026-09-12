#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-or-later
"""Generates textured-Black hair styles as proxies fitted to the body scalp.

Asked for by the owner: afro (the owner's word is `bambucha`), and later locs,
cornrows and bantu knots. ATTEMPTED 2026-09-11 and not shipped -- five
iterations produced silhouettes too crude to carry the names. `memory/todo.md`
records why, and every number below is measured rather than guessed.

WHAT WENT WRONG LAST TIME, and what is different now:

* Placement raycast from the head centre onto the scalp. The scalp is not a
  closed dome, so a direction grid misses, and the nearest-direction fallback
  silently piled every miss onto the rim. THIS generator never searches for a
  scalp point: it starts from the scalp VERTICES themselves and pushes them
  outward, so there is nothing to miss.
* `helper-hair` was the obvious source and is the wrong one -- it is a long-hair
  envelope carrying ribbons down over the face. The region here is the BODY
  group only.

THE FIT. `fitProxy` (src/core/Proxy.cpp:423-441) computes
    P = SUM w_k * H[v_k] + (m.x*d.x, m.y*d.y, m.z*d.z)
and `TMatrix::diagonal` (src/core/Proxy.cpp:82-84) returns (1,1,1) when the
file declares no scale or shear form -- which this one does not. So `d` is a
plain WORLD-SPACE offset in rest, and a proxy vertex is written as
    <b> <b> <b> 1 0 0 <dx> <dy> <dz>
binding to base vertex b. Rest space is right: fitting runs before skinning
(main.cpp wornSkins), so hair anchored to the scalp follows a head turn free.
"""
import argparse
import math
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
BASE = ROOT / "data" / "3dobjs" / "base.obj"
OUT = ROOT / "data" / "hair"

# The cranium centre, measured and recorded in `memory/todo.md`.
CENTRE = (0.0, 7.75, 0.50)

# The hairline, in degrees of elevation about CENTRE, as a function of azimuth
# measured from the FRONT (+z). Measured on the base mesh: forehead sits at
# ~+9 degrees in this frame, brow -13, ears -25, nape -50. An earlier attempt
# used `14 + 20*cos(theta)` in a frame 0.2 dm too low and put every style in a
# band across the crown that read as a headband.
def hairline(azim_deg: float) -> float:
    return -19.0 + 31.0 * math.cos(math.radians(azim_deg))


def read_base():
    verts, faces = [], []
    group = ""
    for line in BASE.read_text().splitlines():
        if line.startswith("v "):
            verts.append(tuple(float(t) for t in line.split()[1:4]))
        elif line.startswith("g "):
            group = line.split(None, 1)[1].strip()
        elif line.startswith("f ") and group == "body":
            faces.append([int(p.split("/")[0]) - 1 for p in line.split()[1:]])
    return verts, faces


def spherical(v):
    """Elevation and azimuth of a vertex about CENTRE, in degrees."""
    dx, dy, dz = v[0] - CENTRE[0], v[1] - CENTRE[1], v[2] - CENTRE[2]
    return (math.degrees(math.atan2(dy, math.hypot(dx, dz))),
            math.degrees(math.atan2(dx, dz)))


def afro(verts, faces, thickness=0.78, edge_deg=20.0, skirt_deg=12.0):
    """A rounded shell standing off the scalp: the afro silhouette.

    Every hair-bearing vertex is pushed out along its own surface NORMAL by
    `thickness`, so the shell is a uniform-depth copy of the skull. An earlier
    version grew each vertex RADIALLY from the cranium centre and rendering it
    settled the matter: radial growth only yields a ball if the source is
    already a sphere, and the scalp is not -- low vertices sit far from the
    centre horizontally, so they pushed sideways and the result was a
    wide-brimmed mushroom cap. A normal offset follows the head, which from the
    front reads as the round halo an afro actually is.

    The offset holds FULL volume across the scalp and eases off only in the
    last `edge_deg` degrees before the hairline, so the shell meets the skin
    tangentially at the edge without losing body anywhere else. MEASURED: a
    single mesh edge changes the hairline margin by a median of 7.2 degrees
    (p90 14.4), so a band narrower than that collapses the whole taper into one
    face -- rendering it at 9 degrees produced dark jagged spikes fanning
    around the face. 30 degrees spans about four rows of vertices. A first version
    eased over the whole scalp -- `margin/deepest` -- and rendering it settled
    the matter: mean offset 0.086 dm, 18 px of added height, no added width,
    and it read as a flat-top sitting too far back with a bare forehead. Volume
    has to be near-constant or it is not an afro.
    """
    margin = {}
    for i, v in enumerate(verts):
        elev, azim = spherical(v)
        m = elev - hairline(azim)
        # A SKIRT of vertices below the hairline is kept in the patch with zero
        # growth. Without it the shell's rim IS the region boundary, and that
        # boundary is a ragged polyline of whichever vertices happened to fall
        # inside -- which rendered as serrations along the hairline. With it the
        # grown part ends inside the patch and the rim lies flat on the skin.
        if m > -skirt_deg:
            margin[i] = m
    if not margin:
        return [], [], {}

    # Only whole faces, and only the vertices those faces use: `loadObj`
    # REJECTS a vertex referenced by no face (src/core/ObjReader.cpp:235-243),
    # and the whole proxy then fails to load -- which renders as a bald head
    # rather than as an error any gate would catch.
    keep = [f for f in faces if all(v in margin for v in f)]
    used = sorted({v for f in keep for v in f})

    # Area-weighted vertex normals over the kept patch, flipped outward: the
    # base mesh's winding is not assumed, it is CHECKED against the direction
    # away from the cranium centre.
    normal = {b: [0.0, 0.0, 0.0] for b in used}
    for f in keep:
        a, bb, c = verts[f[0]], verts[f[1]], verts[f[2]]
        u = (bb[0] - a[0], bb[1] - a[1], bb[2] - a[2])
        w = (c[0] - a[0], c[1] - a[1], c[2] - a[2])
        n = (u[1] * w[2] - u[2] * w[1], u[2] * w[0] - u[0] * w[2], u[0] * w[1] - u[1] * w[0])
        for v in f:
            for k in range(3):
                normal[v][k] += n[k]

    placed = {}
    for b in used:
        n = normal[b]
        ln = math.sqrt(sum(t * t for t in n))
        x, y, z = verts[b]
        out = (x - CENTRE[0], y - CENTRE[1], z - CENTRE[2])
        if ln < 1e-9:
            placed[b] = (0.0, 0.0, 0.0)
            continue
        n = [t / ln for t in n]
        if sum(n[k] * out[k] for k in range(3)) < 0.0:
            n = [-t for t in n]
        # smoothstep over a NARROW band at the hairline only.
        t = min(1.0, max(0.0, margin[b]) / edge_deg)
        ease = t * t * (3.0 - 2.0 * t)
        grow = thickness * ease
        placed[b] = (n[0] * grow, n[1] * grow, n[2] * grow)
    return used, keep, placed


def write_style(name, stem, used, keep, placed, verts):
    index = {b: i for i, b in enumerate(used)}
    obj = ["# Generated by tools/make_hair_styles.py -- do not edit by hand.",
           f"# {name}: {len(used)} vertices, {len(keep)} faces.", "g " + stem]
    for b in used:
        x, y, z = verts[b]
        d = placed[b]
        obj.append("v {:.6f} {:.6f} {:.6f}".format(x + d[0], y + d[1], z + d[2]))
    for f in keep:
        obj.append("f " + " ".join(str(index[v] + 1) for v in f))

    mhclo = [
        "# Generated by tools/make_hair_styles.py -- do not edit by hand.",
        "#",
        "# A shell standing off the BODY scalp. Each vertex binds to the base",
        "# vertex it grew from, with a world-space offset: the fit declares no",
        "# scale form, so TMatrix::diagonal is (1,1,1) and the offset applies",
        "# unchanged (src/core/Proxy.cpp:82-84, 423-441).",
        f"name {name}",
        f"uuid 8f2c1d4a-hair-{stem}",
        "basemesh hm08",
        f"obj_file {stem}.obj",
        "material materials/hair.mhmat",
        "z_depth 60",
        "verts 0",
    ]
    for b in used:
        d = placed[b]
        mhclo.append(f"{b} {b} {b} 1.00000 0.00000 0.00000 "
                     f"{d[0]:.5f} {d[1]:.5f} {d[2]:.5f}")
    return "\n".join(obj) + "\n", "\n".join(mhclo) + "\n"


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--check", action="store_true",
                    help="compare the committed assets against a fresh derivation and "
                         "fail if they differ, instead of writing them")
    args = ap.parse_args()

    verts, faces = read_base()
    used, keep, placed = afro(verts, faces)
    obj, mhclo = write_style("Afro", "afro", used, keep, placed, verts)
    wanted = {"afro.obj": obj, "afro.mhclo": mhclo}

    if args.check:
        # The point of this mode: "generated" is a claim LICENSING.md makes
        # about these files, and without a gate it is an aspiration -- a
        # hand-edit to afro.obj would go unnoticed for ever.
        stale = [n for n, text in wanted.items()
                 if not (OUT / n).exists() or (OUT / n).read_text() != text]
        for n in stale:
            print(f"{n} differs from a fresh derivation", file=sys.stderr)
        if stale:
            return 1
        print(f"afro: {len(wanted)} files match a fresh derivation")
        return 0

    for n, text in wanted.items():
        (OUT / n).write_text(text)
    grown = [math.dist((0, 0, 0), placed[b]) for b in used]
    print(f"afro: {len(used)} vertices, {len(keep)} faces")
    print(f"  offset 0 at the hairline .. {max(grown):.4f} dm at the crown, "
          f"mean {sum(grown)/len(grown):.4f}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
