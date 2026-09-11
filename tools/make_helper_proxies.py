#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-or-later
"""Generates the proxies that the base mesh already carries helper cages for.

Directive 13.5: "if there are no assets or data from legacy, procedurally
generate and wire through." Legacy ships NOTHING for the seven proxy slots --
measured, zero `.mhclo` under every one of them -- because upstream distributes
them as separate downloadable asset packs.

But the SHAPE is not missing. `data/3dobjs/base.obj` carries `helper-*` groups:
cage geometry that exists precisely so a proxy can be fitted to it, hidden from
the render by the static face mask. So nothing here is invented. Each proxy IS
the helper, extracted and renumbered.

That also settles the rigging for free, and it was checked rather than assumed
(dominant bone per vertex, from `data/rigs/default_weights.mhw`):

    helper-upper-teeth    68 verts, every one dominated by `head`
    helper-lower-teeth    68 verts, every one dominated by `jaw`
    helper-tongue        226 verts, every one dominated by a `tongue*` bone,
                         and the whole tongue chain hangs off `jaw`

so a jaw drop moves the lower teeth and the whole tongue, and leaves the upper
teeth exactly where they were. `tests/CMakeLists.txt` pins both with
`--facs AU26=1.0`.

The fitting is the IDENTITY: each proxy vertex names its own base vertex three
times at weight (1, 0, 0) with a zero offset, so the proxy sits exactly on the
helper for every body shape and every pose, with no interpolation error to
accumulate. A `.mhclo` is a weighted blend of three base vertices; one of them
being the answer is the degenerate and exact case.

This file replaced `tools/make_teeth.py` when the tongue became the second slot:
one more copy of the same 260 lines was the wrong way to add a slot. A slot is
DATA here (`SLOTS` below), so the remaining helper-cage slots cost an entry
rather than a script.
"""
import argparse
import io
import sys
from pathlib import Path

import numpy as np
from PIL import Image

ROOT = Path(__file__).resolve().parent.parent
BASE = ROOT / "data" / "3dobjs" / "base.obj"

# The matcap every slot's own is derived FROM. Re-tinting its luminance keeps
# the light coming from the same direction as on the face around it; an invented
# sphere would light the proxy from its own private sun.
SOURCE_LITSPHERE = "skinmat_caucasian.png"


class Slot:
    """One helper-cage proxy: which groups it is cut from, and how it looks."""

    def __init__(self, key, name, uuid, groups, z_depth, tint, material, rationale):
        self.key = key                # directory under data/, and the .mhm slot
        self.name = name              # what the picker and the .mhm line show
        self.uuid = uuid
        self.groups = groups          # base.obj groups to extract
        self.z_depth = z_depth
        self.tint = tint              # matcap tint, linear multipliers on luminance
        self.material = material      # the .mhmat body, minus the generated header
        self.rationale = rationale    # why this shape, in the .mhclo header

    @property
    def out(self):
        return ROOT / "data" / self.key


SLOTS = [
    Slot(
        key="teeth",
        name="Teeth",
        uuid="7e3a6d94-2b51-4c8f-9a17-5d0c8e2f4b63",
        groups=("helper-upper-teeth", "helper-lower-teeth"),
        z_depth=40,
        # Enamel: off-white with the faintest warm cast.
        tint=(0.97, 0.96, 0.93),
        material=(
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
        ),
        rationale=(
            "# The upper teeth sit on head-weighted vertices and the lower on\n"
            "# jaw-weighted ones, so a jaw drop moves the lower set alone -- the\n"
            "# base mesh's weights decide that, not this file."
        ),
    ),
    Slot(
        key="tongue",
        name="Tongue",
        uuid="b5f1c40a-9d27-4e63-8a1b-2c7e06f9d834",
        groups=("helper-tongue",),
        # Behind the teeth, so it loses the depth fight against them rather than
        # poking through an incisor.
        z_depth=30,
        # Wet muscle, not enamel: keeps the skin matcap's warmth and pulls the
        # green and blue down so it reads pink rather than flesh.
        tint=(0.93, 0.50, 0.52),
        material=(
            "# No texture, for the same reason as the teeth: at this scale the\n"
            "# tongue is a flat colour, and a generated texture would be a fill.\n"
            "# Glossier than enamel -- a tongue is wet.\n"
            "name Tongue\n"
            "tag MakeHuman™\n"
            "ambientColor 0.10 0.06 0.06\n"
            "diffuseColor 0.80 0.42 0.44\n"
            "specularColor 1.0 1.0 1.0\n"
            "shininess 0.8\n"
            "opacity 1.0\n"
            "transparent False\n"
            "backfaceCull True\n"
            "castShadows True\n"
            "receiveShadows True\n"
            "shader data/shaders/glsl/litsphere\n"
        ),
        rationale=(
            "# Every tongue vertex is dominated by a `tongue*` bone, and the whole\n"
            "# tongue chain hangs off `jaw`, so a jaw drop carries ALL of it -- the\n"
            "# base mesh's weights decide that, not this file."
        ),
    ),
]


def read_base(wanted):
    """Vertices, texture coords, and the faces of each wanted group."""
    verts, texcoords = [], []
    faces = []
    group = None
    for line in BASE.read_text().splitlines():
        if line.startswith("v "):
            verts.append(tuple(float(x) for x in line.split()[1:4]))
        elif line.startswith("vt "):
            texcoords.append(tuple(float(x) for x in line.split()[1:3]))
        elif line.startswith("g "):
            group = line.split(None, 1)[1].strip()
        elif line.startswith("f ") and group in wanted:
            vi, ti = [], []
            for tok in line.split()[1:]:
                parts = tok.split("/")
                vi.append(int(parts[0]) - 1)
                ti.append(int(parts[1]) - 1 if len(parts) > 1 and parts[1] else -1)
            faces.append((vi, ti))
    return verts, texcoords, faces


def make_litsphere(tint) -> bytes:
    """A matcap for one slot, as PNG bytes, re-tinted from the skin matcap.

    The slot materials carry no diffuse texture, and `litsphere.frag:157-162`
    multiplies the matcap by the diffuse texture alone -- so with the white
    placeholder standing in, the matcap IS the colour. That was found by
    rendering: wired to the body's own skin litsphere the teeth came out at RGB
    (233, 155, 123), orange, indistinguishable from the lip in front of them.
    The other shipped matcap, `skinmat_eye.png`, is worse -- measured
    (45, 25, 13) at its centre, which renders nearly black.
    """
    src = ROOT / "data" / "litspheres" / SOURCE_LITSPHERE
    rgb = np.asarray(Image.open(src).convert("RGB")).astype(np.float64)
    lum = rgb @ np.array([0.2126, 0.7152, 0.0722])

    # Normalised against the BRIGHTEST point rather than 255: the skin matcap
    # never reaches white, and dividing by 255 would leave the result grey.
    lum /= max(lum.max(), 1.0)
    # The exponent lifts the midtones without clipping the highlight, which a
    # linear gain would.
    shade = lum ** 0.55

    out = np.clip(shade[..., None] * np.array(tint) * 255.0, 0.0, 255.0)
    # Outside the sphere is never sampled -- `normal * 0.495 + 0.5` cannot leave
    # the inscribed circle -- but the shipped matcaps fill it with a flat
    # neutral and these match, so a human opening the file sees the same thing.
    corner = rgb[0, 0]
    yy, xx = np.ogrid[: lum.shape[0], : lum.shape[1]]
    cy, cx = (lum.shape[0] - 1) / 2.0, (lum.shape[1] - 1) / 2.0
    out[((yy - cy) ** 2 + (xx - cx) ** 2) > (min(cy, cx) ** 2)] = corner

    buf = io.BytesIO()
    Image.fromarray(out.astype(np.uint8)).save(buf, format="PNG")
    return buf.getvalue()


def build(slot) -> tuple[dict[str, bytes], str]:
    """Every generated file for @p slot as `relative path -> bytes`, plus a report.

    Built in memory rather than written straight out so `--check` can compare the
    committed assets against a fresh derivation. `--check` is the point of that
    split: `data/3dobjs/base.obj` could be retouched, or this generator edited,
    and the committed proxy would go on describing the old helper cage in
    silence.
    """
    verts, texcoords, faces = read_base(slot.groups)
    if not faces:
        raise SystemExit(f"no {slot.groups} geometry in {BASE}")

    # Renumber: the proxy is its own mesh, and the .mhclo maps each of its
    # vertices back to the base index it came from. Sorted so the output is
    # deterministic -- a generated asset that reshuffles between runs is a diff
    # nobody can review.
    used = sorted({v for vi, _ in faces for v in vi})
    local = {base: i for i, base in enumerate(used)}
    used_t = sorted({t for _, ti in faces for t in ti if t >= 0})
    local_t = {base: i for i, base in enumerate(used_t)}

    obj = ["# Generated by tools/make_helper_proxies.py from the base mesh's",
           f"# {', '.join(slot.groups)} geometry. Do not edit by hand; re-run it.",
           f"mtllib {slot.key}.mtl", f"o {slot.key}"]
    for b in used:
        obj.append("v {:.6f} {:.6f} {:.6f}".format(*verts[b]))
    for t in used_t:
        obj.append("vt {:.6f} {:.6f}".format(*texcoords[t]))
    obj.append(f"usemtl {slot.key}")
    for vi, ti in faces:
        toks = []
        for v, t in zip(vi, ti):
            toks.append(f"{local[v] + 1}/{local_t[t] + 1}" if t >= 0 else f"{local[v] + 1}")
        obj.append("f " + " ".join(toks))

    mhclo = [
        "# Generated by tools/make_helper_proxies.py -- see that file for why this",
        "# is the base mesh's own helper geometry rather than invented shape.",
        "#",
        "# The fitting is the IDENTITY: each vertex names its own base vertex at",
        "# weight (1, 0, 0) with no offset, so the proxy tracks the helper exactly",
        "# for every body shape and pose.",
        slot.rationale,
        "",
        f"uuid {slot.uuid}",
        "basemesh hm08",
        "",
        f"name {slot.name}",
        "tag MakeHuman™",
        "",
        f"obj_file {slot.key}.obj",
        f"material materials/{slot.key}.mhmat",
        f"z_depth {slot.z_depth}",
        "",
        "verts 0",
    ]
    for b in used:
        mhclo.append(f"{b} {b} {b} 1.00000 0.00000 0.00000 0.00000 0.00000 0.00000")

    mat = "# Generated by tools/make_helper_proxies.py.\n#\n" + slot.material

    return (
        {
            f"{slot.key}.obj": ("\n".join(obj) + "\n").encode(),
            f"{slot.key}.mhclo": ("\n".join(mhclo) + "\n").encode(),
            f"materials/{slot.key}.mhmat": mat.encode(),
            # Beside the proxy, NOT in data/litspheres: that directory is what
            # the body-skin chooser enumerates, and a matcap dropped there
            # becomes a body skin the user can pick. The eye matcap only escapes
            # that by a substring test on "eye" (`main.cpp:888`), the kind of
            # rule that quietly swallows the next asset whose name matches.
            f"skinmat_{slot.key}.png": make_litsphere(slot.tint),
        },
        f"{slot.key}: {len(used)} vertices, {len(faces)} faces "
        f"({', '.join(g.removeprefix('helper-') for g in slot.groups)}), "
        f"{len(used_t)} uvs",
    )


def is_stale(path: Path, fresh: bytes) -> bool:
    """Whether the committed @p path disagrees with a freshly derived @p fresh.

    The text files are compared byte for byte, but the PNG is compared by its
    decoded PIXELS. Comparing PNG bytes was tried first and failed in CI while
    passing locally. Reproduced in a linux/amd64 container with the same Pillow
    (12.3.0) the runner installs, the difference was measured exactly: the
    committed file was 27,922 bytes and the freshly derived one 28,660, and
    `np.array_equal` on the decoded arrays was True. Same pixels, different
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
    ap.add_argument("--slot", action="append", choices=[s.key for s in SLOTS],
                    help="only this slot (repeatable); default is all of them")
    args = ap.parse_args()

    slots = [s for s in SLOTS if args.slot is None or s.key in args.slot]
    stale, reports = [], []
    for slot in slots:
        files, report = build(slot)
        reports.append(report)
        if args.check:
            stale += [f"{slot.key}/{name}" for name, data in files.items()
                      if is_stale(slot.out / name, data)]
        else:
            for name, data in files.items():
                dst = slot.out / name
                dst.parent.mkdir(parents=True, exist_ok=True)
                dst.write_bytes(data)

    for report in reports:
        print(report)
    if args.check:
        if stale:
            print("stale generated assets: " + ", ".join(sorted(stale)), file=sys.stderr)
            print("Re-run without --check to regenerate.", file=sys.stderr)
            return 1
        print("committed assets match the generator")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
