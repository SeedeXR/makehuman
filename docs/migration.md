# Coming from MakeHuman 1.x

Everything here was **measured against this build**, not inferred from the
format documentation. Where a claim has a number, the number came from running
the command shown.

## Your `.mhm` files open

A `.mhm` the reference wrote loads directly:

```
makehuman --load your-character.mhm
```

Measured on `tests/golden/mhm/reference_save.mhm`, a file the reference itself
wrote: **12 of its 12 `modifier` lines applied**, and the camera, name, uuid
and tags came through. The version line is compared on **major and minor only**,
so `v1.3.0` and `v1.3.7` are the same file to both programs.

There is no import step and no conversion. The reader is `src/core/Mhm.cpp`;
the format is documented in [`formats/mhm.md`](formats/mhm.md).

## What this build does not ship

This port ships a **CC0 subset** of the reference's assets, not all of them.
That is a licensing decision recorded in `LICENSING.md`, not an oversight, and
it is the one thing that will actually bite you.

A character referencing an asset that is not here loads with a warning and the
default in that slot:

```
clothes Fancy_Dress 00000000-...: no clothes proxy with UUID 00000000-...;
using the default
```

**Read that warning before you save.** Measured: on re-save that line becomes
`clothes none`, so the reference to the missing outfit is **gone from the new
file**. The character still loads, but the record of what it used to wear does
not survive the round trip.

So, in order:

1. Load the character and read the warnings.
2. If any slot reports a missing asset, **keep the original file** — copy it
   somewhere before saving over it.
3. Install the asset, or accept the default, and only then save.

A line this build does **not** recognise at all is safer: it is preserved
verbatim. Measured on the same file — a `proxy Proxy741 1111...` line survived
a full load-and-save round trip untouched, because unknown keys are carried
through rather than dropped.

## Proxies are found by UUID, never by name

This is inherited behaviour, not a new restriction: the reference itself
refuses filename references (`apps/gui/proxychooser.py:549-551`, *"Loading
proxies from filename is no longer supported"*). If you hand-edit a `.mhm`,
edit the UUID.

## Differences you will notice

| | MakeHuman 1.x | here |
|---|---|---|
| Anatomical detail | on by default | **off by default**, a tick beside the Genitals picker turns it on |
| Skinning | linear blend | **dual quaternion** by default; `--skinning linear` is the old behaviour exactly, and `--skinning cor` is centres of rotation |
| Rig names on export | fixed | `--rig-names native\|auto\|makehuman1\|mixamo` |
| Units | decimetres internally | unchanged — decimetres, Y up |

Re-saving a file adds the slot lines this build tracks (`eyes`, `teeth`,
`hair`, `litsphere`, `skinMaterial` and so on) with their current values. The
file grows; nothing you had is removed by that.

## If something does not survive

Two things are worth knowing before you conclude the port has a bug:

- **`--list-presets`, `--list-poses`, `--list-animations`** print what this
  build actually has, which is usually faster than guessing why a slot fell
  back to a default.
- The reference is still here, under `legacy/python/`, and this port uses it
  as an oracle rather than as documentation -- the animation rest-pose fix was
  settled by reading a `.bvh` with the reference's own parser when our reader
  could only have agreed with itself. `CLAUDE.md` records how to run it. If a
  character looks wrong, opening it in both is the fastest way to tell a
  porting bug from a missing asset.
