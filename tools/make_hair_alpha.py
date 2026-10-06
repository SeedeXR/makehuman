#!/usr/bin/env python3
"""The alpha-cut strand textures for scalp hair, eyebrows and eyelashes.

    ./.venv-mh/bin/python tools/make_hair_alpha.py

WHAT THE MAPPING MEANS, because the texture is useless without it. Every style
is a swept tube and `u` runs AROUND the tube while `v` runs ALONG it. So a
pattern that varies in u and holds in v cuts the tube lengthwise into ribbons
that run the way the hair grows. Varying it the other way would band the hair
into rungs like a caterpillar.

WHY THIS WAS REWRITTEN (2026-10-06). The first version cut every tube into
EIGHT strands of identical width, all running the full length at 80% duty.
RENDERED and looked at, an afro under it is a ribbed barrel: eight vertical
bands down a perfectly smooth dome, every band reaching the hem together, and
a row of sharp triangular teeth where the cut meets the hairline. Nothing
about it reads as hair, and no amount of geometry fixes a texture problem.

Three things separate hair from a comb, and all three are free at runtime --
this is the same 256x256 texture either way, which is the whole point of
fixing it here rather than in geometry:

  * STRANDS END AT DIFFERENT PLACES. A clump whose hairs all stop on the same
    line is a brush. Varying each strand's tip is what breaks the silhouette,
    and it is the single biggest difference in the render.
  * STRANDS ARE NOT ALL THE SAME WIDTH, and they are not evenly spaced. A
    regular comb reads as manufactured at any distance.
  * A STRAND DRIFTS as it travels. Dead-straight parallel lines are extruded
    plastic; a few texels of lateral wander over the length is enough.

THE ROOT BAND IS NOT DECORATION. Hair meets the scalp as a mass, not as
separate hairs, so the first few percent of v is solid. Without it the alpha
cuts all the way to the hairline and the gaps between strands show skin --
which is the triangular-teeth artefact the old texture had.

DETERMINISTIC ON PURPOSE. These files are committed, so a random one would
churn the repo on every run and make every golden fixture a coin toss. A
seeded `random.Random` is enough: Python guarantees that `random()` keeps
producing the same sequence for the same seed across versions, and `uniform`
is built on it. This carried a hand-rolled LCG for a while on the belief that
the guarantee did not exist; it does.
"""
import math
import random
import struct
import sys
import zlib
from pathlib import Path
from typing import NamedTuple

ROOT = Path(__file__).resolve().parent.parent

SIZE = 256


class Strand(NamedTuple):
    """One hair in the texture: where it sits, how wide, and where it stops."""

    centre: float
    half_width: float
    tip: float
    drift: float
    phase: float
    lum: float


def build_strands(count, seed, width_scale, min_tip, max_tip, drift_max):
    """Lay `count` strands around the tube, jittered off the regular grid.

    Starting from an even spacing and pushing each strand off it keeps the
    coverage roughly uniform -- pure random placement leaves bald patches and
    clumps at this density, which reads as a texture bug rather than as hair.
    """
    if count < 1:
        raise ValueError(f"a strand texture needs at least one strand, got {count}")
    if not 0.0 < min_tip <= max_tip <= 1.0:
        raise ValueError(f"tips must satisfy 0 < min <= max <= 1, got {min_tip}..{max_tip}")

    rng = random.Random(seed)
    pitch = 1.0 / count
    out = []
    for i in range(count):
        centre = (i + rng.uniform(-0.35, 0.35)) * pitch
        half = pitch * 0.5 * width_scale * rng.uniform(0.55, 1.35)
        out.append(
            Strand(
                centre=centre % 1.0,
                half_width=half,
                tip=rng.uniform(min_tip, max_tip),
                drift=rng.uniform(-drift_max, drift_max),
                phase=rng.uniform(0.0, 2.0 * math.pi),
                lum=rng.uniform(0.80, 1.0),
            )
        )
    return out


def wrapped_delta(a: float, b: float) -> float:
    """Distance from a to b on a circle of circumference 1.

    The tube closes, so a strand at u=0.99 is adjacent to one at u=0.01. Not
    wrapping leaves a visible seam down the back of every style.
    """
    d = abs(a - b) % 1.0
    return min(d, 1.0 - d)


def render(size, strands, root_band, tip_softness):
    """Rasterise the strands into an RGBA buffer.

    Per ROW rather than per pixel-per-strand: each strand touches a handful of
    columns, so walking its own span is O(strands * width) a row instead of
    O(strands * size). At 256x256 the difference is seconds against minutes.
    """
    px = bytearray(size * size * 4)
    # The scalp end is a mass, not separate hairs. Opaque here, fading into the
    # cut strands just after, or the gaps show skin at the hairline.
    for y in range(size):
        v = y / (size - 1)
        # Solid to `root_band`, then fading out over the same width again.
        root = max(0.0, min(1.0, 2.0 - v / max(1e-6, root_band)))

        for s in strands:
            if v > s.tip:
                continue
            # The tip fades over the last `tip_softness` of THIS strand's own
            # length, so the ends stagger instead of landing on one line.
            span = s.tip
            fade = 1.0
            if span > 1e-6:
                into_tip = (v - (span - tip_softness * span)) / max(1e-6, tip_softness * span)
                if into_tip > 0.0:
                    fade = max(0.0, 1.0 - into_tip)

            centre = (s.centre + s.drift * math.sin(v * 3.1 + s.phase)) % 1.0
            half = s.half_width
            # Only the columns this strand can touch.
            first = int(math.floor((centre - half) * (size - 1))) - 1
            last = int(math.ceil((centre + half) * (size - 1))) + 1
            for xi in range(first, last + 1):
                x = xi % size
                # `size`, not `size - 1`: u WRAPS, so a period of 255 over
                # 256 columns makes column 255 a duplicate of column 0 and any
                # strand crossing the seam one column fat. v does not wrap and
                # keeps the endpoint-inclusive form.
                u = x / size
                d = wrapped_delta(u, centre)
                if d > half:
                    continue
                # Soft shoulder: a one-texel cut aliases into a dashed line the
                # moment the hair is seen at a glancing angle.
                edge = 1.0 - (d / half) ** 3
                a = max(0.0, min(1.0, edge * fade))
                if a <= 0.0:
                    continue
                i = (y * size + x) * 4
                if a * 255.0 <= px[i + 3]:
                    continue
                lum = int(max(0.0, min(1.0, s.lum)) * 255.0)
                px[i] = lum
                px[i + 1] = lum
                px[i + 2] = lum
                px[i + 3] = int(a * 255.0)

        if root > 0.0:
            base = int(root * 255.0)
            for x in range(size):
                i = (y * size + x) * 4
                if px[i + 3] < base:
                    px[i + 3] = base
                    if px[i] == 0:
                        px[i] = px[i + 1] = px[i + 2] = 180
    return px


def png(width, height, rgba):
    """A minimal RGBA PNG. No Pillow: this must run in CI, which has no wheels."""
    raw = b"".join(b"\0" + bytes(rgba[y * width * 4:(y + 1) * width * 4]) for y in range(height))

    def chunk(tag, data):
        body = tag + data
        return struct.pack(">I", len(data)) + body + struct.pack(">I", zlib.crc32(body))

    return (b"\x89PNG\r\n\x1a\n"
            + chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 6, 0, 0, 0))
            + chunk(b"IDAT", zlib.compress(raw, 9))
            + chunk(b"IEND", b""))


# The three slots, and why their numbers differ.
#
# SCALP's root band is far wider than the others, and that is the POLE. Its
# unwrap is crown-radial, so every strand converges on one texel at v=0 and
# aliases into a noisy strip -- visible in a render as a ragged patch on the
# crown. A whorl is a dense mass rather than separate hairs anyway, so covering
# the singularity with solid alpha is both the fix and the anatomy.
#
# SCALP carries the most strands because it is seen as a mass at a distance.
# Its tips spread only 0.94..1.0, and that is deliberate rather than the 55%
# an earlier draft of this comment claimed: a shell's hem meets SKIN, it does
# not end in air, so fading strands short of it showed scalp through the
# hairline as a bright banded arc. The silhouette break-up a wider spread would
# buy has to come from geometry here, not from alpha.
#
# BROW IS NOT AN ALPHA COMB, and the first attempt at one is why. It used 34
# narrow strands, which is about six per facet on a ridge swept with SIX sides
# -- far more detail than the geometry can resolve. RENDERED, both brows came
# out as a speckled checkerboard: the alpha aliased against the facets instead
# of reading as hairs, and it was plainly worse than the solid ridge it
# replaced. A 2.2 mm brow is roughly ten pixels tall in a 1024 render, and
# individual hairs are simply not resolvable there.
#
# So the strands are FEWER THAN THE FACETS and wide enough to overlap, which
# makes the body of the brow solid. What the texture then buys is the one
# thing silhouette cannot give at this size: the TAIL THINS, because the tips
# are staggered and fade over half their length. That is the part of a brow a
# viewer actually reads.
#
# LASH, and it took re-authoring the mesh's UVs to earn this slot back. The
# lashes carried 250 of them inherited from the BODY atlas -- u 0.658..0.758,
# v 0.927..0.985 -- so the first attempt at a lash texture sampled a window 0%
# opaque with a mean alpha of 8 and made the lashes fainter and nothing else.
# `make_helper_proxies.strand_uvs()` now runs `u` across the lid and `v` along
# the hair, which is what makes this file's output mean anything there.
#
# WIDE AND OVERLAPPING, like the brow and for the same measured reason. The
# anatomy argues for sparseness -- lashes ARE separate hairs and the gaps are
# the thing -- but the render does not resolve them: a lash strip is a few
# pixels tall in a 1024 frame, and a 39% texture cut it into speckle that
# differed from a BARE FACE in 59 px where the solid strip managed 113. It read
# worse, exactly as 34 narrow strands on a six-sided brow ridge did.
#
# So the body stays solid and the alpha buys the one thing silhouette cannot:
# the lash line THINS toward its outer tips instead of stopping square.
SLOTS = (
    ("scalp", ROOT / "data" / "hair" / "hair_strands.png",
     dict(count=52, seed=0x5CA19, width_scale=0.70, min_tip=0.97, max_tip=1.0,
          drift_max=0.010, root_band=0.130, tip_softness=0.03)),
    ("brow", ROOT / "data" / "eyebrows" / "brow_strands.png",
     dict(count=5, seed=0xB4042, width_scale=2.30, min_tip=0.62, max_tip=1.0,
          drift_max=0.004, root_band=0.030, tip_softness=0.50)),
    ("lash", ROOT / "data" / "eyelashes" / "lash_strands.png",
     dict(count=6, seed=0x1A5E5, width_scale=2.10, min_tip=0.50, max_tip=1.0,
          drift_max=0.008, root_band=0.080, tip_softness=0.55)),
)


def write_slot(name, path, cfg):
    strands = build_strands(
        cfg["count"], cfg["seed"], cfg["width_scale"],
        cfg["min_tip"], cfg["max_tip"], cfg["drift_max"],
    )
    px = render(SIZE, strands, cfg["root_band"], cfg["tip_softness"])

    opaque = sum(1 for i in range(3, len(px), 4) if px[i] > 127)
    coverage = 100.0 * opaque / (SIZE * SIZE)
    # A texture that is almost all solid is a ribbon, and one that is almost
    # all gaps is invisible. Both have shipped here before; neither is caught
    # by the file existing, so check the number that actually describes it.
    if not 8.0 <= coverage <= 75.0:
        raise ValueError(
            f"{name}: {coverage:.1f}% opaque is outside the 8..75% a strand "
            f"texture should land in -- it will read as a solid ribbon or as nothing"
        )

    try:
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(png(SIZE, SIZE, px))
    except OSError as exc:
        raise SystemExit(f"cannot write {path}: {exc}") from exc

    tips = sorted(s.tip for s in strands)
    print(
        f"{name}: wrote {path.relative_to(ROOT)} ({SIZE}x{SIZE}, {coverage:.1f}% opaque, "
        f"{cfg['count']} strands, tips {tips[0]:.2f}..{tips[-1]:.2f})"
    )
    return coverage


def main() -> int:
    failures = 0
    for name, path, cfg in SLOTS:
        try:
            write_slot(name, path, cfg)
        except (ValueError, SystemExit) as exc:
            print(f"{name}: FAILED -- {exc}", file=sys.stderr)
            failures += 1
    if failures:
        print(f"{failures} of {len(SLOTS)} strand textures failed", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
