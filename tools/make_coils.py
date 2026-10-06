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
# 150 clumps rendered as separate wisps with scalp showing between them, and
# 420 still left a bald crown and a gap at the back. A coiled head is a MASS;
# the count has to cover the scalp before anything else about it reads.
# 900 x 36 quads is 32,400 faces -- ten times locs, and the price of a style
# whose geometry IS the hair rather than a painted shell. It was 15 quads a
# clump until the close-up showed what five samples a turn draws.
# SAMPLES PER TURN of the helix, and this is the number that decides whether a
# coil reads as a curl or as a zigzag. A card can only draw the polygon its
# samples describe: at `n` samples a turn the ribbon is an n-gon, and the facet
# it shows is `radius * (1 - cos(pi/n))` deep.
#
# This file used to carry a FIXED 15 segments and a comment claiming it
# "resolves about three turns before the ribbon starts to alias into a zigzag".
# RENDERED at head framing, three turns on fifteen segments IS the zigzag. The
# shipped afro measured 5.0 samples per turn, a mean kink of 85 degrees between
# consecutive segments and a worst of 137 -- not hair, a mass of bent wire.
# The error was treating the segment count as a budget to be spent once, when
# it is a SAMPLING RATE and the thing being sampled is `turns`.
#
# 12 puts the facet at 1 - cos(15 deg) = 3.4% of the coil radius: 0.4 mm on the
# afro's 11.5 mm coil, below what the render resolves. At the old 5 it was 19%,
# or 2.2 mm, which is what the eye was picking up.
SAMPLES_PER_TURN = 12
# A clump of coiled hair is a few millimetres across. 0.055 dm is 5.5 mm, which
# at 150 clumps covers the scalp without the cards visibly overlapping.
# How far the hair stands off the head, in dm. The afro's shell is 0.78 at the
# crown; a coil that reaches the same envelope reads as the same haircut.
# Turns per strand. Coiled hair is a HIGH-frequency helix -- this is the
# property the whole method exists for -- but a card cannot draw a coil finer
# than its own segments, and 15 segments resolves about 3 turns before the
# ribbon starts to alias into a zigzag. Measured by rendering: at 6 turns the
# cards self-intersect into noise.
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
GUIDE_CHOICES = 3

# Where along a card it starts narrowing to a point. A clump of hair comes to
# a tip, and a ribbon that stops square is the "braid ends are cut square"
# limitation this project already records.
TAPER_FROM = 0.55

# A card is two columns wide in `u`; the scalp's 52-strand sheet aliases across
# it into a grid. See `materials/hair_card.mhmat`.
CARD_MATERIAL = "materials/hair_card.mhmat"

SEED = 20261006


class Style:
    """One coiled haircut: the same engine, different numbers.

    The engine is `coiled_hair.py`; what separates a low cut from an afro is
    how far the hair travels, how tightly it coils and how much of it there is.
    Keeping them as a table rather than as four near-copies of the generator is
    the whole reason the engine was written as pure functions.
    """

    def __init__(self, stem, name, clumps, guides, length, turns, radius,
                 half_width, droop, segments=0, uuid_override="", edge=None):
        self.stem = stem
        self.name = name
        self.clumps = clumps
        self.guides = guides
        self.length = length
        self.turns = turns
        self.radius = radius
        self.half_width = half_width
        self.droop = droop
        # An optional cut applied to each sampled root: see `lineup_edge`.
        self.edge = edge
        # DERIVED from `turns`, because the segment count is the rate at which
        # the helix is sampled and nothing else. A style is free to override it,
        # but no style should have to know the sampling rule.
        self.segments = segments or int(math.ceil(turns * SAMPLES_PER_TURN))
        # Only the afro has one: it existed before this generator and .mhm files
        # already name it. See `write_bound_style`.
        self.uuid_override = uuid_override


# MEASURED AGAINST THE SCALP, not invented. The hair-bearing region is about
# 2.0 dm across; a clump 5.5 mm wide needs roughly 900 of them to cover it
# without gaps, which rendering 150, 420 and 900 in turn is how the number was
# found -- 420 still left a bald crown and a hole in the back.
#
# `turns` is now FREE of the card. It used to be bounded by a fixed segment
# count -- the comment here claimed fifteen segments carried three turns, and
# the render showed three turns on fifteen segments aliasing into a zigzag at
# 85 degrees a joint. The segment count is derived from `turns` instead
# (`SAMPLES_PER_TURN`), so a style picks the hair it wants and pays for it in
# vertices rather than in facets.
# THE ROPE STYLES' SKULLCAP, grown rather than shelled. Locs and dreadlocks are
# worn on a head OF HAIR and 41 ropes cannot cover a scalp 1.5 dm wide, so
# something has to fill between them. That something used to be an offset copy
# of the scalp with the strand sheet painted on, and it showed: a staircase
# boundary along the mesh edges over the ear, a planar UV projection smearing
# the sheet into vertical bands, and scalp colour coming through between them.
#
# This is a `lowcut` that stops even shorter. It is NOT exported as an asset of
# its own -- `make_hair_styles` grows it and merges it into each rope style.
# WIDE CARDS, and the width is doing the work rather than the count. MEASURED:
# at half_width 0.042 the bed already covered the scalp -- worst gap 0.058 dm,
# no bare spot anywhere -- and the scalp still read bright orange through it.
# Coverage is not occlusion. The gap metric asks how far a scalp point is from
# the nearest hair VERTEX; what the camera sees is how much solid angle the
# ribbons actually block, and a thin ribbon seen near edge-on blocks almost
# none. Widening 0.042 -> 0.075 is 79% more ribbon for exactly the same vertex
# count, where raising the clump count to 2000 would have cost 30,000 vertices
# to move the gap metric from 0.058 to 0.046 -- an improvement in the number
# that was never the problem.
# THE SHAPE-UP, which is what makes a clean cut clean. A barber squares the
# hairline off: a straight horizontal line across the forehead and a sharp
# corner where it meets the natural line at the temple. Everything behind the
# temple is left alone, because that is what a line-up actually is -- the front
# edge is cut, the rest of the head is not.
#
# MEASURED against the mesh rather than chosen: the natural front hairline sits
# at y 7.939 (`hairline(0) = +12 degrees of elevation`), so a line at 7.93 sits
# level with where the hair already starts instead of shaving the forehead
# higher or dragging it down over the brow at 7.47.
LINEUP_Y = 7.93
# How far round the head the straight cut runs before handing back to the
# natural hairline. 72 degrees reaches the temple and stops.
LINEUP_AZIM = 72.0


def lineup_edge(pos):
    """True where a lined-up cut keeps hair: a straight front, natural behind."""
    elev, azim = H.spherical(pos)
    if abs(azim) <= LINEUP_AZIM:
        return pos[1] >= LINEUP_Y
    return elev >= H.hairline(azim)


CAP_BED = Style("ropecap", "Rope cap", clumps=1400, guides=360, length=0.17,
                turns=2.0, radius=0.026, half_width=0.075, droop=0.02)

STYLES = (
    Style("coils", "Coils", clumps=900, guides=230, length=0.72, turns=3.0,
          radius=0.085, half_width=0.055, droop=0.30),
    # THE AFRO, built from coils instead of from a shell. The shell version was
    # one offset copy of the scalp with hair painted on by an alpha: cheap, and
    # no texture makes it read as hair close up, because the geometry has no
    # hairs in it. This reaches the same envelope the shell did -- 0.78 dm at
    # the crown -- with coils that are actually there.
    Style("afro", "Afro", clumps=1100, guides=280, length=0.98, turns=3.2,
          radius=0.115, half_width=0.060, droop=0.18,
          # ITS OLD IDENTITY, kept. The shell afro shipped as
          # `8f2c1d4a-hair-afro` and saved models name it by that; letting this
          # generator mint a fresh uuid5 would orphan every one of them.
          uuid_override="8f2c1d4a-hair-afro"),
    # A LOW CUT: the hair is barely off the scalp, so the coils are tight and
    # short and the mass reads as texture rather than as volume. Fewer, wider
    # cards -- at this length a clump is a few millimetres of hair and drawing
    # it with a narrow ribbon just makes it disappear.
    Style("lowcut", "Low cut", clumps=1000, guides=320, length=0.20, turns=2.2,
          radius=0.030, half_width=0.050, droop=0.03),
    # WISPS: the paper's looser end, where hairs coalesce into long defined
    # curls rather than a mass. Fewer guides means more hairs to a wisp, which
    # is what makes a curl read as one object.
    Style("wisps", "Wisps", clumps=700, guides=90, length=1.15, turns=2.4,
          radius=0.140, half_width=0.075, droop=0.55),
    # A CLEAN CUT: a short afro squared off at the front. The hair is a dense,
    # even, low pile -- what reads is the EDGE, not the volume, so this leans on
    # `lineup_edge` for the shape and keeps the coils tight and short enough
    # that the surface stays flat instead of breaking into separate curls.
    #
    # The clump count is what it is BEFORE the cut: `lineup_edge` rejects every
    # root in front of the line, so the style keeps roughly the share of the
    # scalp that survives it. Asking for 2,600 is how ~2,000 land.
    # `segments` is overridden rather than derived, and this is the one style
    # where that is right. The derived rule (`SAMPLES_PER_TURN`) sizes the
    # sampling to the CURL, which is correct when the curl is what you see --
    # the afro's coils are 11.5 mm across and its facets showed. These are 4 mm
    # and the hair is 26 mm long, so a coil covers a few pixels at head framing
    # and no one can resolve its facets. Derived, it cost 152,460 vertices for a
    # style whose whole point is a flat even pile; 16 brings it in line with the
    # afro at 88,000 and changes nothing anybody can see.
    Style("cleancut", "Clean cut", clumps=2600, guides=520, length=0.26,
          turns=2.4, radius=0.040, half_width=0.062, droop=0.05,
          segments=16, edge=lineup_edge),
)


def scalp_roots(verts, faces, count, rng, edge=None):
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

    # STRATIFIED, not independent. Sampling `count` points independently is a
    # Poisson process: it clumps and leaves holes, and RENDERED that is exactly
    # what showed -- a bald crown, a bare patch above the ear and a gap in the
    # middle of the back. Allocating each triangle its own share of the roots
    # and jittering WITHIN it spreads them evenly, because the gaps a Poisson
    # process leaves are between triangles, not inside them.
    #
    # The remainder is carried rather than rounded away: with 420 roots over
    # ~230 triangles most shares are fractional, and rounding each one down
    # loses a third of the hair.
    roots = []
    carried = 0.0
    for tri, area in zip(tris, [areas[0]] + [areas[i] - areas[i - 1]
                                             for i in range(1, len(areas))]):
        share = count * area / total + carried
        take = int(share)
        carried = share - take
        a, b, c = tri[0], tri[1], tri[2]
        for _ in range(take):
            # The square-root form, which is the one that is actually uniform
            # over a triangle; plain (r1, r2) bunches every root toward one
            # corner.
            r1, r2 = rng.random(), rng.random()
            sq = math.sqrt(r1)
            w0, w1, w2 = 1.0 - sq, sq * (1.0 - r2), sq * r2
            q = (a[0] * w0 + b[0] * w1 + c[0] * w2,
                 a[1] * w0 + b[1] * w1 + c[1] * w2,
                 a[2] * w0 + b[2] * w1 + c[2] * w2)
            # THE CUT IS APPLIED TO THE SAMPLED POINT, not to the region's
            # vertices, and that is the whole reason a barber's line reads as
            # one. Clipping the vertex region would make the edge follow mesh
            # edges, which is a staircase at the mesh's own resolution -- the
            # exact fault the rope styles' shell cap had along the temple. A
            # point test puts the edge wherever the line says, as sharp as the
            # root density allows.
            if edge is not None and not edge(q):
                continue
            roots.append(q)
    if not roots:
        raise SystemExit("the hair-bearing region produced no roots")
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


def guide_curve(root, droop, rng, length, segments):
    """A clump's guide: out from the scalp, then falling under its own weight.

    Four control points, which is the minimum `guided_centreline` accepts and
    enough for the single bend that hair makes -- out, over, down.
    """
    # HAIR DOES NOT GROW PERPENDICULAR TO THE SCALP. It leaves the follicle at
    # an acute angle, sweeping up and back over the skin, and at a hairline that
    # is the whole reason the forehead is covered at all.
    #
    # Growing it radially left a MEASURED bare band. Counting hair vertices in
    # front of each scalp point from the front camera, the strip at elevation
    # +0..+15 -- the one just above the hairline -- averaged 10.6 with a minimum
    # of ZERO, against 34..103 everywhere else on the same face. Radial growth
    # there points the strand straight at the viewer, so a card is edge-on and
    # occludes nothing, and no hair from above falls across it.
    #
    # `TILT` degrees DOWN the skull fixes it: the strand lies over the scalp
    # instead of pointing out of it, so the band above the hairline is covered
    # by the hair rooted in it. See `TILT` for the measurement, and for why the
    # opposite sign -- which is what the anatomy argument suggested -- is worse.
    radial = outward(root)
    # The tangential direction up the skull: `up` with its radial part removed.
    # A negative `TILT` then leans the strand the other way, down the skull.
    # Degenerate only at the crown, where `radial` IS up -- and there a tilt has
    # no meaning, so the strand keeps growing radially.
    up = (0.0, 1.0, 0.0)
    dot_up = C.dot(up, radial)
    tang = C.sub(up, C.scale(radial, dot_up))
    if C.length(tang) > 1e-3:
        tang = C.normalise(tang)
        a = math.radians(TILT)
        d = C.normalise(C.add(C.scale(radial, math.cos(a)), C.scale(tang, math.sin(a))))
    else:
        d = radial
    tip = C.add(root, C.scale(d, length))
    tip = (tip[0], tip[1] - droop, tip[2])
    mid = C.add(root, C.scale(d, length * 0.55))
    mid = (mid[0], mid[1] - droop * 0.25, mid[2])
    near = C.add(root, C.scale(d, length * 0.22))
    jitter = lambda s: (rng.uniform(-s, s), rng.uniform(-s, s), rng.uniform(-s, s))  # noqa: E731
    return C.catmull_rom(
        [root, near, C.add(mid, jitter(0.02)), C.add(tip, jitter(0.03))], segments
    )


# How far off the surface normal a strand grows, in degrees, and NEGATIVE means
# down the skull -- away from the crown, the way hair falls.
#
# The sign was the whole question and reasoning got it backwards. The argument
# for tilting UP toward the crown was that hair sweeps up and back from a
# hairline; measured, that made the bare band WORSE, because it carries each
# strand's mass away from the strip its own root sits in. Tilting DOWN lays the
# strand over that strip instead.
#
# MEASURED on the afro, hair vertices in front of each front-facing scalp point
# in the band just above the hairline (elevation +0..+15):
#
#     TILT   mean   min
#     +38     5.8     0     toward the crown -- worse than not tilting
#       0    10.6     0     radial: the bare patch
#     -25    54.8    15
#     -38    58.5    39
#     -50    63.8    39
#
# -38 and -50 both score higher than this, and both were REJECTED BY THE
# RENDER rather than by the number: at 38 degrees the front curls hang over the
# eyebrows and onto the face, which no afro does. -25 closes the bare patch --
# the minimum goes from nothing to 15, the mean from 10.6 to 54.8 -- and keeps
# the hairline where a hairline belongs. The occlusion count was the right
# instrument for finding the fault and the wrong one for choosing the value.
TILT = -25.0

AZ_BINS, EL_BINS = 64, 32
# Below this the mesh is jaw, neck and shoulder rather than skull. Measured on
# the base mesh: the chin sits at y 6.66, the cranium centre at 7.75.
JAW_Y = 6.9


def skull_radii(verts, faces):
    """The head's radius from the cranium centre, binned by direction.

    A LOOKUP, not a search. Pushing hair back outside the skull needs "how far
    is the head in this direction" for every one of ~35,000 vertices, and
    comparing each against every base vertex is tens of millions of distance
    tests in pure Python. Binning the base mesh once by azimuth and elevation
    makes it a constant-time read, and a head is convex enough at this
    resolution that the MAXIMUM radius in a bin is the surface.
    """
    grid = [0.0] * (AZ_BINS * EL_BINS)
    used = {v for f in faces for v in f}
    for b in used:
        q = verts[b]
        # THE HEAD, which is what the docstring says and what the previous
        # version did not do: `read_base()` returns the whole body, so the grid
        # held arm, torso and foot radii -- measured, max 16.12 dm against the
        # skull's 1.68. Today's styles never land in a torso bin, but a longer
        # or droopier one would, and `push_outside` would fling that vertex a
        # decimetre outward with nothing to stop it. The jaw is the boundary.
        if q[1] < JAW_Y:
            continue
        dx, dy, dz = q[0] - H.CENTRE[0], q[1] - H.CENTRE[1], q[2] - H.CENTRE[2]
        r = math.sqrt(dx * dx + dy * dy + dz * dz)
        if r < 1e-6:
            continue
        az = int(((math.atan2(dx, dz) / (2.0 * math.pi) + 0.5) % 1.0) * AZ_BINS)
        el = int(min(0.999, max(0.0, math.acos(max(-1.0, min(1.0, dy / r))) / math.pi)) * EL_BINS)
        i = el * AZ_BINS + min(AZ_BINS - 1, az)
        if r > grid[i]:
            grid[i] = r
    return grid


# How many samples at the root are drawn inside the head, and how deep the
# first one goes. 0.035 dm is 3.5 mm -- more than the half-width of any card
# this generator makes, so the open edge cannot peek back through the scalp on
# a curved part of the skull.
# How far a buried point has to sit outside the skull before this pass stops
# moving it. One caller, one setting.
CLEARANCE = 0.012
ROOT_BURY = 2
ROOT_DEPTH = 0.035


def bury_root(points):
    """Pull the first samples inside the skull, so the strand emerges from it.

    Radially inward about the cranium centre, fading over `ROOT_BURY` samples
    so the strand leaves the skin smoothly instead of kinking at the surface.
    """
    out = list(points)
    for i in range(min(ROOT_BURY, len(out))):
        depth = ROOT_DEPTH * (1.0 - i / float(ROOT_BURY))
        d = C.sub(out[i], H.CENTRE)
        r = C.length(d)
        if r > 1e-6:
            out[i] = C.add(out[i], C.scale(d, -depth / r))
    return out


def push_outside(points, grid, protect=0):
    """Move any point that sits inside the skull back out onto it.

    WHY THIS IS NEEDED AT ALL. A guide droops -- that is what stops an afro
    sitting like a helmet -- and a drooping tip near the ear or the nape can
    travel back INTO the head. MEASURED before this pass: 790 of 35,200 afro
    vertices inside the skull, the deepest by 0.230 dm. It renders as hair
    emerging from nowhere beside bare scalp, and no vertex count sees it.

    The point is pushed along its own direction from the cranium centre, which
    keeps the strand's shape and only lifts what was buried.
    """
    out = []
    moved = 0
    for i, q in enumerate(points):
        # THE FIRST FEW SAMPLES ARE MEANT TO BE INSIDE. This pass exists to lift
        # a drooping TIP out of the head; applied to the root it did the
        # opposite of what anatomy wants, pinning every strand's first vertex
        # `CLEARANCE` ABOVE the skin. MEASURED on the shipped assets: 100% of
        # sampled roots sat outside, median 1.2 mm -- which is exactly this
        # constant -- so each card's open root edge floated clear of the scalp
        # and read as a cut stub rather than as hair growing out of a head.
        if i < protect:
            out.append(q)
            continue
        dx, dy, dz = q[0] - H.CENTRE[0], q[1] - H.CENTRE[1], q[2] - H.CENTRE[2]
        r = math.sqrt(dx * dx + dy * dy + dz * dz)
        if r < 1e-6:
            out.append(q)
            continue
        az = int(((math.atan2(dx, dz) / (2.0 * math.pi) + 0.5) % 1.0) * AZ_BINS)
        el = int(min(0.999, max(0.0, math.acos(max(-1.0, min(1.0, dy / r))) / math.pi)) * EL_BINS)
        surface = grid[el * AZ_BINS + min(AZ_BINS - 1, az)]
        if surface > 0.0 and r < surface + CLEARANCE:
            k = (surface + CLEARANCE) / r
            out.append((H.CENTRE[0] + dx * k, H.CENTRE[1] + dy * k, H.CENTRE[2] + dz * k))
            moved += 1
        else:
            out.append(q)
    return out, moved


def card(centre, half_width, axis):
    """A coiled centreline widened into a ribbon of quads.

    The ribbon faces OUTWARD, not along the curve's own frame. A parallel frame
    is stable but arbitrary about the tangent, so using it directly lets
    neighbouring cards face randomly and the hair flickers between bright and
    edge-on.

    OUTWARD FROM THE COIL'S OWN AXIS when `axis` is given -- the centreline the
    helix was wrapped around -- and only from the HEAD when it is not. Facing
    every quad away from the head is right for a shell and wrong for a coil,
    because near the scalp a coil's own axis IS the outward direction: the
    ribbon then lies flat in the plane of the helix and draws a flat spiral.
    RENDERED from the side that is unmistakable -- the hair above the ear was a
    field of flat discs, like rosettes pinned to the skull, instead of a mass of
    tubes. Facing away from the axis instead makes the ribbon twist with the
    helix, so a coil presents its width from every direction and reads round.

    Where the helix has no width to speak of -- the root fade, and the neck of a
    switchback -- the offset is too short to give a direction, and there the
    head's own outward vector is still the best answer available.
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
        out = ()
        if axis:
            radial = C.sub(p, axis[i])
            # A tenth of a millimetre. Below that the offset is numerical noise
            # and normalising it would point the card anywhere.
            if C.length(radial) > 1e-3:
                out = C.normalise(radial)
        if not out:
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


def grow(style):
    """One style's GEOMETRY: (points, quads, uvs). No binder, no files.

    Split out of `build` so a style that is not an asset of its own can use it.
    The rope styles in `make_hair_styles` grow their skullcap with this: a cap
    under locs or dreadlocks is short hair, and short hair here is the same
    coiled cards every other style is made of. Before that it was an offset
    SHELL of the scalp with a strand texture painted on, which is the approach
    `derive()` already records as settled -- "no texture makes a shell read as
    hair close up, because the geometry has no hairs in it". The shell cap was
    the last of them.
    """
    rng = random.Random(SEED)
    verts, faces = H.read_base()
    roots = scalp_roots(verts, faces, style.clumps, rng, style.edge)

    # The guides take the SAME cut. A guide rooted outside the line would pull
    # strands across it, and a clump locks to its guide past the lock point --
    # so a cut that stopped at the roots would be undone by the next step.
    guide_roots = scalp_roots(verts, faces, style.guides, random.Random(SEED + 1),
                              style.edge)
    guides = [guide_curve(r, style.droop * rng.uniform(0.85, 1.15), rng, style.length,
                          style.segments) for r in guide_roots]

    allpts, allquads, alluvs = [], [], []
    switchback_total = 0
    buried_total = 0
    skull = skull_radii(verts, faces)
    for root in roots:
        # Noisy Voronoi (paper 3.3.1): the nearest few guides, chosen between at
        # random. Strictly nearest makes the cells read as hard partings.
        near = sorted(range(len(guides)),
                      key=lambda g: C.length(C.sub(guides[g][0], root)))[:GUIDE_CHOICES]
        guide = guides[rng.choice(near)]

        centre = C.guided_centreline(root, guide, loose=rng.uniform(0.25, 0.45),
                                     samples=style.segments + 1)
        # Switchbacks are common in tightly coiled hair but not universal, and
        # one per strand at a random place is what keeps a head of them from
        # pulsing in unison.
        backs = tuple(sorted(rng.uniform(0.25, 0.85) for _ in range(rng.choice((0, 1, 1, 2)))))
        switchback_total += len(backs)
        radius = style.radius * rng.uniform(0.75, 1.25)
        # LIFT THE CENTRELINE BY ITS OWN COIL RADIUS. `coil()` swings the strand
        # perpendicular to the centreline, so half of every turn goes INWARD --
        # and where the centreline hugs the scalp, that half is inside the
        # skull. MEASURED before this: 1,615 of 35,200 afro vertices inside the
        # head, the deepest by 0.257 dm. It renders as hair sprouting out of
        # nowhere with bare scalp beside it.
        #
        # Lifting by `radius * amplitude(t)` is exact rather than generous: it
        # is the inward excursion at that point, so the coil ends up TANGENT to
        # the skin instead of cutting it, and the roots still meet the scalp
        # because the amplitude ramps from zero at t = 0.
        lifted = []
        for i, q in enumerate(centre):
            tt = i / (len(centre) - 1)
            clear = radius * C.amplitude_at(tt, backs) * min(1.0, tt / C.ROOT_FADE)
            lifted.append(C.add(q, C.scale(outward(q), clear)))
        centre = lifted
        coiled = C.coil(
            centre,
            radius=radius,
            turns=style.turns * rng.uniform(0.85, 1.15),
            own_phase=rng.uniform(0.0, 2.0 * math.pi),
            clump_phase=rng.uniform(0.0, 2.0 * math.pi),
            switchbacks=backs,
        )
        # BURY THE ROOT. A hair emerges FROM the skin; you never see the end of
        # one. The strand's first samples are pulled inside the skull so the
        # card's open edge is hidden by the scalp, and `protect` stops the pass
        # below putting them straight back out.
        coiled = bury_root(coiled)
        coiled, buried = push_outside(coiled, skull, protect=ROOT_BURY)
        buried_total += buried
        pts, quads, uvs = card(coiled, style.half_width * rng.uniform(0.8, 1.2),
                               axis=centre)
        base = len(allpts)
        allpts.extend(pts)
        alluvs.extend(uvs)
        allquads.extend([[i + base for i in q] for q in quads])

    print(f"{style.stem}: {len(allpts)} vertices, {len(allquads)} faces, "
          f"{len(roots)} clumps, {switchback_total} switchbacks, {len(guides)} guides, "
          f"{buried_total} vertices lifted out of the skull")
    return allpts, allquads, alluvs


def build(style, app_path=""):
    allpts, allquads, alluvs = grow(style)
    binds = H.bind_points(app_path or H.app_binary(), allpts)
    if len(binds) != len(allpts):
        raise SystemExit(
            f"the binder returned {len(binds)} bindings for {len(allpts)} points"
        )
    return allpts, allquads, alluvs, binds


def write_style(style, app, check):
    """Generate one style; with `check`, report staleness instead of writing."""
    try:
        pts, quads, uvs, binds = build(style, app)
    except SystemExit:
        raise
    except Exception as exc:  # noqa: BLE001 -- the tool must name what broke
        print(f"{style.stem}: cannot build: {exc}", file=sys.stderr)
        return 1

    obj, mhclo = H.write_bound_style(style.name, style.stem, pts, quads, binds, uvs,
                                     material=CARD_MATERIAL,
                                     uuid_override=style.uuid_override)
    # `write_bound_style` stamps the sibling generator's banner on everything it
    # writes. Anyone refreshing this asset would run that tool, which does not
    # touch these styles, and conclude the file was current.
    # BOTH files, not just the .obj. The previous version rewrote the banner on
    # the geometry and left all four .mhclo files opening with "Generated by
    # tools/make_hair_styles.py", which is the tool that does NOT touch them --
    # the exact misdirection the rewrite exists to prevent, fixed halfway.
    obj = obj.replace("tools/make_hair_styles.py", "tools/make_coils.py", 1)
    mhclo = mhclo.replace("tools/make_hair_styles.py", "tools/make_coils.py", 1)

    # STALENESS, which is the failure this whole area keeps having.
    # `make_eyebrows.py` sat broken at head and nobody noticed, because a
    # generator nobody runs fails silently and its asset simply stops moving.
    # The maths self-test cannot see it either: that was green while the
    # shipped cards were self-intersecting bowties.
    # EVERY FILE, not just the geometry. Checking only the .obj left the
    # .mhclo ungated -- and the .mhclo is where the bindings, the material
    # reference and the UUID live. The afro's identity changed in the same
    # change that moved it to an ungated file, which is exactly the hand-edit
    # this is supposed to notice.
    written = {f"{style.stem}.obj": obj, f"{style.stem}.mhclo": mhclo}
    if check:
        stale = [n for n, text in written.items()
                 if not (OUT / n).exists() or (OUT / n).read_text(encoding="utf-8") != text]
        if stale:
            for n in stale:
                print(f"stale generated asset: data/hair/{n} -- re-run make_coils.py",
                      file=sys.stderr)
            return 1
        print(f"{style.stem}: {len(written)} shipped files match a fresh run")
        return 0

    try:
        for n, text in written.items():
            (OUT / n).write_text(text, encoding="utf-8")
    except OSError as exc:
        print(f"{style.stem}: cannot write: {exc}", file=sys.stderr)
        return 1
    print(f"{style.stem}: wrote {len(written)} files under data/hair")
    return 0


def main() -> int:
    # argparse, like `make_hair_styles.py`, and not a partition on "=". The
    # hand-rolled form accepted `--app=X` and SILENTLY DROPPED `--app X`,
    # leaving the binary to fall back to MH_APP or to a hard-coded debug path
    # -- the exact hazard tests/CMakeLists.txt records for the sibling tool,
    # whose own CMake invocation uses the space form.
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--app", default="", help="the makehuman binary to bind with")
    ap.add_argument("--check", action="store_true",
                    help="report whether the shipped assets match a fresh run")
    ap.add_argument("--style", default="", help="one style's stem, or all of them")
    args = ap.parse_args()

    wanted = [s for s in STYLES if not args.style or s.stem == args.style]
    if not wanted:
        raise SystemExit(f"no such style: {args.style} "
                         f"(have {', '.join(s.stem for s in STYLES)})")
    failures = sum(write_style(s, args.app, args.check) for s in wanted)
    return 1 if failures else 0


if __name__ == "__main__":
    raise SystemExit(main())
