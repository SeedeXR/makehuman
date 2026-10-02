#!/usr/bin/env python3
"""The disk image's background, drawn from the project's own design tokens.

NOT an invented palette. `memory/design.md` sets `--bg-base` to #212124, the
accent to the #ffa02f -> #e96226 ramp the reference used, and `--text-primary`
to #ececee; the typeface is 42dot Sans, which ships in `resources/fonts`. A DMG
window is the first thing anyone sees of this application, and it should look
like the application.

Run it when the branding changes:

    ./.venv-mh/bin/python tools/make_dmg_background.py

The output is COMMITTED, because the `dmg` target has to work on a CI runner
with no Pillow and no Finder. See `tools/make_dmg_layout.sh` for the other half,
the icon positions, which are committed for the same reason.
"""
from pathlib import Path

from PIL import Image, ImageDraw, ImageFont

ROOT = Path(__file__).resolve().parent.parent
OUT = ROOT / "packaging" / "dmg-background.png"

# design.md's table, not taste.
BG = (0x21, 0x21, 0x24)
TEXT = (0xEC, 0xEC, 0xEE)
ACCENT_HI = (0xFF, 0xA0, 0x2F)
ACCENT_LO = (0xE9, 0x62, 0x26)

# The window is 640x400 points and the two icons sit at x=160 and x=480, which
# is what `make_dmg_layout.sh` writes into the .DS_Store. Both halves read these
# numbers from the same comment and they have to agree; if one moves, move both.
W, H = 640, 400
ICON_Y = 205
LEFT_X, RIGHT_X = 160, 480

# Deliberately 1x. Finder wants the background's pixel size to match the window
# in POINTS unless it is handed a multi-resolution TIFF, and building one needs
# `tiffutil`, a second asset and a second thing to keep in step. The cost is
# that the background is soft on a Retina display; the icons and their labels,
# which is what anyone actually reads, are drawn by Finder and stay sharp.


def font(size: int):
    path = ROOT / "resources" / "fonts" / "42dotSans-VariableFont_wght.ttf"
    try:
        return ImageFont.truetype(str(path), size)
    except OSError:
        # A missing font must not silently change the layout, but it also must
        # not stop a contributor regenerating the image.
        print(f"warning: {path} unreadable, falling back to the default face")
        return ImageFont.load_default()


def main() -> int:
    img = Image.new("RGB", (W, H), BG)
    d = ImageDraw.Draw(img)

    title = font(30)
    sub = font(14)
    d.text((W // 2, 58), "MakeHuman", font=title, fill=TEXT, anchor="mm")
    d.text((W // 2, 92), "Drag the app onto Applications to install",
           font=sub, fill=(0x9A, 0x9A, 0xA2), anchor="mm")

    # The arrow, as the accent ramp left-to-right. Drawn as a run of 1px
    # columns because PIL has no gradient primitive and a 220px bar does not
    # justify pulling one in.
    x0, x1 = LEFT_X + 84, RIGHT_X - 84
    y = ICON_Y
    for x in range(x0, x1):
        t = (x - x0) / max(1, x1 - x0 - 1)
        c = tuple(round(ACCENT_HI[i] + (ACCENT_LO[i] - ACCENT_HI[i]) * t) for i in range(3))
        d.line([(x, y - 1), (x, y + 1)], fill=c)
    for k in range(14):                       # head
        d.line([(x1 - k, y - k), (x1 - k, y + k)], fill=ACCENT_LO)

    OUT.parent.mkdir(parents=True, exist_ok=True)
    img.save(OUT)
    print(f"wrote {OUT.relative_to(ROOT)} ({W}x{H})")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
