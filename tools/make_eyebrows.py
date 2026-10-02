#!/usr/bin/env python3
# SPDX-License-Identifier: CC0-1.0
"""Generates `data/eyebrows/*.mhclo` -- brow ridges bound to the face.

    ./.venv-mh/bin/python tools/make_eyebrows.py

WHY THIS CAN EXIST AT ALL. `EyebrowsTaskView` has been filed as "blocked on
content: no helper cage in the base mesh", and the cage really is missing --
`base.obj` carries `helper-{genital,hair,l-eye,l-eyelashes-1,l-eyelashes-2,
lower-teeth,r-eye,r-eyelashes-1,r-eyelashes-2,skirt,tights,tongue,upper-teeth}`
and no eyebrow among them. But a cage stopped being REQUIRED when the hair
styles got a binder that works against the body: MEASURED, `hair.mhclo` binds
428 helper vertices and zero body ones, while `afro` and `locs` bind 475 BODY
vertices and zero helper. The two vertex sets are disjoint (13,380 body, 4,778
helper), so that is a real division. A brow binds to the face exactly as a loc
binds to the scalp, and **the base mesh is not touched** -- 19,158 vertices is
the parity oracle every golden fixture is captured against.

WHAT IT REUSES, DELIBERATELY. `make_hair_styles` already owns every piece:
`read_base`, `region_triangles`, `project_to_region`, `ridge` and
`write_bound_style`. An eyebrow is a short arc swept into a low ridge, which is
a cornrow that does not leave the forehead, so writing a second sweep would be
a second thing to keep correct.

THE ARC IS MEASURED, NOT DRAWN. The face surface above the eye was sampled
before any geometry was placed: between y 7.46 and 7.58 it runs from x 0.06,
z 1.52 near the midline out to x 0.61, z 1.14 at the temple. The eye helper
cage centres at (0.308, 7.284, 1.245) and spans y 7.146..7.422, so a brow sits
just above it. Every point of the path is then PROJECTED onto the real surface
rather than trusted from the formula -- the face is not a sphere and the
`ridge` docstring records what assuming otherwise cost the braids.
"""
import math
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import make_hair_styles as H  # noqa: E402

DATA = Path(__file__).resolve().parent.parent / "data"

# The eye, from `helper-l-eye`: centre (0.308, 7.284, 1.245), y 7.146..7.422.
# The brow rides above that band.
BROW_Y = 7.47
# Inner end near the midline, outer end short of the temple where the surface
# falls away steeply (z drops 1.28 -> 1.14 between x 0.51 and 0.61).
BROW_X0 = 0.11
BROW_X1 = 0.50
# A real brow is not level: it lifts toward the outer third and drops again.
# Peak placed at 62% of the span, which is where the arch reads highest.
ARCH_AT = 0.62
ARCH = 0.055

# The brow neighbourhood of the base mesh, as ONE box used twice: to pick the
# triangles the arc is PROJECTED onto, and -- through `--bind-region` -- the
# triangles it is BOUND to. Those two were allowed to differ once and the
# binding went to the scalp while the projection went to the brow, which is
# how every record ended up carrying a 37.8 mm offset to the hairline.
REGION_X = 0.80
REGION_Y0, REGION_Y1 = 7.25, 7.95
REGION_Z0, REGION_Z1 = 0.80, 4.00
BIND_REGION = (f"{-REGION_X},{REGION_X},{REGION_Y0},{REGION_Y1},"
               f"{REGION_Z0},{REGION_Z1}")


def brow_path(tris, samples=22):
    """The arc, projected onto the face, as `ridge` wants it: (point, normal, d)."""
    path = []
    for i in range(samples):
        t = i / (samples - 1)
        x = BROW_X0 + (BROW_X1 - BROW_X0) * t
        # A single raised cosine: zero at both ends, ARCH at ARCH_AT.
        u = t / ARCH_AT if t <= ARCH_AT else (1.0 - t) / (1.0 - ARCH_AT)
        y = BROW_Y + ARCH * math.sin(max(0.0, min(1.0, u)) * math.pi / 2.0) ** 2
        # TWO STEPS, and the first one is not optional.
        #
        # `project_to_region` returns the CLOSEST point, so probing from far in
        # front (z = 3.0) put every sample on whatever protrudes most -- the
        # brow ridge beside the nose -- and the whole arc collapsed to
        # x -0.152..0.152 instead of spanning 0.11..0.50. That is the same
        # failure `ridge`'s docstring records for the braids: a projection with
        # nothing to aim at quietly piles up on one feature.
        #
        # So find the surface height near this (x, y) from the region's own
        # vertices first, and only then project from just in front of it, where
        # "closest" and "directly behind" are the same point.
        near = min(
            ((vx - x) ** 2 + (vy - y) ** 2, vz)
            for tri in tris
            for (vx, vy, vz) in tri[:3]
        )
        probe = (x, y, near[1] + 0.25)
        q, n, _d = H.project_to_region(probe, tris)
        path.append((q, n, 0.0))
    return path


def build(stand, half, sides, samples):
    verts, faces = H.read_base()
    used = sorted({v for f in faces for v in f})
    # A generous box around both brows: the projection is exhaustive over these
    # triangles, so it must contain the whole arc AND room around it, but not
    # the entire head.
    region = [
        v
        for v in used
        if abs(verts[v][0]) < REGION_X
        and REGION_Y0 < verts[v][1] < REGION_Y1
        and REGION_Z0 < verts[v][2] < REGION_Z1
    ]
    tris = H.region_triangles(verts, region, faces)
    if not tris:
        raise SystemExit("no triangles in the brow region; the box is wrong")

    path = brow_path(tris, samples)
    pts, quads = H.ridge(path, stand=stand, half=half, sides=sides)

    # The right brow is the left one mirrored in x. Mirroring the RESULT rather
    # than re-projecting keeps the pair identical, which is what a face wants;
    # re-running the projection would let floating point make them differ.
    n = len(pts)
    allpts = list(pts) + [(-x, y, z) for x, y, z in pts]
    # Winding flips under a mirror, so the copied quads are reversed or every
    # right-hand face points into the head.
    allquads = list(quads) + [tuple(reversed([i + n for i in q])) for q in quads]
    return allpts, allquads, tris


def write_eyebrows(points, faces, bindings):
    """The .obj and .mhclo, in the contract `fitProxy` reads.

    Not `H.write_bound_style`: that one hardcodes `materials/hair.mhmat` and a
    `makehuman/hair/<stem>` uuid namespace, and an eyebrow is neither. Bending
    it into taking both would grow a parameter for one caller; the six lines
    that differ are cheaper than the indirection.
    """
    import uuid

    obj = [H.BANNER, f"# Eyebrows: {len(points)} vertices, {len(faces)} faces.", "g eyebrows"]
    for x, y, z in points:
        obj.append(f"v {x:.6f} {y:.6f} {z:.6f}")
    for f in faces:
        obj.append("f " + " ".join(str(i + 1) for i in f))

    mhclo = [
        H.BANNER,
        "#",
        "# AUTHORED geometry bound to the FACE by three base vertices,",
        "# barycentric weights and an offset -- `mh::core::bindToSurface` via",
        "# `--bind-points`. There is no eyebrow helper cage in the base mesh and",
        "# this does not need one, which is the whole reason the slot could be",
        "# filled without touching base.obj.",
        "name Eyebrows",
        f"uuid {uuid.uuid5(uuid.NAMESPACE_URL, 'makehuman/eyebrows/eyebrows')}",
        "basemesh hm08",
        "obj_file eyebrows.obj",
        "material materials/eyebrows.mhmat",
        # Above the skin and below hair: a brow is drawn on the face, and a
        # fringe hangs over it.
        "z_depth 55",
        "verts 0",
    ]
    mhclo.extend(bindings)
    return "\n".join(obj) + "\n", "\n".join(mhclo) + "\n"


def main() -> int:
    # 2.2 mm proud, which is what a real brow is, and it took finding a BUG to
    # get there. This comment used to say the opposite -- that anatomical
    # numbers were invisible and 9 mm was the honest floor -- and the
    # measurement behind that was real but the diagnosis was wrong.
    #
    # THE BUG: `bind_points` sent every point to `--bind-points`, which bound
    # to the SCALP and nothing else (`main.cpp`, `loadScalp`). The brow arc sits
    # at y 7.47..7.53; the scalp triangles it chose sit at y 7.87..8.11, so all
    # 264 vertices hung off just 16 scalp vertices with a mean offset of 50.3 mm
    # (max 61.4). `fitProxy` SCALES an offset per axis but never rotates it
    # (`Proxy.cpp:434-438`), so that 50 mm vector did not follow the brow ridge:
    # on the default character -- 16.594 dm tall against the base mesh's 16.946
    # -- the brow landed inside the skin. Hence the "floor": 0.055 showed 0
    # pixels and 0.065 showed 34, not because a thin brow cannot be seen but
    # because it had to be inflated until it escaped the head it was sunk into.
    # Moving the arc UP the face changed nothing, which is what ruled out the
    # eye-socket hollow and pointed at the anchor.
    #
    # With `--bind-region` the same points bind to 68 vertices at y 7.394..7.765
    # -- the brow itself -- and the mean offset is 1.0 mm, which is the standoff
    # and nothing else. At 0.022 the render now differs from a bare face by 211
    # pixels, MORE than the 9 mm version's 199, so this is both correct and more
    # visible than what it replaces. Rendered and looked at: dark, arched,
    # lying along the ridge instead of standing off it.
    stand, half, sides, samples = 0.022, 0.080, 6, 22
    for arg in sys.argv[1:]:
        key, _, value = arg.partition("=")
        if key == "--stand":
            stand = float(value)
        elif key == "--half":
            half = float(value)
        elif key == "--sides":
            sides = int(value)
        elif key == "--samples":
            samples = int(value)

    pts, quads, _tris = build(stand, half, sides, samples)
    app = H.app_binary()
    binds = H.bind_points(app, pts, region=BIND_REGION)
    if len(binds) != len(pts):
        raise SystemExit(f"binder returned {len(binds)} rows for {len(pts)} points")

    (DATA / "eyebrows").mkdir(exist_ok=True)
    # The slot's matcap. Every proxy slot needs `skinmat_<slot>.png` beside its
    # .mhclo -- the material carries no diffuse texture and `litsphere.frag`
    # multiplies the matcap by the white placeholder, so the MATCAP is the
    # colour. Reuses the tinting `make_helper_proxies` already does for teeth,
    # hair and the rest rather than shipping a hand-made image.
    #
    # Darker than the hair tint (0.32, 0.22, 0.16) on purpose: a brow reads
    # darker than scalp hair on the same head because it is thin and sits
    # against lit skin instead of massed against a silhouette.
    import make_helper_proxies as P

    (DATA / "eyebrows" / "skinmat_eyebrows.png").write_bytes(P.make_litsphere((0.22, 0.15, 0.11)))
    obj, mhclo = write_eyebrows(pts, quads, binds)
    (DATA / "eyebrows" / "eyebrows.obj").write_text(obj)
    (DATA / "eyebrows" / "eyebrows.mhclo").write_text(mhclo)
    print(f"eyebrows: {len(pts)} vertices, {len(quads)} faces "
          f"(stand {stand}, half {half}, {sides} sides, {samples} samples)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
