# SPDX-License-Identifier: AGPL-3.0-or-later
"""Long hair as cards laid over the base mesh's own envelope.

    MH_APP=<makehuman> ./.venv-mh/bin/python tools/make_long_hair.py

WHY THIS EXISTS. `data/hair/hair.*` -- the style called plainly "Hair" -- was
the `helper-hair` cage copied verbatim: 428 vertices, 198 faces, with the
52-strand scalp sheet painted on. Rendered, it is a flat faceted HOOD draped
over the head and face, polygon edges and all, and the texture does nothing.

It was the last shell in the tree. The afro stopped being one, then the rope
styles' skullcap, then the eyelashes; each time for the same measured reason,
which `make_hair_styles.derive()` states once and for all: no texture makes a
shell read as hair close up, because the geometry has no hairs in it.

THE CAGE IS STILL THE SHAPE. It is a long-hair envelope reaching from the crown
at y 8.497 down to y 2.013, and that silhouette is the asset's whole identity --
it is not this tool's business to invent a different haircut. So the cage is
read, roots are placed on the part of it that covers the scalp, and each card
WALKS DOWN ITS SURFACE: step along the surface tangent, snap back to the cage,
repeat. The hair follows the envelope exactly and is made of strands.
"""

import argparse
import math
import random
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

import coiled_hair as C  # noqa: E402
import make_coils as M  # noqa: E402
import make_hair_styles as H  # noqa: E402

ROOT = Path(__file__).resolve().parent.parent
OUT = ROOT / "data" / "hair"
BASE = ROOT / "data" / "3dobjs" / "base.obj"
CAGE = "helper-hair"

SEED = 20261007

# Cards, and the count comes from the envelope's own area: 31.81 dm^2 of
# surface at a card 0.09 dm wide needs roughly 700 of them to cover it with the
# overlap that stops a parting showing between neighbours.
CARDS = 700
# Samples along a card. The envelope is nearly a developable surface over most
# of its length, so the bend is gentle and 16 carries it; the coil styles need
# their sampling tied to a helix and this has no helix in it.
# 34 samples at 0.19 dm, not 16 at 0.42. The step is what decides how closely a
# card hugs the scalp near its ROOT, and at 0.42 each card had exactly one
# sample on the cap before the next landed down the side -- 700 isolated points
# spread over the whole crown, which rendered as bare scalp with hair falling
# past it on both sides. The length a card can reach is the product of the two,
# so halving the step doubles the count to keep the fall reaching the waist.
STEPS = 34
HALF_WIDTH = 0.045
# How far outside the cage a card floats. Enough that cards do not z-fight with
# each other where they overlap, small enough that the silhouette stays the
# envelope's.
LIFT = 0.012
# Roots go on the part of the envelope that covers the scalp. MEASURED: the
# cage's crown is y 8.497 and the hair-bearing scalp bottoms out at 6.989, so
# this is the cap rather than the fall.
ROOT_Y = 7.55
# A card stops when it leaves the cage or runs out of length. Lengths vary so
# the tips land at different heights and the mass reads as layered rather than
# cut straight across.
LEN_MIN, LEN_MAX = 2.2, 6.3
STEP = 0.19

MATERIAL = "materials/hair_card.mhmat"
# THE SCALP, not the torso, even though the hair reaches the waist.
#
# The binder ties each card vertex to the nearest triangle in this box. Given
# the whole torso it does the obvious thing and binds hair beside the cheek to
# the CHEEK -- and then a jaw drop drags the hair with it: MEASURED, AU26 moved
# 5,476 of 30,320 vertices, where the shell it replaces moved none, and the
# suite pins that a jaw drop leaves the hair alone.
#
# Binding the whole fall to the scalp box makes the hair follow the HEAD as one
# piece, which is what the identity-bound cage did in effect and what a hair
# proxy should do. Hair resting on the shoulders will not slide when they move;
# that is the trade, and it is the same one the old asset made.
#
# The FLOOR of the box is 7.60 and that is measured, not rounded. The scalp
# region reaches down to y 6.9890, but its lower part still moves a little when
# the jaw drops, and the hair inherits whatever it binds to:
#
#     bind box floor   AU26 moved   worst displacement
#     torso (1.6)         5476          --
#     6.95               12360        0.03546 dm
#     7.30                9881        0.00905 dm
#     7.60                   0        0.00014 dm   <- one unit of the written decimal
#
# 7.60 is the first floor at which a jaw drop leaves the hair alone, which is
# the property the suite pins and which the identity-bound cage had for free.
REGION = "-0.80,0.80,7.60,8.55,-0.45,1.50"
# Half-width of the corridor in front of the cranium centre that hair may not
# occupy. 0.55 dm clears the cheeks, so hair falls against the side of the head
# rather than across the eyes.
FACE_HALF = 0.55


def read_cage():
    """The envelope's triangles, as a flat list of (a, b, c)."""
    verts, group, tris = [], "", []
    for line in BASE.read_text().splitlines():
        if line.startswith("v "):
            verts.append(tuple(float(t) for t in line.split()[1:4]))
        elif line.startswith("g "):
            group = line.split(None, 1)[1].strip()
        elif line.startswith("f ") and group == CAGE:
            f = [int(p.split("/")[0]) - 1 for p in line.split()[1:]]
            for k in range(1, len(f) - 1):
                tris.append((verts[f[0]], verts[f[k]], verts[f[k + 1]]))
    if not tris:
        raise SystemExit(f"no `{CAGE}` faces in the base mesh")
    return tris


def normal(tri):
    a, b, c = tri
    return C.normalise(C.cross(C.sub(b, a), C.sub(c, a)), (0.0, 1.0, 0.0))


def closest_on_triangle(p, tri):
    """The nearest point to `p` on one triangle, clamped to its edges."""
    a, b, c = tri
    ab, ac, ap = C.sub(b, a), C.sub(c, a), C.sub(p, a)
    d1, d2 = C.dot(ab, ap), C.dot(ac, ap)
    if d1 <= 0.0 and d2 <= 0.0:
        return a
    bp = C.sub(p, b)
    d3, d4 = C.dot(ab, bp), C.dot(ac, bp)
    if d3 >= 0.0 and d4 <= d3:
        return b
    vc = d1 * d4 - d3 * d2
    if vc <= 0.0 <= d1 and d3 <= 0.0:
        return C.add(a, C.scale(ab, d1 / (d1 - d3)))
    cp = C.sub(p, c)
    d5, d6 = C.dot(ab, cp), C.dot(ac, cp)
    if d6 >= 0.0 and d5 <= d6:
        return c
    vb = d5 * d2 - d1 * d6
    if vb <= 0.0 <= d2 and d6 <= 0.0:
        return C.add(a, C.scale(ac, d2 / (d2 - d6)))
    va = d3 * d6 - d5 * d4
    if va <= 0.0 and (d4 - d3) >= 0.0 and (d5 - d6) >= 0.0:
        return C.add(b, C.scale(C.sub(c, b), (d4 - d3) / ((d4 - d3) + (d5 - d6))))
    denom = 1.0 / (va + vb + vc)
    return C.add(a, C.add(C.scale(ab, vb * denom), C.scale(ac, vc * denom)))


def snap(p, tris):
    """The nearest point on the whole cage, and the normal there."""
    best, bestd, bestn = None, 1e30, (0.0, 1.0, 0.0)
    for tri in tris:
        q = closest_on_triangle(p, tri)
        d = (q[0] - p[0]) ** 2 + (q[1] - p[1]) ** 2 + (q[2] - p[2]) ** 2
        if d < bestd:
            best, bestd, bestn = q, d, normal(tri)
    return best, bestn


def sample_roots(tris, rng):
    """Roots spread over the cap of the envelope, area-weighted and stratified.

    The same shape as `make_coils.scalp_roots`, and for the same reason: picking
    triangles uniformly crowds roots wherever the cage happens to be dense.
    """
    # ABOVE THE HAIRLINE, not merely high up. The envelope's cap reaches
    # forward over the forehead, so sampling it by height alone rooted hair on
    # the brow and the walk took it straight down the face: rendered, the whole
    # face was curtained off. `mh::core::hairlineElevation` already says where
    # hair may grow and its Python mirror is right here, so the rule is reused
    # rather than restated.
    def on_scalp(tri):
        mid = tuple(sum(p[k] for p in tri) / 3.0 for k in range(3))
        elev, azim = H.spherical(mid)
        return mid[1] >= ROOT_Y and elev >= H.hairline(azim)

    cap = [t for t in tris if on_scalp(t)]
    if not cap:
        raise SystemExit("no cage triangles above the root line")
    areas, total = [], 0.0
    for a, b, c in cap:
        total += 0.5 * C.length(C.cross(C.sub(b, a), C.sub(c, a)))
        areas.append(total)
    roots, carried = [], 0.0
    for tri, area in zip(cap, [areas[0]] + [areas[i] - areas[i - 1]
                                            for i in range(1, len(areas))]):
        share = CARDS * area / total + carried
        take = int(share)
        carried = share - take
        a, b, c = tri
        for _ in range(take):
            r1, r2 = rng.random(), rng.random()
            sq = math.sqrt(r1)
            w0, w1, w2 = 1.0 - sq, sq * (1.0 - r2), sq * r2
            roots.append(tuple(a[k] * w0 + b[k] * w1 + c[k] * w2 for k in range(3)))
    return roots


def off_the_face(p):
    """Push a fallen point out of the face, so hair parts around it.

    THE ENVELOPE ITSELF DRAPES DOWN THE FRONT. Restricting the ROOTS to the
    hair-bearing scalp was not enough and the render said so plainly: the cards
    still started behind the hairline, walked down the cage, and curtained the
    whole face off. The cage's front panel is hair falling in front of the
    SHOULDERS, and a walk that follows the surface cannot tell that from hair
    over the eyes.

    So the fall is deflected rather than the cage re-cut: a point in front of
    the cranium centre and near the midline is moved out to the edge of the
    keep-out, keeping the side it was already on. Hair then falls beside the
    face, which is what long hair does and what every reference of it shows.
    """
    # ONLY BELOW THE HAIRLINE. This used to push aside ANY point in front of
    # the cranium centre, at every height -- which includes the CROWN, where
    # hair is supposed to be. MEASURED: the crown band changed 2.0% of its
    # pixels when the hair was worn, against 65.4% at the side, because the
    # cards rooted up there were being swept off the top of the head by this
    # very function.
    #
    # Above the hairline a point is on the scalp and stays where it fell; below
    # it on the face side it is moved aside. `hairline` is the same rule the
    # roots are chosen by and that `mh::core::hairlineElevation` owns.
    elev, azim = H.spherical(p)
    if elev >= H.hairline(azim):
        return p
    if p[2] <= H.CENTRE[2] or abs(p[0]) >= FACE_HALF:
        return p
    side = 1.0 if p[0] >= 0.0 else -1.0
    return (side * FACE_HALF, p[1], p[2])


def fall(start, tris, length, rng):
    """Walk down the cage's surface from `start`, staying on it.

    Each step projects straight DOWN onto the local tangent plane, moves, and
    snaps back to the cage. That is what makes the card follow the envelope
    instead of cutting through it or flying off at the shoulders, where the
    surface turns over hardest.
    """
    p, n = snap(start, tris)
    p = off_the_face(p)
    out, norms, travelled = [], [], 0.0
    drift = rng.uniform(-0.12, 0.12)
    while travelled < length:
        out.append(C.add(p, C.scale(n, LIFT)))
        norms.append(n)
        down = (drift * 0.25, -1.0, 0.0)
        tangent = C.sub(down, C.scale(n, C.dot(down, n)))
        if C.length(tangent) < 1e-6:
            break
        step = C.scale(C.normalise(tangent), STEP)
        nxt, n2 = snap(C.add(p, step), tris)
        if C.length(C.sub(nxt, p)) < 1e-4:
            break  # the walk has reached the bottom edge of the envelope
        p, n = nxt, n2
        p = off_the_face(p)
        travelled += STEP
        if len(out) >= STEPS:
            break
    return out, norms


def card(points, norms, rng):
    """A fallen path widened into a ribbon that faces away from the envelope."""
    pts, uvs, quads = [], [], []
    n = len(points) - 1
    if n < 1:
        return [], [], []
    w0 = HALF_WIDTH * rng.uniform(0.75, 1.3)
    for i, p in enumerate(points):
        nxt = points[min(i + 1, n)]
        prv = points[max(i - 1, 0)]
        tangent = C.normalise(C.sub(nxt, prv), (0.0, 1.0, 0.0))
        # THE SURFACE'S OWN NORMAL, which `fall` already has. This used to be
        # the horizontal radial from the body axis, and that is meaningless at
        # the CROWN: there the surface faces up, the radial is perpendicular to
        # it, and the card came out edge-on. MEASURED, cards lay within 0.10 dm
        # of every crown vertex -- they were there all along -- and the top of
        # the head still rendered bare orange, because a card seen on its edge
        # covers nothing.
        out = norms[i]
        across = C.cross(tangent, out)
        if C.length(across) < 0.25:
            across = C.cross(tangent, (0.0, 1.0, 0.0))
        across = C.normalise(across, (1.0, 0.0, 0.0))
        t = i / n
        # Tapering to a point: a card that stops square reads as a cut end.
        w = w0 * (1.0 - 0.75 * t)
        pts.append(C.sub(p, C.scale(across, w)))
        pts.append(C.add(p, C.scale(across, w)))
        uvs.append((0.0, t))
        uvs.append((1.0, t))
    for i in range(n):
        a, b = 2 * i, 2 * i + 1
        quads.append([a, b, b + 2, a + 2])
    return pts, quads, uvs


def build():
    rng = random.Random(SEED)
    tris = read_cage()
    # THE ENVELOPE IS NOT RELIABLY OUTSIDE THE HEAD. At the crown the cage tops
    # out at y 8.484..8.497 while the scalp itself reaches 8.50, so cards laid
    # on it there are INSIDE the skull and invisible -- which is why the top of
    # the head kept rendering bare while the measurements insisted cards lay
    # within 0.10 dm of every crown vertex. They did; they were buried.
    #
    # `make_coils.push_outside` is the pass that already solves this for the
    # coil styles, and the skull it needs is the same one.
    body_verts, body_faces = H.read_base()
    skull = M.skull_radii(body_verts, body_faces)
    roots = sample_roots(tris, rng)
    allpts, allquads, alluvs = [], [], []
    short = 0
    for r in roots:
        path, norms = fall(r, tris, rng.uniform(LEN_MIN, LEN_MAX), rng)
        path, _buried = M.push_outside(path, skull)
        if len(path) < 3:
            short += 1
            continue
        pts, quads, uvs = card(path, norms, rng)
        base = len(allpts)
        allpts.extend(pts)
        alluvs.extend(uvs)
        allquads.extend([[i + base for i in q] for q in quads])
    if short > len(roots) // 4:
        raise SystemExit(f"{short} of {len(roots)} cards were too short to draw")
    print(f"hair: {len(allpts)} vertices, {len(allquads)} faces, "
          f"{len(roots) - short} cards, {short} dropped")
    return allpts, allquads, alluvs


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--app", default="", help="the makehuman binary to bind with")
    ap.add_argument("--check", action="store_true",
                    help="report staleness instead of writing")
    args = ap.parse_args()

    pts, quads, uvs = build()
    app = args.app or H.app_binary()
    binds = H.bind_points(app, pts, REGION)
    if len(binds) != len(pts):
        raise SystemExit(f"{len(binds)} bindings for {len(pts)} hair points")
    # ITS OLD IDENTITY, kept. The style shipped as this uuid and saved .mhm
    # files name it; minting a fresh one would orphan every one of them.
    obj, mhclo = H.write_bound_style("Hair", "hair", pts, quads, binds, uvs,
                                     material=MATERIAL,
                                     uuid_override="3f8c21d5-6b04-4e79-9c52-1a7de0b46f38")
    wanted = {"hair.obj": obj, "hair.mhclo": mhclo}
    stale = [n for n, text in wanted.items()
             if not (OUT / n).exists() or (OUT / n).read_text() != text]
    if args.check:
        for n in stale:
            print(f"{n} differs from a fresh derivation", file=sys.stderr)
        print(f"hair: {len(wanted) - len(stale)} shipped files match a fresh run")
        return 1 if stale else 0
    for n, text in wanted.items():
        (OUT / n).write_text(text)
    print(f"hair: wrote {', '.join(sorted(wanted))}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
