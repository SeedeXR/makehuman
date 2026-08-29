# File formats

Two families live here.

**MakeHuman's own formats** — `.target`, `.mhclo`/`.proxy`, `.mhmat`, `.mhskel`,
`.mhw`, `.mhm` — have **no specification anywhere**. The Python source was the
only definition. These documents are how that knowledge stops being locked
inside `legacy/python/`, which is scheduled for deletion.

**Interchange formats** — OBJ, glTF/GLB, FBX, USD, Collada, STL, 3MF, BVH — are
published, but they disagree with each other on conventions that no file
announces. That table is below, because those disagreements have cost real time
three separate times.

---

## The conventions table

Every row is something that produces a **plausible but wrong** result when
carried over from another format. None of them fails loudly.

| | UV origin | Up axis | Matrix order | Units |
|---|---|---|---|---|
| **MakeHuman internal** | bottom-left | Y | row-major, column vectors | decimetre |
| **OBJ** | bottom-left | (not recorded) | — | whatever you write |
| **glTF / GLB** | **top-left** | Y (fixed by spec) | **column-major** | metre (by convention) |
| **FBX** | bottom-left | recorded in header | row-major | centimetre (conventional) |
| **USD** | bottom-left | **recorded** (`upAxis`) | row-major | recorded (`metersPerUnit`) |
| **BVH** | — | **not recorded** | — | whatever the file used |
| **Blender (importer's world)** | bottom-left | **Z** | — | metre |

### What each row cost

- **glTF flips V.** Its UV origin is top-left while everything else here is
  bottom-left, so `v` is written as `1 - v`. Getting it wrong mirrors every
  texture vertically — the model looks fine until it is textured.
  (`src/io/GltfWriter.cpp`)
- **USD does *not* flip V.** Having just written the glTF flip, carrying it over
  is the natural mistake. A test asserts `0.25` stays `0.25`.
  (`tests/golden/test_usd_writer.cpp`)
- **glTF matrices are column-major**, ours are row-major, so they transpose on
  the way out. Writing them unchanged produces a file that loads, poses, and is
  wrong in a way that looks like bad skin weights.
- **BVH does not record its up axis.** Both shipped MakeHuman pose files are
  Z-up. A reader that assumes Y-up produces a complete, plausible skeleton
  **lying on its side**. `readBvh` measures it instead of assuming.
  (`src/io/BvhReader.cpp`)
- **Blender's world is Z-up** and every importer rotates a Y-up file on the way
  in, so a model's height arrives as Blender's **Z**. Reading the Y component
  reports the body's *depth* — which looks like a 4x unit error that is not
  there. (`tools/blender_validate.py`)

### Units

Every writer takes the same `Unit` enum — decimetre, metre, centimetre, inch —
and `tests/golden/test_unit_correctness.cpp` measures the exported height back
**out of each file** at all four, plus asserts that all writers agree with each
other at the same unit.

This is tested because the reference gets it wrong:
`legacy/python/plugins/9_export_fbx/fbx_binary.py:736` hardcodes
`scale_factor = 10.0` with the correct `10.0/config.scale` commented out on the
line above, so every non-decimetre FBX export there is off by the configured
scale.

The base mesh is **16.9455 dm** tall — 169.5 cm, a real human height. That
number is the fixture every unit test compares against.

---

## Support matrix

| Format | Read | Write | Rig | Morphs | Notes |
|---|---|---|---|---|---|
| OBJ | yes | yes | — | — | quads preserved; `.mtl` written |
| glTF / GLB | via assimp | **yes, from spec** | yes | yes | `JOINTS_0`/`WEIGHTS_0`, `targets` |
| FBX | via assimp | via assimp | yes | yes | binary 7500 |
| USD (`.usda`) | — | **yes, from spec** | — | — | no OpenUSD dependency |
| Collada, STL, 3MF | via assimp | via assimp | — | — | |
| BVH | **yes, from spec** | — | n/a | n/a | pose import |
| `.target` etc. | yes | `.mhmat` only | — | — | see the per-format pages |

"From spec" means written from the published format rather than translated from
another implementation — which is also what keeps those writers on the
permissive side of the licence boundary (`LICENSING.md` §4).

---

## Verification

Three independent layers, because the first two share lineage with what they
check:

1. **Parity fixtures** captured from the Python reference — but we were ported
   *from* it, so a shared misunderstanding passes.
2. **assimp** as a second reader — but it also *writes* our FBX.
3. **Blender** (`tools/run_blender_validation.sh`), a third implementation that
   has never seen this codebase. 7/7 exports currently agree.

Per-format pages: [target](target.md) · [mhclo](mhclo.md) · [mhmat](mhmat.md) ·
[mhskel](mhskel.md) · [mhw](mhw.md) · [mhm](mhm.md) · [bvh](bvh.md)
