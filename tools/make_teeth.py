#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-or-later
"""Generates the teeth proxy from the base mesh's own helper geometry.

Directive 13.5: "if there are no assets or data from legacy, procedurally
generate and wire through." Legacy ships NOTHING for this slot -- measured, zero
`.mhclo` under every one of the seven proxy directories -- because upstream
distributes them as separate downloadable asset packs.

But the SHAPE is not missing. `data/3dobjs/base.obj` carries `helper-upper-teeth`
and `helper-lower-teeth` groups: cage geometry that exists precisely so a proxy
can be fitted to it, hidden from the render by the static face mask. So nothing
here is invented. The proxy IS the helper, extracted and renumbered.

That also settles the rigging for free, and it was checked rather than assumed:

    helper-upper-teeth   68 verts, every one dominated by `head`
    helper-lower-teeth   68 verts, every one dominated by `jaw`

so the lower teeth follow a jaw drop and the upper do not, because the base
mesh's own weights say so. `tests/CMakeLists.txt` pins exactly that with
`--facs AU26=1.0`.

The fitting is the IDENTITY: each proxy vertex names its own base vertex three
times at weight (1, 0, 0) with a zero offset, so the proxy sits exactly on the
helper for every body shape and every pose, with no interpolation error to
accumulate. A `.mhclo` is a weighted blend of three base vertices; one of them
being the answer is the degenerate and exact case.
"""
import argparse
import io
import sys
from pathlib import Path

import numpy as np
from PIL import Image

ROOT = Path(__file__).resolve().parent.parent
BASE = ROOT / "data" / "3dobjs" / "base.obj"
OUT = ROOT / "data" / "teeth"

WANTED = ("helper-upper-teeth", "helper-lower-teeth")


def read_base():
    """Vertices, texture coords, and the faces of each wanted group."""
    verts, texcoords = [], []
    faces = []  # (vertex indices, texcoord indices) for the wanted groups
    group = None
    for line in BASE.read_text().splitlines():
        if line.startswith("v "):
            verts.append(tuple(float(x) for x in line.split()[1:4]))
        elif line.startswith("vt "):
            texcoords.append(tuple(float(x) for x in line.split()[1:3]))
        elif line.startswith("g "):
            group = line.split(None, 1)[1].strip()
        elif line.startswith("f ") and group in WANTED:
            vi, ti = [], []
            for tok in line.split()[1:]:
                parts = tok.split("/")
                vi.append(int(parts[0]) - 1)
                ti.append(int(parts[1]) - 1 if len(parts) > 1 and parts[1] else -1)
            faces.append((vi, ti))
    return verts, texcoords, faces


# Enamel, as a matcap. Off-white with the faintest warm cast; the highlight is
# the source matcap's, not an invented one.
TINT = (0.97, 0.96, 0.93)
SOURCE_LITSPHERE = "skinmat_caucasian.png"


def make_litsphere() -> bytes:
    """The enamel matcap, as PNG bytes.

    Teeth were first wired to the body's own skin matcap, because the teeth
    material carries no diffuse texture and `litsphere.frag:157-162` multiplies
    the matcap by the diffuse texture alone -- so with a white placeholder
    standing in for the texture, the matcap IS the colour. Rendered and looked
    at, the result was teeth at RGB (233, 155, 123): orange, indistinguishable
    from lip. The other shipped matcap, `skinmat_eye.png`, is worse -- measured
    (45, 25, 13) at its centre, which renders teeth nearly black.

    So this is generated rather than chosen, and generated FROM the skin matcap
    rather than from scratch: taking its luminance and re-tinting keeps the
    light coming from exactly the same direction as on the face around it. An
    invented sphere would have lit the teeth from its own private sun.
    """
    src = ROOT / "data" / "litspheres" / SOURCE_LITSPHERE
    rgb = np.asarray(Image.open(src).convert("RGB")).astype(np.float64)
    lum = rgb @ np.array([0.2126, 0.7152, 0.0722])

    # Normalised against the BRIGHTEST point rather than 255: the skin matcap
    # never reaches white, and dividing by 255 would leave the teeth grey.
    lum /= max(lum.max(), 1.0)
    # Enamel is lighter and lower-contrast than skin. The exponent lifts the
    # midtones without clipping the highlight, which a linear gain would.
    shade = lum ** 0.55

    out = np.clip(shade[..., None] * np.array(TINT) * 255.0, 0.0, 255.0)
    # Outside the sphere is never sampled -- `normal * 0.495 + 0.5` cannot leave
    # the inscribed circle -- but the shipped matcaps fill it with a flat
    # neutral and this one matches, so a human opening the file sees the same
    # thing.
    corner = rgb[0, 0]
    yy, xx = np.ogrid[: lum.shape[0], : lum.shape[1]]
    cy, cx = (lum.shape[0] - 1) / 2.0, (lum.shape[1] - 1) / 2.0
    mask = ((yy - cy) ** 2 + (xx - cx) ** 2) > (min(cy, cx) ** 2)
    out[mask] = corner

    buf = io.BytesIO()
    Image.fromarray(out.astype(np.uint8)).save(buf, format="PNG")
    return buf.getvalue()


def build() -> tuple[dict[str, bytes], str]:
    """Every generated file as `relative path -> bytes`, plus a one-line report.

    Built in memory rather than written straight out so `--check` can compare
    the committed assets against a fresh derivation byte for byte. `--check` is
    the point of this split: `data/3dobjs/base.obj` could be retouched, or this
    generator edited, and the committed proxy would go on describing the old
    helper cage in silence.
    """
    verts, texcoords, faces = read_base()
    if not faces:
        raise SystemExit(f"no {WANTED} geometry in {BASE}")

    # Renumber: the proxy is its own mesh, and the .mhclo maps each of its
    # vertices back to the base index it came from. Sorted so the output is
    # deterministic -- a generated asset that reshuffles between runs is a diff
    # nobody can review.
    used = sorted({v for vi, _ in faces for v in vi})
    local = {base: i for i, base in enumerate(used)}
    used_t = sorted({t for _, ti in faces for t in ti if t >= 0})
    local_t = {base: i for i, base in enumerate(used_t)}

    obj = ["# Generated by tools/make_teeth.py from the base mesh's helper-*-teeth",
           "# groups. Do not edit by hand; re-run the generator.",
           "mtllib teeth.mtl", "o teeth"]
    for b in used:
        obj.append("v {:.6f} {:.6f} {:.6f}".format(*verts[b]))
    for t in used_t:
        obj.append("vt {:.6f} {:.6f}".format(*texcoords[t]))
    obj.append("usemtl teeth")
    for vi, ti in faces:
        toks = []
        for v, t in zip(vi, ti):
            toks.append(f"{local[v] + 1}/{local_t[t] + 1}" if t >= 0 else f"{local[v] + 1}")
        obj.append("f " + " ".join(toks))

    mhclo = [
        "# Generated by tools/make_teeth.py -- see that file for why this is the",
        "# base mesh's own helper geometry rather than invented shape.",
        "#",
        "# The fitting is the IDENTITY: each vertex names its own base vertex at",
        "# weight (1, 0, 0) with no offset, so the proxy tracks the helper exactly",
        "# for every body shape and pose. The upper teeth sit on head-weighted",
        "# vertices and the lower on jaw-weighted ones, so a jaw drop moves the",
        "# lower set alone -- the base mesh's weights decide that, not this file.",
        "",
        "uuid 7e3a6d94-2b51-4c8f-9a17-5d0c8e2f4b63",
        "basemesh hm08",
        "",
        "name Teeth",
        "tag MakeHuman™",
        "",
        "obj_file teeth.obj",
        "material materials/teeth.mhmat",
        "z_depth 40",
        "",
        "verts 0",
    ]
    for b in used:
        mhclo.append(f"{b} {b} {b} 1.00000 0.00000 0.00000 0.00000 0.00000 0.00000")

    mat = (
        "# Generated by tools/make_teeth.py.\n"
        "#\n"
        "# No texture: enamel is close to a flat off-white at this scale, and a\n"
        "# generated texture would be a flat fill in a PNG. `autoBlendSkin` is\n"
        "# absent on purpose -- that blends a litsphere by ETHNICITY, which teeth\n"
        "# are not.\n"
        "name Teeth\n"
        "tag MakeHuman™\n"
        "ambientColor 0.12 0.12 0.12\n"
        "diffuseColor 0.96 0.94 0.89\n"
        "specularColor 1.0 1.0 1.0\n"
        "shininess 0.6\n"
        "opacity 1.0\n"
        "transparent False\n"
        "backfaceCull True\n"
        "castShadows True\n"
        "receiveShadows True\n"
        "shader data/shaders/glsl/litsphere\n"
    )

    return (
        {
            "teeth.obj": ("\n".join(obj) + "\n").encode(),
            "teeth.mhclo": ("\n".join(mhclo) + "\n").encode(),
            "materials/teeth.mhmat": mat.encode(),
            # Beside the teeth, NOT in data/litspheres: that directory is what
            # the body-skin chooser enumerates, and a matcap dropped there
            # becomes a body skin the user can pick. The eye matcap only escapes
            # that by a substring test on "eye" (`main.cpp:888`), the kind of
            # rule that quietly swallows the next asset whose name matches.
            "skinmat_teeth.png": make_litsphere(),
        },
        f"teeth: {len(used)} vertices, {len(faces)} faces "
        f"(upper+lower helpers), {len(used_t)} uvs",
    )


def is_stale(path: Path, fresh: bytes) -> bool:
    """Whether the committed @p path disagrees with a freshly derived @p fresh.

    The text files are compared byte for byte, but the PNG is compared by its
    decoded PIXELS. Comparing PNG bytes was tried first and failed in CI while
    passing locally. Reproduced in a linux/amd64 container with the same Pillow
    (12.3.0) the runner installs, the difference was measured exactly: the
    committed file is 27,922 bytes and the freshly derived one 28,660, and
    `np.array_equal` on the decoded arrays is True. Same pixels, different
    deflate stream -- the platform's zlib, not the library version and not the
    float maths (both were checked and neither differs).

    Pixels are what the renderer reads, so pixels are what this asks about. The
    cost is that the gate no longer notices a re-encode that preserves every
    pixel, which is exactly the change it should not care about.
    """
    if not path.exists():
        return True
    if path.suffix != ".png":
        return path.read_bytes() != fresh
    committed = np.asarray(Image.open(path).convert("RGB"))
    regenerated = np.asarray(Image.open(io.BytesIO(fresh)).convert("RGB"))
    return not np.array_equal(committed, regenerated)


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--check", action="store_true",
                    help="verify the committed assets match this generator; write nothing")
    args = ap.parse_args()

    files, report = build()

    if args.check:
        stale = [name for name, data in files.items() if is_stale(OUT / name, data)]
        if stale:
            print("stale generated teeth assets: " + ", ".join(sorted(stale)), file=sys.stderr)
            print("Re-run without --check to regenerate.", file=sys.stderr)
            return 1
        print(f"{report} -- committed assets match the generator")
        return 0

    for name, data in files.items():
        dst = OUT / name
        dst.parent.mkdir(parents=True, exist_ok=True)
        dst.write_bytes(data)
    print(report)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
