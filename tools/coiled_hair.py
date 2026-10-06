#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-or-later
"""Geometry for highly coiled hair: helices, phase locking, switchbacks.

    ./.venv-mh/bin/python tools/coiled_hair.py --selftest

AGPL on purpose, and that is the owner's decision (2026-10-06): the same
copyleft Blender ships under, which does not stop anyone selling the product
(`LICENSING.md`: "Can I sell the application itself? **Yes.**").

PROVENANCE, because it matters here. The methods are those described in
Wu, Shi, Darke and Kim, "Curly-Cue: Geometric Methods for Highly Coiled Hair",
SIGGRAPH Asia 2024 (doi 10.1145/3680528.3687641), and this is written FROM THE
PAPER. The authors' `switchbackGen.cpp` is GPL-3.0-or-later and was deliberately
NOT read: implementing from a published description is clean provenance, while
reading someone's source and then writing it "differently" is what makes a
derivative work arguable. Their sibling `curlyCueGuidesToFull` is MIT and was
likewise not copied. Algorithms are not copyrightable; expression is.

WHAT COILED HAIR NEEDS THAT WAVY HAIR DOES NOT. A loose curl is a low-frequency
curve and interpolating between two of them looks fine. Coiled hair is a
HIGH-FREQUENCY HELIX, and averaging two helices a half-period apart gives a
straight line -- the curl cancels itself. That is the paper's central
observation and it is why hair near the scalp cannot simply be blended toward a
guide. Three consequences are modelled here:

  * PHASE LOCKING. Near the scalp each hair coils on its own phase, which reads
    as a "spongy" mass; further out the hairs of a clump fall into step and
    become one visible curl. A strand therefore carries its own phase at the
    root and the clump's phase outward, blended between.
  * A LOOSELY-GUIDED REGION. Up to `loose`, a strand only eases toward its
    guide; past it, it follows the guide's centreline. The boundary derivatives
    are what stop a visible kink where the two meet.
  * SWITCHBACKS, a.k.a. helical perversions: a coil reverses handedness
    part-way along. They are everywhere in tightly coiled hair and are the
    thing that reads as "not a spring".

NO NUMPY. These tools run in CI, which has no wheels -- the same constraint
`make_hair_alpha.py` records for Pillow. The vector maths here is a dozen lines
and does not earn a dependency.
"""
import argparse
import math
import random
import sys

def add(a, b):
    return (a[0] + b[0], a[1] + b[1], a[2] + b[2])


def sub(a, b):
    return (a[0] - b[0], a[1] - b[1], a[2] - b[2])


def scale(a, s):
    return (a[0] * s, a[1] * s, a[2] * s)


def dot(a, b):
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]


def cross(a, b):
    return (a[1] * b[2] - a[2] * b[1],
            a[2] * b[0] - a[0] * b[2],
            a[0] * b[1] - a[1] * b[0])


def length(a):
    return math.sqrt(dot(a, a))


def normalise(a, fallback=(0.0, 1.0, 0.0)):
    n = length(a)
    if n < 1e-12:
        return fallback
    return (a[0] / n, a[1] / n, a[2] / n)


def catmull_rom(points, samples_per_span):
    """A C1 piecewise cubic Hermite (Catmull-Rom) through `points` (paper 3.1.1).

    Interior tangents are the centred difference (p[i+1] - p[i-1]) / 2. The ends
    cannot use that -- there is no p[-1] -- so they take the one-sided form the
    paper gives rather than a zero tangent, which would otherwise flatten the
    first and last spans and put a visible straight stub at every hair root.
    """
    n = len(points)
    if n < 2:
        raise ValueError(f"a spline needs at least two points, got {n}")
    if samples_per_span < 1:
        raise ValueError(f"samples_per_span must be >= 1, got {samples_per_span}")

    def tangent(i):
        if 0 < i < n - 1:
            return scale(sub(points[i + 1], points[i - 1]), 0.5)
        if i == 0:
            return sub(points[1], points[0])
        return sub(points[n - 1], points[n - 2])

    out = []
    for i in range(n - 1):
        p0, p1 = points[i], points[i + 1]
        m0, m1 = tangent(i), tangent(i + 1)
        last = samples_per_span if i == n - 2 else samples_per_span - 1
        for k in range(last + 1):
            t = k / samples_per_span
            t2, t3 = t * t, t * t * t
            h00 = 2 * t3 - 3 * t2 + 1
            h10 = t3 - 2 * t2 + t
            h01 = -2 * t3 + 3 * t2
            h11 = t3 - t2
            out.append(add(add(scale(p0, h00), scale(m0, h10)),
                           add(scale(p1, h01), scale(m1, h11))))
    return out


def parallel_frames(curve):
    """A non-twisting frame per sample (paper 3.1.2).

    PARALLEL TRANSPORT, not Frenet. A Frenet frame is defined by the curve's
    second derivative, so it spins wildly wherever the curve is locally
    straight and FLIPS through an inflection -- and a coiled hair is nothing but
    inflections. Transporting one frame along the curve instead keeps the
    reference stable, which is what stops a hair ribbon from twisting on the
    spot.
    """
    if len(curve) < 2:
        raise ValueError("need at least two samples to build frames")
    frames = []
    tangent = normalise(sub(curve[1], curve[0]))
    ref = (0.0, 0.0, 1.0)
    if abs(dot(ref, tangent)) > 0.9:
        ref = (1.0, 0.0, 0.0)
    u = normalise(sub(ref, scale(tangent, dot(ref, tangent))))
    for i in range(len(curve)):
        if i > 0:
            nxt = curve[min(i + 1, len(curve) - 1)]
            prv = curve[i - 1]
            new_t = normalise(sub(nxt, prv), tangent)
            # Rotate u by the same rotation that takes the old tangent to the
            # new one, which is what "parallel" means here.
            axis = cross(tangent, new_t)
            if length(axis) > 1e-9:
                axis = normalise(axis)
                angle = math.atan2(length(cross(tangent, new_t)), dot(tangent, new_t))
                c, s = math.cos(angle), math.sin(angle)
                u = add(add(scale(u, c), scale(cross(axis, u), s)),
                        scale(axis, dot(axis, u) * (1.0 - c)))
            tangent = new_t
            u = normalise(sub(u, scale(tangent, dot(u, tangent))), u)
        frames.append((u, cross(tangent, u), tangent))
    return frames


def phase_at(t, own_phase, clump_phase, lock_start, lock_end):
    """Where this hair sits in its coil at arc fraction `t` (phase locking).

    Near the scalp a hair coils on its own phase; outward it falls into the
    clump's. Blending the PHASE rather than the positions is the whole point:
    averaging two helices half a period apart cancels the coil and yields a
    straight line, which is the failure the paper opens with.

    The blend uses a smoothstep so the hair does not visibly snap into step at
    `lock_start`.
    """
    if lock_end <= lock_start:
        raise ValueError(f"lock window must be non-empty, got {lock_start}..{lock_end}")
    w = (t - lock_start) / (lock_end - lock_start)
    w = max(0.0, min(1.0, w))
    w = w * w * (3.0 - 2.0 * w)
    # Shortest way round, or a hair locking from 6.1 rad to 0.2 rad unwinds
    # almost a full turn on its way rather than nudging across the wrap.
    delta = (clump_phase - own_phase + math.pi) % (2.0 * math.pi) - math.pi
    return own_phase + delta * w


# How much of the strand a switchback takes to reverse through.
#
# WIDE ENOUGH TO LAND ON THE SAMPLE GRID. At 0.035 the whole reversal spanned
# 0.07 against a card's sample spacing of 1/15 = 0.0667, so the pinch fell
# between samples and never appeared: measured over the real generation stream,
# 59% of switchback strands never dipped below half radius, and the neck this
# exists to draw was simply not in the mesh. 0.08 spans about two and a half
# samples.
BLEND = 0.08


def chirality_at(t, switchbacks, blend=BLEND):
    """Handedness at `t`, reversing smoothly at each switchback.

    A switchback (helical perversion) is where a coil changes hand. Physically
    it is where two opposing twists meet, and the coil must pass through a
    straight, uncoiled neck to get there -- so the sign is interpolated rather
    than flipped, and the amplitude taper that goes with it is
    `amplitude_at` below. A hard flip gives a visible kink and a hair that
    looks snapped rather than coiled.
    """
    # ACCUMULATED, not returned from inside the loop. Returning on the first
    # switchback whose blend window contains `t` drops any LATER one that lands
    # within 2*blend of it, so the sign reverses once where it should reverse
    # twice. MEASURED on the real generation stream: 113 of 420 strands draw
    # two switchbacks and 22 of those are closer than 0.07 apart -- those
    # strands ended on the wrong handedness while `amplitude_at`, which
    # correctly uses `min` over all of them, still pinched at both. A coil that
    # necks down and does not reverse is the one shape a perversion never makes.
    sign = 1.0
    for s in sorted(switchbacks):
        if t >= s + blend:
            sign = -sign
        elif t > s - blend:
            w = (t - (s - blend)) / (2.0 * blend)
            sign = sign * (1.0 - 2.0 * (w * w * (3.0 - 2.0 * w)))
    return sign


def amplitude_at(t, switchbacks, blend=BLEND):
    """The coil's radius, pinched to zero at each switchback.

    This is the geometric content of a perversion: the helix cannot swap hands
    at full radius, it has to neck down and open out again. Without the pinch
    the reversal reads as a crease.
    """
    a = 1.0
    for s in switchbacks:
        d = abs(t - s)
        if d < blend:
            # LINEAR to the neck. An exponent below 1 makes the pinch
            # SHALLOWER near the centre, not sharper -- 0.4**0.6 is 0.577
            # against a linear 0.4 -- so the coil recovered before the next
            # sample and the neck never appeared: 0.591 at the closest sampled
            # point where 0.5 is the bar. The physical shape is a coil that
            # necks to nothing and opens out again, which is what this is.
            a = min(a, d / blend)
    return a


def guided_centreline(root, guide, loose, samples):
    """A strand's centreline: eased toward its guide, then locked to it (3.3.3).

    Up to `loose` the hair is only loosely influenced by its guide -- this is
    the "spongy" scalp layer, and the paper is explicit that dropping it makes
    hair read as a wig rather than as growth. Past `loose` the centreline IS the
    guide's, which is what makes a clump coalesce into one curl.

    The boundary tangent at `loose` is taken from the guide so the two halves
    meet without a kink; the tangent at the root aims at the guide's midpoint,
    scaled by the span, which is the paper's condition and which keeps the hair
    leaving the scalp in a plausible direction rather than lunging at its guide.
    """
    if not 0.0 < loose < 1.0:
        raise ValueError(f"loose must be strictly inside (0, 1), got {loose}")
    if len(guide) < 4:
        raise ValueError(f"a guide needs at least four samples, got {len(guide)}")

    n = len(guide) - 1
    at = lambda f: guide[max(0, min(n, int(round(f * n))))]  # noqa: E731
    c_loose = at(loose)
    c_half = at(loose * 0.5)
    span = length(sub(c_loose, root))

    m0 = scale(normalise(sub(c_half, root)), span)
    j = max(1, min(n - 1, int(round(loose * n))))
    m1 = scale(normalise(sub(guide[j + 1], guide[j - 1])), span)

    out = []
    knot = max(2, int(round(loose * samples)))
    for k in range(knot):
        # `k / knot` ALREADY runs 0..1 across the loose region. Dividing it by
        # `loose` again -- which this did -- drives the Hermite parameter to
        # 1/loose, up to 4, and a cubic evaluated that far outside its interval
        # does not merely extrapolate, it explodes. MEASURED before the fix: a
        # strand whose root sat 0.61 dm from its guide reached 11.24 dm, and a
        # head of them rendered as spikes several head-radii long.
        s = k / knot
        s2, s3 = s * s, s * s * s
        h00 = 2 * s3 - 3 * s2 + 1
        h10 = s3 - 2 * s2 + s
        h01 = -2 * s3 + 3 * s2
        h11 = s3 - s2
        out.append(add(add(scale(root, h00), scale(m0, h10)),
                       add(scale(c_loose, h01), scale(m1, h11))))
    for k in range(knot, samples):
        out.append(at(k / (samples - 1)))
    return out


# How far along the strand the coil opens to full radius. A hair emerges from
# the scalp at the follicle's own diameter, not at full curl radius; starting
# at full radius plants every hair a visible step off the skin.
ROOT_FADE = 0.08


def coil(centreline, radius, turns, own_phase, clump_phase,
         lock_start=0.15, lock_end=0.55, switchbacks=()):
    """Wrap a helix around a centreline: the finished hair.

    """
    if radius < 0.0:
        raise ValueError(f"radius must not be negative, got {radius}")
    frames = parallel_frames(centreline)
    out = []
    n = len(centreline) - 1
    # INTEGRATED, not multiplied. The twist is the running total of a SIGNED
    # RATE, so a switchback changes the direction the coil winds from there on.
    # Multiplying the accumulated angle by the instantaneous sign instead --
    # `chirality_at(t) * turns * 2pi * t`, which is what this did -- mirrors
    # every turn already laid down, so the hair does not reverse, it jumps.
    # MEASURED on the generator's own grid with one switchback at t=0.5:
    # consecutive samples at +1.40 and -1.59 turns, a 2.99-turn swing inside a
    # single card segment, at 0.971 of full radius. That is the crease the
    # amplitude pinch exists to prevent, drawn at full width.
    twist = 0.0
    prev_t = 0.0
    for i, p in enumerate(centreline):
        t = i / n if n else 0.0
        twist += chirality_at(t, switchbacks) * turns * 2.0 * math.pi * (t - prev_t)
        prev_t = t
        u, v, _ = frames[i]
        ph = phase_at(t, own_phase, clump_phase, lock_start, lock_end)
        ang = ph + twist
        amp = radius * amplitude_at(t, switchbacks) * min(1.0, t / ROOT_FADE)
        out.append(add(p, add(scale(u, math.cos(ang) * amp),
                              scale(v, math.sin(ang) * amp))))
    return out


def selftest() -> int:
    """Properties that are known by construction, so a failure is unambiguous."""
    failures = []

    def check(name, ok, detail=""):
        if not ok:
            failures.append(f"{name}: {detail}")

    # A spline through collinear points stays collinear.
    line = catmull_rom([(0, 0, 0), (1, 0, 0), (2, 0, 0), (3, 0, 0)], 4)
    check("catmull_rom straight", max(abs(p[1]) + abs(p[2]) for p in line) < 1e-9,
          "a straight input bent")
    check("catmull_rom endpoints",
          length(sub(line[0], (0, 0, 0))) < 1e-9 and length(sub(line[-1], (3, 0, 0))) < 1e-9,
          f"ends {line[0]} {line[-1]}")

    # Frames stay orthonormal; that is what makes a ribbon not shear.
    curve = catmull_rom([(0, 0, 0), (1, 1, 0), (2, 0, 1), (3, 1, 1)], 8)
    for u, v, t in parallel_frames(curve):
        check("frame orthonormal",
              abs(length(u) - 1) < 1e-6 and abs(dot(u, t)) < 1e-6 and abs(dot(u, v)) < 1e-6,
              "frame is not orthonormal")

    # Phase locking: own phase at the root, the clump's once locked.
    check("phase at root", abs(phase_at(0.0, 1.0, 2.5, 0.2, 0.6) - 1.0) < 1e-9, "root phase moved")
    check("phase locked", abs(phase_at(1.0, 1.0, 2.5, 0.2, 0.6) - 2.5) < 1e-9, "never locked")
    # The wrap is the case worth pinning: 0.1 -> 6.2 rad is a nudge backwards,
    # not a 6.1 rad unwind.
    locked = phase_at(1.0, 0.1, 6.2, 0.2, 0.6)
    check("phase wraps the short way", abs(((locked - 6.2 + math.pi) % (2 * math.pi)) - math.pi) < 1e-6,
          f"landed {locked}")

    # A switchback reverses handedness and pinches the radius to zero.
    check("chirality before", chirality_at(0.1, (0.5,)) > 0.99, "not right-handed before")
    check("chirality after", chirality_at(0.9, (0.5,)) < -0.99, "did not reverse")
    check("amplitude pinches", amplitude_at(0.5, (0.5,)) < 1e-6, "no pinch at the switchback")
    check("amplitude recovers", amplitude_at(0.9, (0.5,)) > 0.99, "never reopened")

    # ON THE SAMPLE GRID, which is where the previous two checks did not look.
    # `amplitude_at(0.5, (0.5,)) < 1e-6` probes the switchback point exactly --
    # a value `coil()` never evaluates -- so it passed while the shipped cards
    # sailed through the reversal at full radius. These sample the way a card
    # does and assert what the card actually sees.
    grid = catmull_rom([(0, 0, 0), (0, 1, 0), (0, 2, 0), (0, 3, 0)], 5)
    gn = len(grid) - 1
    sampled = [amplitude_at(i / gn, (0.5,)) for i in range(gn + 1)]
    check("the pinch lands on a sampled point", min(sampled) < 0.5,
          f"narrowest sampled amplitude {min(sampled):.3f} -- the neck falls between samples")

    turned = coil(grid, 0.1, 3, 0.0, 0.0, switchbacks=(0.5,))
    centre = [grid[i] for i in range(len(grid))]
    angles = []
    for i, q in enumerate(turned):
        d = sub(q, centre[i])
        angles.append(math.atan2(d[2], d[0]))
    steps = []
    for i in range(1, len(angles)):
        step = abs((angles[i] - angles[i - 1] + math.pi) % (2.0 * math.pi) - math.pi)
        steps.append(step)
    # Three turns over 15 spans is 0.4 pi a span; a reversal that JUMPS instead
    # of turning shows up as a step far larger than that.
    check("no jump at the switchback", max(steps) < 1.6,
          f"largest per-sample twist step {max(steps):.2f} rad -- the coil jumped rather than reversed")

    # Two hairs a half-period apart must NOT cancel: this is the failure the
    # paper opens with, and the reason phase is blended rather than position.
    guide = catmull_rom([(0, 0, 0), (0, 1, 0), (0, 2, 0), (0, 3, 0)], 10)
    a = coil(guide, 0.1, 6, 0.0, 0.0, switchbacks=())
    b = coil(guide, 0.1, 6, math.pi, 0.0, switchbacks=())
    # SAMPLED PAST THE LOCK, and the first version of this check was sampled
    # before it and failed -- correctly. Inside the loosely-guided region two
    # hairs are MEANT to be out of phase; that disorder is the spongy scalp
    # layer, and averaging there cancels the coil exactly as the paper says it
    # must. The property worth pinning is that locking undoes it: once the two
    # have fallen into step, their mean is still a coil and not a line.
    late = int(0.92 * (len(a) - 1))
    averaged = length(sub(scale(add(a[late], b[late]), 0.5), guide[late]))
    single = length(sub(a[late], guide[late]))
    check("phase locking coalesces the clump", averaged > 0.9 * single,
          f"locked hairs still cancel: {averaged:.4f} vs {single:.4f}")

    early = int(0.03 * (len(a) - 1))
    spongy = length(sub(scale(add(a[early], b[early]), 0.5), guide[early]))
    check("the scalp layer stays disordered", spongy < 0.5 * single,
          f"hairs locked at the root, so there is no spongy layer: {spongy:.4f}")

    # A guided strand starts at its root and ends on its guide.
    strand = guided_centreline((0.5, 0.0, 0.5), guide, 0.4, 24)
    check("guided starts at root", length(sub(strand[0], (0.5, 0.0, 0.5))) < 1e-9,
          f"started {strand[0]}")
    check("guided ends on guide", length(sub(strand[-1], guide[-1])) < 1e-6,
          f"ended {strand[-1]} not {guide[-1]}")
    # IT MUST NOT OVERSHOOT, and the bound is measured rather than guessed.
    # The loose region is a cubic from the root to the guide point AT `loose`,
    # so nothing on it should wander far past that chord. MEASURED on this
    # guide, correct code reaches 0.87..0.91 of the chord; the Hermite-parameter
    # bug reaches 2.37x at loose=0.40 and 12.0x at loose=0.25. 1.5 separates
    # those cleanly.
    #
    # The FIRST version of this check compared against the distance to the
    # guide's END instead, which is more than twice the chord, and so passed
    # on the very bug it was written for -- the mutation ran green.
    root_pt = (0.5, 0.0, 0.5)
    for loose_t in (0.25, 0.40):
        probe = guided_centreline(root_pt, guide, loose_t, 24)
        n_g = len(guide) - 1
        chord = length(sub(guide[max(0, min(n_g, int(round(loose_t * n_g))))], root_pt))
        # `round`, the way `guided_centreline` sizes its own loose region. With
        # `int` the last Hermite sample -- the largest `s`, where a
        # mis-parameterised cubic diverges most -- fell outside the slice and
        # was never examined.
        knot = max(2, int(round(loose_t * 24)))
        reach = max(length(sub(q, root_pt)) for q in probe[:knot])
        check(f"guided does not overshoot (loose={loose_t})", reach < 1.5 * chord,
              f"reached {reach:.3f} on a chord of {chord:.3f} ({reach / chord:.2f}x)")

    # Refusals.
    for bad, fn in (("two points", lambda: catmull_rom([(0, 0, 0)], 4)),
                    ("loose=0", lambda: guided_centreline((0, 0, 0), guide, 0.0, 10)),
                    ("negative radius", lambda: coil(guide, -1.0, 2, 0.0, 0.0))):
        try:
            fn()
            check(f"refuses {bad}", False, "accepted bad input")
        except ValueError:
            pass

    for f in failures:
        print(f"FAIL {f}", file=sys.stderr)
    if failures:
        print(f"{len(failures)} coiled-hair checks failed", file=sys.stderr)
        return 1
    print("coiled hair: all checks passed")
    return 0


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--selftest", action="store_true", help="run the property checks")
    args = ap.parse_args()
    if args.selftest:
        return selftest()
    ap.print_help()
    return 0


if __name__ == "__main__":
    random.seed(0)
    raise SystemExit(main())
