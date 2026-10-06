#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-or-later
"""Generates `data/eyebrows/*` -- a brow of individual hairs, not a ridge.

    MH_APP=<makehuman> ./.venv-mh/bin/python tools/make_eyebrow_hairs.py

WHY THE RIDGE HAD TO GO. The brow was a swept tube: one arc, six facets,
wearing an alpha. Every attempt to make it look like hair failed in the same
way, and the renders say why. A strand texture across six facets aliased into
a speckled checkerboard. Widening the strands until the speckle went made it a
solid dark patch again. At 2.2 mm proud it has almost no silhouette to read, so
there is nothing for shading to describe -- the thing a viewer recognises as an
eyebrow is not a shape, it is HAIRS LYING IN A DIRECTION.

So the brow is built the way the hair is now: individual strands, each its own
card. That is the same engine `make_coils.py` uses, with the curl turned almost
all the way down.

THE DIRECTION FIELD IS THE ALGORITHM. A brow is not a patch of parallel hairs;
its flow changes along its length, and that change is most of what makes one
read as a brow rather than as a smudge:

  * THE HEAD, at the nose, grows almost straight UP. These are the longest
    hairs in the brow and they splay.
  * THE BODY sweeps outward and slightly up, the hairs lying flatter as they
    go, which is what gives the arch its direction.
  * THE TAIL turns DOWN and outward, and the hairs shorten. A brow that keeps
    pointing up at the tail reads as a drawn line rather than as hair.

Hairs also alternate which way they lean about that mean direction -- upper
ones comb down, lower ones comb up, so the two sets interleave in the middle.
That interleaving is what makes the body of a brow look dense without being
solid.

EVERYTHING IS PROJECTED ONTO THE FACE, not trusted from the formula. The
surface above the eye is not a sphere, and the ridge generator this replaces
recorded what assuming otherwise cost: a projection with nothing to aim at
piles every sample onto whatever protrudes most.
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
OUT = ROOT / "data" / "eyebrows"

# The arc, inherited from the ridge generator this replaces, where it was
# measured against the face rather than drawn: the surface above the eye runs
# from x 0.06, z 1.52 near the midline out to x 0.61, z 1.14 at the temple, and
# `helper-l-eye` centres at (0.308, 7.284, 1.245) spanning y 7.146..7.422.
BROW_Y = 7.47
BROW_X0 = 0.11
BROW_X1 = 0.50
ARCH_AT = 0.62
ARCH = 0.055

# The same box the ridge used, for the same two jobs: choosing the triangles
# the arc is PROJECTED onto, and -- through `--bind-region` -- the triangles it
# is BOUND to. Letting those differ once put the binding on the scalp while the
# projection stayed on the brow, and every record carried a 37.8 mm offset.
REGION_X = 0.80
REGION_Y0, REGION_Y1 = 7.25, 7.95
REGION_Z0, REGION_Z1 = 0.80, 4.00
BIND_REGION = (f"{-REGION_X},{REGION_X},{REGION_Y0},{REGION_Y1},"
               f"{REGION_Z0},{REGION_Z1}")

# A real brow carries roughly 250 hairs a side. 150 RENDERED AS SCATTERED
# DASHES rather than as a brow -- a brow reads dense even where you can pick
# out single hairs, so the count has to exceed the anatomy to survive being
# drawn at a dozen pixels. 330 a side is 6,600 faces for the pair, against the
# ridge's 252 and the afro's 16,500 beside it.
# 580 a side, up from 330. The references read as a MASS with a clean outline,
# not as countable hairs: at 330 the body was a thin smear with skin showing
# through it at head framing. 580 is 11,600 faces for the pair.
HAIRS = 580
# 8, not 5. A bowed hair drawn with five segments is four straight facets, and
# at this width the corners between them are visible -- which is the other half
# of why the brow read as sticks. Eight costs 3,680 vertices on the pair and
# makes the arc read as a curve.
SEGMENTS = 8

# A brow hair is 4..10 mm, so 0.04..0.10 dm -- and WHERE the long ones sit was
# backwards. This file had the head carrying the longest hairs, tapering
# monotonically to the tail. Every reference in
# `references/.../eye-brow-pictures` -- two drawn sheets, two photographs --
# shows the opposite: the head's hairs are SHORT and fan out, the longest hairs
# are in the body sweeping toward the tail, and the tail's converge to a point.
#
# That inversion is what "the beginning is messy" was. Long hairs at the head,
# steep and splayed, leave the brow's outline and read as a spray of loose
# bristles over the nose bridge. Nothing else about the brow had to change.
LEN_HEAD = 0.052
LEN_PEAK = 0.104
LEN_TAIL = 0.045
# Where along the brow the longest hairs sit, measured off the sheets: just
# inboard of the arch, around a third of the way out.
LEN_PEAK_AT = 0.38
# Half the width of one hair, in dm. A real brow hair is about 0.1 mm, and
# drawing it at that scale is why the first attempts looked sparse despite
# carrying 660 of them: at 0.22 mm a hair is SUB-PIXEL in a 1024 render, so
# antialiasing averages it into the skin and most of the brow disappears. 0.8
# mm is wider than anatomy and is what makes a hair survive being drawn -- the
# same reason a hair card in any real-time groom is far wider than a hair.
# 0.0062, up from 0.0040. The references read as a SOLID dark shape that
# happens to be made of hairs; ours read as hairs that happen to be near each
# other, because the body never closed up. The fix is WIDTH, not count -- the
# same thing the rope styles' skullcap needed on the same day. A wider ribbon
# occludes more for exactly the same vertex count, where another 200 hairs a
# side would have cost 4,800 vertices to close the same gaps.
#
# Still far under a real hair's apparent width at this framing: a brow hair is
# about 0.1 mm and this is 1.0 mm, because a ribbon seen near edge-on shows a
# fraction of its width and the alpha sheet eats the rest.
#
# Backed off from 0.0062 once the hairs lay DOWN. At that width, standing off
# the skin, each one was its own visible stick; lying along the surface they
# overlap, so the mass closes up at a width that no longer draws the eye to any
# single hair. Density is carried by the count instead, which is what should
# have carried it.
HALF_WIDTH = 0.0052
# Half the brow's thickness at its widest, in dm. MEASURED from the reference
# as length : thickness = 5.6 : 1; this arc runs about 0.40 dm, so the full
# thickness is 0.071 and half of it is this. The first version used 0.023,
# which is 8.7 : 1 -- a brow drawn that thin reads as a pencil line.
SPREAD = 0.036
# Hairs lie close to the skin. This is the standoff of the root, in dm.
STAND = 0.004

SEED = 20261006


def arc_point(t):
    """A point on the brow's centre line at `t` along it, before projection."""
    x = BROW_X0 + (BROW_X1 - BROW_X0) * t
    u = t / ARCH_AT if t <= ARCH_AT else (1.0 - t) / (1.0 - ARCH_AT)
    y = BROW_Y + ARCH * math.sin(max(0.0, min(1.0, u)) * math.pi / 2.0) ** 2
    return x, y


# THE THICKNESS PROFILE, MEASURED off a reference sheet of drawn brows rather
# than drawn by eye (`references/`, gitignored -- it is licensed stock art and
# is used here as a ruler, not as content).
#
# Thresholding one brow and reading its height column by column gives a shape
# that is NOT the symmetric lens the first version used:
#
#     length : max thickness = 5.6 : 1,  peak at t = 0.40..0.50
#     t      0.0  0.1  0.2  0.3  0.5  0.6  0.7  0.8  0.9  1.0
#     frac   0.00 0.56 0.82 0.94 0.97 0.81 0.74 0.64 0.45 0.10
#
# It rises STEEPLY out of the head, holds a broad plateau through the arch,
# then tapers for the whole second half to a point. `sin(pi*t)**0.65` -- the
# symmetric lens -- is too thin at t=0.1 (0.46 against 0.56) and far too thick
# at t=0.9 (0.46 against 0.45 measured only because the reference's tail hairs
# spike there; the body is thinner still). A brow tapers to its tail, and a
# symmetric profile makes the tail look clubbed.
#
# The table is interpolated rather than fitted to a curve: a fit would be a
# second approximation on top of the measurement for no gain.
_PROFILE = ((0.00, 0.10), (0.10, 0.56), (0.20, 0.82), (0.30, 0.94),
            (0.50, 0.97), (0.60, 0.81), (0.70, 0.74), (0.80, 0.64),
            (0.90, 0.45), (1.00, 0.10))


def spread_at(t):
    """Half the brow's thickness at `t`, from the measured profile."""
    t = max(0.0, min(1.0, t))
    for i in range(1, len(_PROFILE)):
        t1, f1 = _PROFILE[i]
        if t <= t1:
            t0, f0 = _PROFILE[i - 1]
            k = (t - t0) / (t1 - t0) if t1 > t0 else 0.0
            return SPREAD * (f0 + (f1 - f0) * k)
    return SPREAD * _PROFILE[-1][1]


def flow(t, lean):
    """The direction a hair at `t` grows, as an unnormalised (x, y, z).

    THIS IS THE PART THAT MAKES IT A BROW. The mean direction rotates along the
    arc -- up at the head, outward through the body, down at the tail -- and
    `lean` tilts an individual hair off that mean so the upper and lower hairs
    interleave instead of lying parallel.

    The angles are from the anatomy rather than tuned by eye: brow hairs at the
    head stand within about 20 degrees of vertical, the body lies 20 to 40
    degrees above horizontal, and the tail falls 10 to 25 degrees below it.
    """
    if t < 0.22:
        # The head: STEEP AND SWEPT, not vertical. Measured off
        # `references/.../eye-brow-pictures` -- two drawn sheets and two
        # photographs agree that the inner hairs rise at roughly 45 to 60
        # degrees and ALREADY LEAN TOWARD THE TAIL. None of the four has a
        # vertical hair in it.
        #
        # 78 degrees was close enough to vertical that the head read as a spray
        # standing clear of the brow's own outline: "the beginning is messy and
        # not clean like the references". Rendered at head framing it is a
        # cowlick, and it is the first thing the eye lands on.
        deg = 62.0 - 44.0 * (t / 0.22)
    elif t < 0.62:
        # The body: flattening through the arch.
        deg = 18.0 - 14.0 * ((t - 0.22) / 0.40)
    else:
        # The tail: turning over and down.
        deg = 4.0 - 26.0 * ((t - 0.62) / 0.38)
    deg += lean
    rad = math.radians(deg)
    # x is outward along the face, y is up. z (depth) is left to the projection.
    return (math.cos(rad), math.sin(rad), 0.0)


def surface_at(tris, x, y):
    """The face, just in front of (x, y), and its normal.

    TWO STEPS, and the first is not optional: `project_to_region` returns the
    CLOSEST point, so probing from far in front puts every sample on whatever
    protrudes most -- the ridge beside the nose -- and the whole brow collapses
    toward the midline. Find the surface height near this (x, y) from the
    region's own vertices first, then project from just in front of it.
    """
    near = min(((vx - x) ** 2 + (vy - y) ** 2, vz)
               for tri in tris for (vx, vy, vz) in tri[:3])
    return H.project_to_region((x, y, near[1] + 0.25), tris)


def hair(tris, t, across, lean, rng):
    """One brow hair, as a list of points from root to tip."""
    cx, cy = arc_point(t)
    # Step across the arc, perpendicular to it in the x-y plane. The arc's own
    # slope matters: near the head it climbs steeply, so "across" is not simply
    # vertical.
    ax, ay = arc_point(min(1.0, t + 0.01))
    bx, by = arc_point(max(0.0, t - 0.01))
    tx, ty = ax - bx, ay - by
    tl = math.hypot(tx, ty) or 1.0
    nx, ny = -ty / tl, tx / tl
    px = cx + nx * across * spread_at(t)
    py = cy + ny * across * spread_at(t)

    root, normal, _d = surface_at(tris, px, py)
    root = C.add(root, C.scale(normal, STAND))

    direction = flow(t, lean)
    # Normalise in the x-y plane and let the surface normal supply the lift off
    # the face, so a hair follows the brow ridge instead of burrowing into it.
    dl = math.hypot(direction[0], direction[1]) or 1.0
    grow = (direction[0] / dl, direction[1] / dl, 0.0)
    # Mirror the growth for the right brow later; here x is the LEFT side, where
    # outward is +x.
    # Two straight ramps rather than a curve: the measurement is three points
    # and fitting anything smoother to it would be invention.
    if t < LEN_PEAK_AT:
        base = LEN_HEAD + (LEN_PEAK - LEN_HEAD) * (t / LEN_PEAK_AT)
    else:
        base = LEN_PEAK + (LEN_TAIL - LEN_PEAK) * ((t - LEN_PEAK_AT) / (1.0 - LEN_PEAK_AT))
    # 0.82..1.18, not 0.75..1.25. The long tail of that spread was a quarter
    # again the mean, and those are precisely the hairs that cleared the
    # outline and read as strays.
    length = base * rng.uniform(0.82, 1.18)
    # NO TUFT MULTIPLIER. There used to be one here growing the first eighth by
    # half again, on the reading that the reference's head "stands clear of the
    # body's outline". The length profile above now carries the head's length
    # directly, and the fan comes from `lean` spreading the hairs apart, which
    # is what the sheets actually show. A second length rule on top of the
    # profile only put the spray back.

    pts = []
    for i in range(SEGMENTS + 1):
        s = i / SEGMENTS
        # A hair bows: it leaves the skin, then lies back DOWN toward it. The
        # second term used to be `+ s * 0.35`, which is not a bow -- it is a
        # ramp, and it lifted the tip further off the face the longer the hair
        # got. Every hair therefore ended standing clear of the skin, and a brow
        # of them read as "sticks put on sand": separate rigid spines planted in
        # the surface rather than hair lying along it.
        #
        # Now the arc peaks mid-hair and the tip SETTLES, finishing nearer the
        # skin than it started climbing. That is what makes neighbouring hairs
        # overlap into a mass instead of standing apart.
        lift = math.sin(s * math.pi) * 0.30 - s * 0.12
        along = C.scale(grow, length * s)
        out = C.scale(normal, length * lift * 0.45)
        pts.append(C.add(C.add(root, along), out))
    return pts


def ribbon(points, half_width, normal):
    """A hair widened into a card, facing away from the face."""
    out, uvs, quads = [], [], []
    n = len(points) - 1
    for i, p in enumerate(points):
        nxt = points[min(i + 1, n)]
        prv = points[max(i - 1, 0)]
        tangent = C.normalise(C.sub(nxt, prv), (0.0, 1.0, 0.0))
        across = C.cross(tangent, normal)
        if C.length(across) < 1e-6:
            across = C.cross(tangent, (1.0, 0.0, 0.0))
        across = C.normalise(across, (1.0, 0.0, 0.0))
        t = i / n
        # The hair tapers to a point; a card that stops square reads as a stick.
        w = half_width * (1.0 - 0.75 * t)
        out.append(C.sub(p, C.scale(across, w)))
        out.append(C.add(p, C.scale(across, w)))
        uvs.append((0.0, t))
        uvs.append((1.0, t))
    for i in range(n):
        a = 2 * i
        quads.append([a, a + 1, a + 3, a + 2])
    return out, quads, uvs


def build(app_path=""):
    rng = random.Random(SEED)
    verts, faces = H.read_base()
    used = sorted({v for f in faces for v in f})
    region = [v for v in used
              if abs(verts[v][0]) < REGION_X
              and REGION_Y0 < verts[v][1] < REGION_Y1
              and REGION_Z0 < verts[v][2] < REGION_Z1]
    tris = H.region_triangles(verts, region, faces)
    if not tris:
        raise SystemExit("no triangles in the brow region; the box is wrong")

    pts, quads, uvs = [], [], []
    for i in range(HAIRS):
        # DENSER THROUGH THE BODY, and the first version did the exact
        # opposite while claiming this. `0.5 - 0.5*cos(pi*t)` has a derivative
        # of (pi/2)*sin(pi*t), which is ZERO at both ends -- so it piles
        # samples up at t=0 and t=1. Measured over 100k draws the deciles ran
        # [20553, 8964, 7195, 6856, 6472, 6512, 6691, 7258, 8852, 20647]:
        # 3.2x denser at the head and tail than through the arch, which is
        # precisely where `_PROFILE` puts the brow's greatest THICKNESS. The
        # brow clumped at the nose and thinned through its own widest part.
        #
        # A triangular density does what the comment always said: weight each
        # draw by the profile itself, so hairs land where the brow is thick.
        # Rejection sampling rather than an inverse CDF -- the profile is a
        # measured table, and two draws beat inverting it.
        while True:
            t = rng.random()
            if rng.random() <= spread_at(t) / SPREAD:
                break
        across = rng.uniform(-1.0, 1.0)
        # Upper hairs comb DOWN and lower ones comb UP, so the two sets
        # interleave through the middle instead of lying parallel.
        # THE HEAD IS THE TIDIEST PART OF A BROW, and a constant jitter made it
        # the opposite. The same +/-7 degrees reads as texture on the flat body
        # and as a scribble on hairs that are steep -- which is the other half
        # of why the beginning looked messy. Tapered, so the head is combed and
        # the tail keeps the scatter the references show there.
        lean = -22.0 * across + rng.uniform(-7.0, 7.0) * (0.35 + 0.65 * t)
        # THE OUTLINE IS COMBED, THE INTERIOR IS NOT. In the references the
        # brow's top edge is a smooth swept line and the loose, crossing hairs
        # are all INSIDE it -- an edge hair that stands up is a stray, and a
        # brow drawn with a hundred of them reads as ragged however good its
        # shape is. Rendered at head framing that was the last thing separating
        # this from the sheets: the mass was right and the silhouette was
        # spiky.
        #
        # Hairs near the upper edge (`across` toward +1) get their lean pulled
        # down toward the brow's own direction, so they lie along the outline
        # instead of crossing it. Interior hairs are untouched, because the
        # interleaving they give is what stops the brow reading as painted.
        if across > 0.45:
            lean -= 16.0 * ((across - 0.45) / 0.55)
        strand = hair(tris, t, across, lean, rng)
        _p, normal, _d = surface_at(tris, *arc_point(t))
        hp, hq, hu = ribbon(strand, HALF_WIDTH * rng.uniform(0.7, 1.4), normal)
        base = len(pts)
        pts.extend(hp)
        uvs.extend(hu)
        quads.extend([[k + base for k in q] for q in hq])

    # The right brow is the left mirrored in x. Mirroring the RESULT rather than
    # re-projecting keeps the pair identical, which is what a face wants;
    # re-running the projection would let floating point make them differ.
    n = len(pts)
    allpts = list(pts) + [(-x, y, z) for x, y, z in pts]
    alluvs = list(uvs) + list(uvs)
    # Winding flips under a mirror, or every right-hand face points into the
    # head and backface culling eats the brow.
    allquads = list(quads) + [list(reversed([k + n for k in q])) for q in quads]

    print(f"eyebrows: {len(allpts)} vertices, {len(allquads)} faces, "
          f"{HAIRS * 2} hairs, {SEGMENTS} segments each")

    binds = H.bind_points(app_path or H.app_binary(), allpts, BIND_REGION)
    if len(binds) != len(allpts):
        raise SystemExit(f"{len(binds)} bindings for {len(allpts)} points")
    return allpts, allquads, alluvs, binds


MHMAT = """# Generated by tools/make_eyebrow_hairs.py.
#
# A SINGLE-STRAND sheet, because each card here is ONE HAIR. Every other hair
# material in this tree cuts a surface into many strands, since its geometry is
# a tube or a shell carrying a whole clump. A brow hair is its own card, so the
# texture it wears is opaque down the middle of `u` with soft edges, fading
# along `v` so the hair comes to a point.
#
# `transparent True` is load-bearing: without the flag the renderer discards
# the alpha and paints an opaque rectangle per hair, which is the bristle look
# this replaces.
#
# Darker than scalp hair on purpose: brows read darker on the same head,
# because they are thinner and sit against lit skin rather than massed against
# a silhouette.
name Eyebrows
tag MakeHuman™
ambientColor 0.03 0.02 0.02
diffuseColor 0.16 0.11 0.08
specularColor 0.2 0.2 0.2
shininess 0.12
opacity 1.0
transparent True
diffuseTexture ../brow_hair.png
backfaceCull False
castShadows True
receiveShadows True
shader data/shaders/glsl/litsphere
"""


def write(points, faces, uvs, bindings):
    import uuid

    obj = [H.BANNER.replace("make_hair_styles", "make_eyebrow_hairs"),
           f"# Eyebrows: {len(points)} vertices, {len(faces)} faces.", "g eyebrows"]
    for x, y, z in points:
        obj.append(f"v {x:.6f} {y:.6f} {z:.6f}")
    if len(uvs) != len(points):
        raise SystemExit(
            f"eyebrows: {len(uvs)} uvs for {len(points)} vertices -- refusing to "
            f"write a mesh that cannot carry its strand alpha")
    for u, v in uvs:
        obj.append(f"vt {u:.6f} {v:.6f}")
    for f in faces:
        obj.append("f " + " ".join(f"{i + 1}/{i + 1}" for i in f))

    mhclo = [
        H.BANNER.replace("make_hair_styles", "make_eyebrow_hairs"),
        "#",
        "# AUTHORED geometry bound to the FACE by three base vertices,",
        "# barycentric weights and an offset -- `mh::core::bindToSurface` via",
        "# `--bind-points --bind-region`. The region matters: binding to the",
        "# scalp put every vertex 0.5 dm above the arc with an offset that",
        "# `fitProxy` scales but never rotates.",
        "name Eyebrows",
        f"uuid {uuid.uuid5(uuid.NAMESPACE_URL, 'makehuman/eyebrows/eyebrows')}",
        "basemesh hm08",
        "obj_file eyebrows.obj",
        "material materials/eyebrows.mhmat",
        "z_depth 55",
        "verts 0",
    ]
    mhclo.extend(bindings)
    return "\n".join(obj) + "\n", "\n".join(mhclo) + "\n"


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--app", default="", help="the makehuman binary to bind with")
    ap.add_argument("--check", action="store_true",
                    help="report whether the shipped asset matches a fresh run")
    args = ap.parse_args()

    try:
        pts, quads, uvs, binds = build(args.app)
    except SystemExit:
        raise
    except Exception as exc:  # noqa: BLE001 -- the tool must name what broke
        print(f"eyebrows: cannot build: {exc}", file=sys.stderr)
        return 1

    obj, mhclo = write(pts, quads, uvs, binds)
    if args.check:
        current = (OUT / "eyebrows.obj").read_text(encoding="utf-8") \
            if (OUT / "eyebrows.obj").exists() else ""
        if current != obj:
            print("stale generated asset: data/eyebrows/eyebrows.obj", file=sys.stderr)
            return 1
        print("eyebrows: the shipped asset matches a fresh run")
        return 0

    try:
        (OUT / "eyebrows.obj").write_text(obj, encoding="utf-8")
        (OUT / "eyebrows.mhclo").write_text(mhclo, encoding="utf-8")
        (OUT / "materials" / "eyebrows.mhmat").write_text(MHMAT, encoding="utf-8")
    except OSError as exc:
        print(f"eyebrows: cannot write: {exc}", file=sys.stderr)
        return 1
    print(f"eyebrows: wrote {(OUT / 'eyebrows.obj').relative_to(ROOT)}, .mhclo and .mhmat")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
