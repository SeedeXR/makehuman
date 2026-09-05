#!/usr/bin/env python3
"""Generate the shipped skin materials procedurally.

**Why procedural.** The owner's four reference sources split on licence:
`texturing.xyz` VFace is paid and forbids redistribution, so it can never enter
this AGPL repo; `3dtextures.me` and `texturecan.com` are CC0 but are photographic
tiling swatches, not the MakeHuman body UV ATLAS, so they cannot be dropped onto
this mesh without a reprojection step that does not exist yet. Generating our own
sidesteps both problems and leaves the result unambiguously ours (CC0, like the
rest of `data/`).

**What is copied from real skin is the STRUCTURE, not any pixel:**
  * tone varies mostly in luminance and a little in hue -- melanin darkens and
    warms, it does not change the surface;
  * so the microstructure (pores, fine wrinkles, specular break-up) is shared by
    every tone and only the albedo changes. That is one normal map and one
    roughness map for all of them, which is both physically right and what keeps
    this from costing tens of megabytes;
  * skin is matte-to-satin, roughness around 0.5-0.7, never mirror-like;
  * real skin is blotchy at low frequency (subdermal variation) and grainy at
    high frequency (pores). Uniform colour reads as plastic.

**Tones.** Eight, spanning the range rather than the usual three. Four of them
are African, because "African skin" is not one colour -- it runs from deep
Nilotic browns to light West African and North African tones, and collapsing
that to a single swatch is the exact failure this set exists to avoid.

Run:  ./.venv-mh/bin/python tools/make_skins.py
"""
import sys
from pathlib import Path

import numpy as np
from PIL import Image

REPO = Path(__file__).resolve().parent.parent
SKINS = REPO / "data" / "skins"
TEX = REPO / "data" / "textures" / "skin"

SIZE = 1024
RNG = np.random.default_rng(20260905)  # fixed: the assets must regenerate identically

# (file stem, display name, base sRGB). Ordered light -> deep so a chooser reads
# as a spectrum rather than a bag.
# `name` in a .mhmat is ONE TOKEN: the reference reads `words[1]` and stops
# (shared/material.py:368), and our parser matches it (Material.cpp:213-214).
# "African (rich)" therefore arrives everywhere as "African", and all four
# African tones collided on that one name in every export. Measured, not
# guessed -- the first generated set did exactly that.
TONES = [
    ("fair_rose", "FairRose", (0.906, 0.773, 0.706)),
    ("fair_olive", "FairOlive", (0.855, 0.729, 0.612)),
    ("medium_olive", "MediumOlive", (0.761, 0.612, 0.478)),
    ("medium_amber", "MediumAmber", (0.678, 0.510, 0.376)),
    ("african_light", "AfricanLight", (0.596, 0.427, 0.302)),
    ("african_warm", "AfricanWarm", (0.478, 0.325, 0.220)),
    ("african_deep", "AfricanDeep", (0.353, 0.231, 0.153)),
    ("african_rich", "AfricanRich", (0.243, 0.153, 0.106)),
]


def fbm(shape, octaves=6, persistence=0.5):
    """Fractal noise by summing upsampled white noise, cheapest thing that gives
    both the low-frequency blotching and the high-frequency grain skin needs."""
    out = np.zeros(shape, dtype=np.float32)
    amplitude = 1.0
    total = 0.0
    for o in range(octaves):
        res = max(2, shape[0] >> (octaves - o))
        small = RNG.random((res, res)).astype(np.float32)
        big = np.array(Image.fromarray((small * 255).astype(np.uint8)).resize(shape, Image.BICUBIC),
                       dtype=np.float32) / 255.0
        out += big * amplitude
        total += amplitude
        amplitude *= persistence
    return out / total


def albedo(base):
    """Base tone modulated by subdermal blotching and pore grain.

    Multiplicative, not additive: melanin scales reflectance, so a dark tone must
    vary by less in absolute terms than a light one. Adding a fixed noise range
    would wash the deep tones grey.
    """
    blotch = fbm((SIZE, SIZE), octaves=4, persistence=0.6)
    grain = fbm((SIZE, SIZE), octaves=7, persistence=0.45)
    # 0.88..1.12 of the base, most of it from the low frequencies.
    modulation = 1.0 + (blotch - 0.5) * 0.18 + (grain - 0.5) * 0.06

    img = np.empty((SIZE, SIZE, 3), dtype=np.float32)
    for c in range(3):
        img[..., c] = base[c] * modulation
    # A touch more red variation than blue: subdermal blood is what the blotching
    # physically IS, so it must not be a grey multiplier.
    img[..., 0] *= 1.0 + (blotch - 0.5) * 0.05
    img[..., 2] *= 1.0 - (blotch - 0.5) * 0.04
    return Image.fromarray((np.clip(img, 0.0, 1.0) * 255).astype(np.uint8), "RGB")


def detail_normal():
    """Pore-scale normals from the gradient of a high-frequency height field."""
    height = fbm((SIZE, SIZE), octaves=8, persistence=0.4)
    dy, dx = np.gradient(height.astype(np.float32))
    strength = 12.0
    nx, ny, nz = -dx * strength, -dy * strength, np.ones_like(height)
    norm = np.sqrt(nx * nx + ny * ny + nz * nz)
    out = np.stack([nx / norm, ny / norm, nz / norm], axis=-1)
    return Image.fromarray((((out + 1.0) * 0.5) * 255).astype(np.uint8), "RGB")


def roughness():
    """Skin is matte-to-satin. 0.52..0.72, breaking up the specular so it does
    not read as wet plastic; the shared map is why tone costs one texture."""
    r = 0.62 + (fbm((SIZE, SIZE), octaves=6, persistence=0.5) - 0.5) * 0.20
    return Image.fromarray((np.clip(r, 0.0, 1.0) * 255).astype(np.uint8), "L")


MHMAT = """# Generated by tools/make_skins.py -- do not hand-edit.
#
# Released CC0, like the rest of data/. Generated from noise, not derived from
# any photographic source; see the tool for what was and was not borrowed.

name {name}
tag skin
tag procedural

// Blinn-Phong terms. shininess 0.42 converts to roughness 0.58 for the glTF and
// USD writers (foundation::metallicRoughnessOf) and for the PBR viewport; the
// roughness MAP refines it per texel.
//
// 0.58, not the 0.35 this shipped with first. Nothing in the exported file
// looked wrong at 0.35 -- it took rendering the same material through a real
// microfacet BRDF to see it, and every tone came out reading as oiled skin
// rather than skin. Measured photographic skin sits around 0.5-0.7 for a
// single-lobe GGX; the wet look below 0.4 is what a specular lobe does when it
// is the only lobe and there is no subsurface term under it.
ambientColor 0.11 0.11 0.11
diffuseColor 1.0 1.0 1.0
specularColor 0.28 0.28 0.28
shininess 0.42
emissiveColor 0.0 0.0 0.0
opacity 1.0
translucency 0.0

diffuseTexture {albedo}
normalmapTexture ../textures/skin/skin_normal.png

// autoBlendSkin OFF: that blends a litsphere by ethnicity, which is what these
// materials replace. Leaving it on would tint an explicitly chosen tone by the
// macro sliders and make the choice look ignored.
autoBlendSkin false

shaderConfig diffuse true
shaderConfig bump false
shaderConfig normal true
shaderConfig spec true
shaderConfig vertexColors false
shaderConfig displacement false
"""


def main() -> int:
    SKINS.mkdir(parents=True, exist_ok=True)
    TEX.mkdir(parents=True, exist_ok=True)

    detail_normal().save(TEX / "skin_normal.png", optimize=True)
    roughness().save(TEX / "skin_roughness.png", optimize=True)
    print(f"wrote {TEX.relative_to(REPO)}/skin_normal.png and skin_roughness.png")

    for stem, name, base in TONES:
        albedo(base).save(TEX / f"{stem}.png", optimize=True)
        (SKINS / f"{stem}.mhmat").write_text(
            MHMAT.format(name=name, albedo=f"../textures/skin/{stem}.png"))
        print(f"wrote {stem}.mhmat + {stem}.png")

    total = sum(p.stat().st_size for p in TEX.glob("*.png"))
    print(f"{len(TONES)} tones, textures total {total / 1e6:.1f} MB")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
