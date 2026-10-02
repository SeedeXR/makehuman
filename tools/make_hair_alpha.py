#!/usr/bin/env python3
"""The alpha-cut strand texture the hair material has been asking for.

`data/hair/materials/hair.mhmat` said it outright -- "real hair needs an
alpha-cut strand texture, which the cage geometry cannot stand in for" -- and
until now there was nowhere to put one: the four generated styles carried NO
UVs at all. MEASURED on an export: body 14,517 distinct UVs, eyes 808, teeth
136, hair ZERO. The sweeps emit UVs now, so this is the other half.

WHAT THE MAPPING MEANS, because the texture is useless without it. Every style
is a swept tube and `u` runs AROUND the tube while `v` runs ALONG it. So a
pattern that varies in u and holds in v cuts the tube lengthwise into ribbons
that run the way the hair grows. Varying it the other way would band the hair
into rungs like a caterpillar.

The precedent for the alpha itself is the eye: `hazel_eye.png` carries an
alpha-0 cornea disc and its material sets `transparent True`, "without the flag
the renderer discards that alpha and paints an opaque disc over the iris".

    ./.venv-mh/bin/python tools/make_hair_alpha.py
"""
import math
import struct
import zlib
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
OUT = ROOT / "data" / "hair" / "hair_strands.png"

SIZE = 256
# How many strands the tube is cut into around its circumference. Eight against
# the six-to-eight facets the sweeps use, so a strand is about one facet wide
# and the cut does not fall consistently on an edge.
STRANDS = 8
# How much of each strand's width is solid. Below about 0.5 the hair reads as
# gaps with strands in them rather than hair with gaps.
DUTY = 0.80


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


def main() -> int:
    px = bytearray(SIZE * SIZE * 4)
    for y in range(SIZE):
        v = y / (SIZE - 1)
        for x in range(SIZE):
            u = x / (SIZE - 1)
            # Where we sit inside this strand, 0..1 across its width.
            phase = (u * STRANDS) % 1.0
            # A soft edge rather than a hard cut: a one-texel step aliases into
            # a dashed line the moment the hair is seen at an angle.
            edge = min(phase, 1.0 - phase) / max(1e-6, (1.0 - DUTY) * 0.5)
            alpha = max(0.0, min(1.0, edge))

            # Thinning toward the tip. v = 1 is the far end of every sweep --
            # `ridge`, `loc_tube` and `knot_mesh` all run v along the path -- and
            # hair that ends in a flat wall is the "braid ends are cut square"
            # limitation this project already recorded.
            alpha *= max(0.0, min(1.0, (1.0 - v) * 3.0))

            # A little variation along the strand so it does not read as
            # extruded plastic. Deterministic: this file is committed and a
            # random one would churn the repo on every run.
            shade = 0.82 + 0.18 * math.sin(u * STRANDS * 2.0 * math.pi) * math.cos(v * 9.0)
            lum = int(max(0.0, min(1.0, shade)) * 255.0)

            i = (y * SIZE + x) * 4
            px[i] = lum
            px[i + 1] = lum
            px[i + 2] = lum
            px[i + 3] = int(alpha * 255.0)

    OUT.parent.mkdir(parents=True, exist_ok=True)
    OUT.write_bytes(png(SIZE, SIZE, px))
    solid = sum(1 for i in range(3, len(px), 4) if px[i] > 127)
    print(f"wrote {OUT.relative_to(ROOT)} ({SIZE}x{SIZE}, "
          f"{100.0 * solid / (SIZE * SIZE):.1f}% opaque)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
