#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-or-later
"""Generates `data/hair/coils.*` -- coiled hair as cards, not as a shell.

    MH_APP=<makehuman> ./.venv-mh/bin/python tools/make_coils.py

WHY A FIFTH STYLE RATHER THAN A BETTER AFRO. The afro is a SHELL: one offset
copy of the scalp, 475 vertices, with the hair painted on by an alpha. That is
cheap and it is the right trade for a silhouette seen at a distance, but no
texture makes a shell read as hair from close up, because the geometry has no
hairs in it. This style has them -- and keeps the cost in the same bracket as
the styles that already ship (locs is 3,448 faces) by drawing each clump as a
CARD: a flat ribbon of quads following a coiled centreline, which is what
every real-time hair system does and what the shell cannot be.

WHERE THE SHAPE COMES FROM. `coiled_hair.py` implements the geometry described
in Wu, Shi, Darke and Kim, "Curly-Cue: Geometric Methods for Highly Coiled
Hair" (SIGGRAPH Asia 2024), written from the paper -- see that file's header
for the provenance, which matters because the authors' own switchback code is
GPL-3.0 and was deliberately not read.

Three things that file gives and a sine wave does not:

  * PHASE LOCKING. Hairs leave the scalp on their own phase and fall into step
    further out. Near the scalp that disorder is the "spongy" layer; without it
    hair reads as a wig rather than as growth.
  * SWITCHBACKS. A coil reverses handedness part-way along, pinching to a neck
    as it does. This is what stops a coil reading as a spring.
  * A LOOSELY-GUIDED ROOT. Each strand eases toward its clump's guide instead
    of starting on it, so a clump is a bundle of hairs rather than one thick
    worm.

THE UVS ARE THE SAME CONTRACT the rest of the hair uses: `u` ACROSS the card
and `v` ALONG it, so `hair_strands.png` draws strands running the way the hair
grows. The afro needed its unwrap rewritten to get that; a card gets it for
free, because a card already knows which way its hair runs.
"""
import argparse
import math
import random
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import coiled_hair as C  # noqa: E402
import make_hair_styles as H  # noqa: E402

ROOT = Path(__file__).resolve().parent.parent
OUT = ROOT / "data" / "hair"

# MEASURED against what already ships: locs is 3,448 faces and cornrows 2,112.
# 150 clumps rendered as separate wisps with scalp showing between them -- a
# coiled head is a MASS, and the count has to cover the scalp before anything
# else about it reads. 420 x 15 quads is 6,300 faces, above locs but in the
# same bracket, and every face is hair rather than a patch of scalp.
CLUMPS = 420
SEGMENTS = 15
# A clump of coiled hair is a few millimetres across. 0.055 dm is 5.5 mm, which
# at 150 clumps covers the scalp without the cards visibly overlapping.
CARD_HALF_WIDTH = 0.055
# How far the hair stands off the head, in dm. The afro's shell is 0.78 at the
# crown; a coil that reaches the same envelope reads as the same haircut.
LENGTH = 0.72
# Turns per strand. Coiled hair is a HIGH-frequency helix -- this is the
# property the whole method exists for -- but a card cannot draw a coil finer
# than its own segments, and 15 segments resolves about 3 turns before the
# ribbon starts to alias into a zigzag. Measured by rendering: at 6 turns the
# cards self-intersect into noise.
TURNS = 3.0
COIL_RADIUS = 0.085
# Guides per scalp, and how many a root may choose between.
#
# THE GUIDE COUNT IS THE COVERAGE. Past the lock point a strand's centreline IS
# its guide's -- that is what phase locking means and what makes a clump
# coalesce into one visible curl -- so the number of guides is the number of
# WISPS on the head, not a quality knob. RENDERED at 14, all 420 strands
# collapsed onto fourteen centrelines and the head wore about a dozen scraggly
# tufts over bare scalp. 110 guides with 420 strands is roughly four hairs to a
# wisp, which is a wisp rather than a rope.
#
# The paper's "noisy Voronoi": picking strictly the nearest guide makes the
# cells visible as hard partings, so a root picks randomly among its nearest
# few.
GUIDES = 110
GUIDE_CHOICES = 3

# Where along a card it starts narrowing to a point. A clump of hair comes to
# a tip, and a ribbon that stops square is the "braid ends are cut square"
# limitation this project already records.
TAPER_FROM = 0.55

SEED = 20261006


def scalp_roots(verts, faces, count, rng):
    """Root positions spread over the hair-bearing scalp.

    SAMPLED ON THE SURFACE, not picked from the vertices, and the vertex
    version is why. The hair-bearing region holds only 237 of them, so asking
    for more clumps than that fails outright and asking for most of them picks
    a ragged subset -- 150 rendered as separate wisps with scalp showing
    between. A follicle does not care where a vertex is; sampling inside the
    triangles gives any density with even coverage.

    AREA-WEIGHTED, because the scalp's triangles are not the same size. Picking
    a triangle uniformly would crowd roots wherever the mesh happens to be
    dense -- around the ears -- and thin them over the flat crown.
    """
    used = sorted({v for f in faces for v in f})
    region = []
    for i in used:
        elev, azim = H.spherical(verts[i])
        if elev >= H.hairline(azim):
            region.append(i)
    tris = H.region_triangles(verts, region, faces)
    if not tris:
        raise SystemExit("no triangles in the hair-bearing region; the hairline is wrong")

    areas, total = [], 0.0
    for t in tris:
        a, b, c = t[0], t[1], t[2]
        area = 0.5 * C.length(C.cross(C.sub(b, a), C.sub(c, a)))
        total += area
        areas.append(total)
    if total <= 0.0:
        raise SystemExit("the hair-bearing region has zero area")

    roots = []
    for _ in range(count):
        pick = rng.uniform(0.0, total)
        lo, hi = 0, len(areas) - 1
        while lo < hi:
            mid = (lo + hi) // 2
            if areas[mid] < pick:
                lo = mid + 1
            else:
                hi = mid
        a, b, c = tris[lo][0], tris[lo][1], tris[lo][2]
        # The square-root form, which is the one that is actually uniform over
        # a triangle; plain (r1, r2) bunches every root toward one corner.
        r1, r2 = rng.random(), rng.random()
        sq = math.sqrt(r1)
        w0, w1, w2 = 1.0 - sq, sq * (1.0 - r2), sq * r2
        roots.append((a[0] * w0 + b[0] * w1 + c[0] * w2,
                      a[1] * w0 + b[1] * w1 + c[1] * w2,
                      a[2] * w0 + b[2] * w1 + c[2] * w2))
    return roots


def outward(p):
    """The direction hair leaves the head at `p`: away from the cranium centre.

    Radial rather than the surface normal, and deliberately: a coil is grown as
    a free curve standing off the head, so what matters is that it leaves
    roughly outward and that neighbouring clumps diverge rather than converge.
    `afro()` needs the true normal because it offsets the skull by a fixed
    depth; nothing here is offsetting a surface.
    """
    d = C.sub(p, H.CENTRE)
    return C.normalise(d, (0.0, 1.0, 0.0))


def guide_curve(root, droop, rng):
    """A clump's guide: out from the scalp, then falling under its own weight.

    Four control points, which is the minimum `guided_centreline` accepts and
    enough for the single bend that hair makes -- out, over, down.
    """
    d = outward(root)
    tip = C.add(root, C.scale(d, LENGTH))
    tip = (tip[0], tip[1] - droop, tip[2])
    mid = C.add(root, C.scale(d, LENGTH * 0.55))
    mid = (mid[0], mid[1] - droop * 0.25, mid[2])
    near = C.add(root, C.scale(d, LENGTH * 0.22))
    jitter = lambda s: (rng.uniform(-s, s), rng.uniform(-s, s), rng.uniform(-s, s))  # noqa: E731
    return C.catmull_rom(
        [root, near, C.add(mid, jitter(0.02)), C.add(tip, jitter(0.03))], SEGMENTS
    )


def card(centre, half_width):
    """A coiled centreline widened into a ribbon of quads.

    The ribbon faces OUTWARD, not along the curve's own frame. A parallel frame
    is stable but arbitrary about the tangent, so using it directly lets
    neighbouring cards face randomly and the hair flickers between bright and
    edge-on. Facing each quad away from the head is what makes a mass of cards
    read as one surface.

    """
    pts, uvs = [], []
    frames = C.parallel_frames(centre)
    prev = None
    n = len(centre) - 1
    for i, p in enumerate(centre):
        t = i / n
        nxt = centre[min(i + 1, n)]
        prv = centre[max(i - 1, 0)]
        tangent = C.normalise(C.sub(nxt, prv), (0.0, 1.0, 0.0))
        out = outward(p)
        across = C.cross(tangent, out)
        # DEGENERATE AT THE CROWN, which is where the first version went bald.
        # A hair on top of the head grows ALONG the outward direction, so
        # `tangent` and `out` are parallel there and their cross product
        # collapses: the card gets no stable width and renders edge-on. A
        # near-parallel pair is just as bad as an exactly parallel one -- the
        # direction is defined but wobbles between samples -- so the test is on
        # the cross product's LENGTH, which is sin(angle), not on an exact zero.
        # Below that, any perpendicular is as good as any other, and the
        # strand's own frame supplies a stable one.
        if C.length(across) < 0.25:
            across = frames[i][0]
        across = C.normalise(across, (1.0, 0.0, 0.0))
        # KEPT ON THE SAME SIDE ALL THE WAY ALONG. `cross(tangent, out)` is
        # recomputed per sample and a coiled tangent swings straight THROUGH
        # the outward direction, so the sign flips -- and when it does, this
        # sample's two ribbon vertices swap sides and the quad joining it to
        # the last one crosses itself into a bowtie. MEASURED on the shipped
        # asset before this line existed: 1,407 of 6,300 consecutive pairs
        # flipped, 22.3%, and only 131 of those were the crown fallback. They
        # render as an X with the backface showing through.
        if prev is not None and C.dot(prev, across) < 0.0:
            across = C.scale(across, -1.0)
        prev = across
        w = half_width
        if t > TAPER_FROM:
            w *= max(0.05, 1.0 - (t - TAPER_FROM) / (1.0 - TAPER_FROM))
        pts.append(C.sub(p, C.scale(across, w)))
        pts.append(C.add(p, C.scale(across, w)))
        uvs.append((0.0, t))
        uvs.append((1.0, t))
    quads = []
    for i in range(n):
        a, b = 2 * i, 2 * i + 1
        quads.append([a, b, b + 2, a + 2])
    return pts, quads, uvs


def build(app_path=""):
    rng = random.Random(SEED)
    verts, faces = H.read_base()
    roots = scalp_roots(verts, faces, CLUMPS, rng)

    guide_roots = scalp_roots(verts, faces, GUIDES, random.Random(SEED + 1))
    guides = [guide_curve(r, 0.30 + rng.uniform(-0.05, 0.05), rng) for r in guide_roots]

    allpts, allquads, alluvs = [], [], []
    switchback_total = 0
    for root in roots:
        # Noisy Voronoi (paper 3.3.1): the nearest few guides, chosen between at
        # random. Strictly nearest makes the cells read as hard partings.
        near = sorted(range(len(guides)),
                      key=lambda g: C.length(C.sub(guides[g][0], root)))[:GUIDE_CHOICES]
        guide = guides[rng.choice(near)]

        centre = C.guided_centreline(root, guide, loose=rng.uniform(0.25, 0.45),
                                     samples=SEGMENTS + 1)
        # Switchbacks are common in tightly coiled hair but not universal, and
        # one per strand at a random place is what keeps a head of them from
        # pulsing in unison.
        backs = tuple(sorted(rng.uniform(0.25, 0.85) for _ in range(rng.choice((0, 1, 1, 2)))))
        switchback_total += len(backs)
        coiled = C.coil(
            centre,
            radius=COIL_RADIUS * rng.uniform(0.75, 1.25),
            turns=TURNS * rng.uniform(0.85, 1.15),
            own_phase=rng.uniform(0.0, 2.0 * math.pi),
            clump_phase=rng.uniform(0.0, 2.0 * math.pi),
            switchbacks=backs,
        )
        pts, quads, uvs = card(coiled, CARD_HALF_WIDTH * rng.uniform(0.8, 1.2))
        base = len(allpts)
        allpts.extend(pts)
        alluvs.extend(uvs)
        allquads.extend([[i + base for i in q] for q in quads])

    print(f"coils: {len(allpts)} vertices, {len(allquads)} faces, {CLUMPS} clumps, "
          f"{switchback_total} switchbacks, {len(guides)} guides")

    binds = H.bind_points(app_path or H.app_binary(), allpts)
    if len(binds) != len(allpts):
        raise SystemExit(
            f"the binder returned {len(binds)} bindings for {len(allpts)} points"
        )
    return allpts, allquads, alluvs, binds


def main() -> int:
    # argparse, like `make_hair_styles.py`, and not a partition on "=". The
    # hand-rolled form accepted `--app=X` and SILENTLY DROPPED `--app X`,
    # leaving the binary to fall back to MH_APP or to a hard-coded debug path
    # -- which is the exact hazard tests/CMakeLists.txt records for the sibling
    # tool, whose own CMake invocation uses the space form. A mistyped flag was
    # ignored just as quietly.
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--app", default="", help="the makehuman binary to bind with")
    ap.add_argument("--check", action="store_true",
                    help="regenerate into memory and report whether the shipped asset is stale")
    args = ap.parse_args()
    app = args.app
    try:
        pts, quads, uvs, binds = build(app)
    except SystemExit:
        raise
    except Exception as exc:  # noqa: BLE001 -- the tool must name what broke
        print(f"coils: cannot build: {exc}", file=sys.stderr)
        return 1

    obj, mhclo = H.write_bound_style("Coils", "coils", pts, quads, binds, uvs)
    obj = obj.replace("tools/make_hair_styles.py", "tools/make_coils.py", 1)
    # STALENESS, which is the failure this whole area keeps having.
    # `make_eyebrows.py` sat broken at head and nobody noticed because a
    # generator nobody runs fails silently and its asset simply stops moving.
    # The maths self-test cannot see that: it was green while the shipped cards
    # were self-intersecting bowties.
    if args.check:
        current = (OUT / "coils.obj").read_text(encoding="utf-8") if (OUT / "coils.obj").exists() else ""
        if current != obj:
            print("stale generated asset: data/hair/coils.obj -- re-run make_coils.py",
                  file=sys.stderr)
            return 1
        print("coils: the shipped asset matches a fresh run")
        return 0
    try:
        (OUT / "coils.obj").write_text(obj, encoding="utf-8")
        (OUT / "coils.mhclo").write_text(mhclo, encoding="utf-8")
    except OSError as exc:
        print(f"coils: cannot write: {exc}", file=sys.stderr)
        return 1
    print(f"coils: wrote {(OUT / 'coils.obj').relative_to(ROOT)} and .mhclo")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
