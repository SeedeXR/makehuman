#!/usr/bin/env python3
"""Build macOS AppIcon.icns from the brand logo, in Apple's icon geometry.

A macOS app icon is NOT a full-bleed square. Since Big Sur every system and
first-party icon is a rounded rectangle ("squircle") that occupies **824 of a
1024pt canvas**, centred, leaving a 100pt transparent margin on every side, with
a corner radius of 185.4pt. Shipping the raw square makes the icon visibly
larger than every neighbour in the Dock and square where they are round -- which
is exactly the "doesn't look like other macOS apps" complaint.

Sizes come from Apple's iconset spec: 16/32/128/256/512 at 1x and 2x.

Run:  ./.venv-mh/bin/python tools/make_appicon.py
"""
import shutil
import subprocess
import sys
from pathlib import Path

from PIL import Image, ImageDraw

REPO = Path(__file__).resolve().parent.parent
SOURCE = REPO / "resources" / "branding" / "makehuman-logo.png"
ICONSET = REPO / "resources" / "branding" / "AppIcon.iconset"
ICNS = REPO / "resources" / "branding" / "AppIcon.icns"

# Apple's proportions, expressed against the 1024pt canvas.
CANVAS = 1024
SHAPE = 824
RADIUS = 185.4
# Supersample the mask, then downsample: PIL's rounded_rectangle is aliased at
# 1x and the corners come out visibly stepped at 512pt.
SS = 4


def squircle_mask(size: int) -> Image.Image:
    big = Image.new("L", (size * SS, size * SS), 0)
    ImageDraw.Draw(big).rounded_rectangle(
        (0, 0, size * SS - 1, size * SS - 1), radius=RADIUS * SS * size / SHAPE, fill=255
    )
    return big.resize((size, size), Image.LANCZOS)


def render(canvas: int) -> Image.Image:
    """The logo, masked to the squircle and inset in a transparent canvas."""
    shape = round(canvas * SHAPE / CANVAS)
    margin = (canvas - shape) // 2

    art = Image.open(SOURCE).convert("RGBA").resize((shape, shape), Image.LANCZOS)
    art.putalpha(squircle_mask(shape))

    out = Image.new("RGBA", (canvas, canvas), (0, 0, 0, 0))
    out.paste(art, (margin, margin), art)
    return out


def main() -> int:
    if not SOURCE.exists():
        print(f"missing {SOURCE}", file=sys.stderr)
        return 1
    if shutil.which("iconutil") is None:
        print("iconutil not found; this is macOS-only tooling", file=sys.stderr)
        return 1

    if ICONSET.exists():
        shutil.rmtree(ICONSET)
    ICONSET.mkdir(parents=True)

    for base in (16, 32, 128, 256, 512):
        render(base).save(ICONSET / f"icon_{base}x{base}.png")
        render(base * 2).save(ICONSET / f"icon_{base}x{base}@2x.png")

    subprocess.run(["iconutil", "-c", "icns", str(ICONSET), "-o", str(ICNS)], check=True)
    print(f"wrote {ICNS.relative_to(REPO)} ({ICNS.stat().st_size} bytes)")

    # A 1024 PNG for anything that wants a plain image: the Qt window icon, the
    # DMG background, the README.
    render(1024).save(REPO / "resources" / "branding" / "AppIcon-1024.png")
    print("wrote resources/branding/AppIcon-1024.png")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
