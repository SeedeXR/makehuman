# `correctives.json` — the corrective authoring manifest

The one file an author writes to add pose-space correctives. It names the joints
that drive them, where each sculpted example sits, and which `.target` payload
belongs to each.

Unlike the other formats documented here, this one is **ours** — it has no
Python ancestor. Owner directive 12.4 asks for it to be versioned
**conservatively** and specified in writing; this is that specification.

Read by `mh::core::loadCorrectiveManifest`
(`include/makehuman/core/CorrectiveManifest.h`).

---

## Where it sits

Directive 12.4 splits the corrective pipeline into three layers with one
direction of flow:

| Layer | What | Lifetime |
|---|---|---|
| **Authoring** | this manifest + sparse `.target` payloads | committed, diffable, hand-editable |
| **Compile** | solve the RBF once, bake a blob | build artefact |
| **Runtime** | the blob only | disposable cache, rebuilt on hash mismatch |

All three exist now. The manifest is the contract the other two are written
against, and `core::loadOrCompileCorrectives` is the entry point that ties them
together: it reads this file, looks for `<stem>.mhcorr` beside it, and rebuilds
whenever the blob is missing, stale, corrupt, or in a blob format the build no
longer reads.

**The blob is derived and gitignored.** Deleting it costs a recompile and
nothing else, and failing to WRITE it does not fail a load -- a read-only asset
directory costs the rebuild time on every load, not the character.

## Why JSON and not TOML

Directive 12.4 offers either. JSON, because **nlohmann/json is already a
recorded dependency** (`LICENSING.md` §5.1) and a TOML library would be a new
one. Hard rule 6 forbids adding a dependency without recording it, and TOML buys
nothing here that would justify the entry.

## Example

```json
{
  "formatVersion": 1,
  "topologyHash": "e38c060123b5d0db",
  "kernel": "gaussian",
  "radius": 0.9,
  "drivers": [
    { "joint": "upperarm01.L", "component": "swing" },
    { "joint": "lowerarm01.L", "component": "twist" }
  ],
  "poses": [
    { "name": "arm_up",   "signal": [0.0, 1.2, 0.0, 0.4], "delta": "deltas/arm_up.target" },
    { "name": "arm_back", "signal": [0.3, 0.0, 0.8, 0.0], "delta": "deltas/arm_back.target" }
  ]
}
```

## Fields

Every field below is **required**. There are no optional fields and no defaults,
which is deliberate: a corrective that silently does not appear, or appears in
the wrong place, is worse than a file that will not load.

### `formatVersion` (integer)

The version of this format. This build reads **1** and refuses anything else,
including a missing value.

Refusing a *newer* version rather than reading the parts it recognises is what
directive 12.4 means by versioning conservatively. Silently ignoring a field
added in version 2 means geometry that quietly does not appear — the failure
mode nobody reports because nothing looks broken.

### `topologyHash` (string, exactly 16 hex digits, either case)

`mh::core::topologyHash` of the base mesh this was authored against. The shipped
base mesh is `e38c060123b5d0db`.

Directive 12.7's **topology hash guard**. A `.target` is a list of *vertex
indices*; renumber the base mesh and every delta lands somewhere else, with
nothing to notice. Comparing this against the mesh actually loaded is the
caller's job — stating it is the manifest's.

Parsed strictly. A hash that parsed loosely could silently equal a different
topology's, and the guard exists precisely to be trusted.

### `kernel` (string)

Only `"gaussian"` exists. Anything else is refused rather than falling back:
a thin-plate kernel is a different shape, and producing Gaussian output for a
manifest that asked for thin-plate is the kind of wrong that looks plausible.

Thin-plate is the case that would justify the Eigen use cleared in
`LICENSING.md` §5.1.1 — it is only *conditionally* positive definite and needs a
polynomial term and a pivoted factorisation. The Gaussian is positive definite,
so Cholesky is both correct and stable, and fails loudly when the system is too
degenerate to trust (`src/foundation/Rbf.cpp`).

### `radius` (number, greater than zero)

The Gaussian width, in the units of the signal space (radians for both
components).

**Tie it to the spacing of your example poses.** A narrow Gaussian reproduces
every sculpted pose exactly and still interpolates badly, because between
samples each kernel has already decayed. Measured on a 7×7 grid
(`tests/unit/test_rbf.cpp`), worst error against the sampled function:

| radius | worst error |
|---|---|
| 1× sample spacing | 1.58e-1 |
| 2× | 2.28e-2 |
| **3×** | **2.31e-3** |
| 4× | 4.17e-4 |

Two orders of magnitude from a parameter that does not look
accuracy-critical. Roughly **3× the spacing** is the working rule.

Too *large* also fails, and fails loudly: past about twelve times the spacing
the interpolation matrix collapses toward rank one and the solve is refused.

### `drivers` (array, at least one)

What the correctives key on. Each entry:

- `joint` (string) — a bone name in the loaded skeleton.
- `component` (string) — `"swing"` or `"twist"`.

The two halves of `foundation::swingTwist`. **Swing** is how far the bone is
bent away from rest and contributes **three** numbers (the rotation vector).
**Twist** is rotation about the bone's own length and contributes **one** signed
angle. Directive 12.3 rules out Euler angles for the reason they always fail
here: three sequential angles gimbal-lock, and near the lock a twist reading
jumps 180° while the rotation barely moves.

The same joint may appear under **both** components — driving a shoulder by its
swing and its twist is the ordinary case. The same joint under the **same**
component twice is refused: it would count that joint's contribution twice, and
every distance in signal space would be wrong in a way that still solves and
still renders.

**Order matters.** The signal vector is the drivers' contributions concatenated
in the order listed, and every pose's `signal` is read in that order.

### `poses` (array, at least one)

Each sculpted example:

- `name` (string, non-empty, unique) — used in error messages and, later, in the
  compiled blob.
- `signal` (array of numbers) — where this pose sits in signal space. Length
  must equal the drivers' total dimension: 3 per swing, 1 per twist. In the
  example above, one swing plus one twist is **4**.
- `delta` (string) — path to the `.target` payload, **relative to the manifest's
  own directory**. A subdirectory is fine. Absolute paths, empty paths and any
  path containing `..` are refused: a manifest is data, and a shipped asset must
  not be able to name any file on the machine.

Two poses at the same point in signal space are refused. That is the realistic
authoring mistake — the same pose keyed twice — and it makes the interpolation
matrix singular. `rbfSolve` would refuse it too, but only as "not solvable",
which tells the author nothing; refused here, both poses can be named.

## The content hash

`CorrectiveManifest::hash` is FNV-1a over the manifest's **content in a
canonical order**. It is what makes the compiled blob a disposable cache: the
blob records this hash, and `loadOrCompileCorrectives` rebuilds on a mismatch.

Deliberately **not** part of the hash:

- whitespace and key order — reformatting a manifest must not discard a cache;
- the manifest's own location — otherwise every checkout invalidates everything;
- the printed form of a number — `0.9` and `0.90` hash the same, `0.9` and `0.8`
  do not.

Deliberately **part** of it: the format version, topology hash, radius, every
driver, and every pose's name, signal and payload path *as written*.

## What an export can carry

A corrective reaches a **baked** export (`.obj`) and no other kind, and this is
a property of the target formats rather than a gap here.

The formats that carry a skeleton — glTF, FBX, UsdSkel — get a **live rig** from
this application: rest geometry with a posed armature, so the consumer computes
the deformation itself. The rest geometry is uncorrected on purpose. A
pose-space corrective is not a rest shape, and baking this pose's bulge into
something labelled "rest" would carry it into every other pose the consumer
sets. None of the three has a pose-driven shape to put one in instead.

So `--correctives` with a live-rig target writes a file **byte-identical** to
one written without it (measured with `cmp`; `app_correctives_live_rig_unchanged`
pins it). The application says so on stderr rather than leaving it to be
discovered, and `tools/run_blender_validation.sh` round-trips the `.obj` that
does carry it — Blender reports 164.74 dm^2 of surface without the corrective
and 165.76 with, on a mesh whose bounding box is identical either way.

The way out, when something needs it, is a **blend shape** at the weight the RBF
returned for the exported pose: all three formats have those, and this
application already writes 34 of them. Right at the pose in the file, adjustable
rather than invisible anywhere else. Not built yet.

## Not in version 1

- **Composition rules.** Directive 12.4 lists them; there is no consumer yet, so
  there is no field for them. Adding one now would be a guess at a shape the
  compiler has not asked for.
- **Poses given as `.bvh` files** rather than literal signal values. The
  directive says "example pose values", and literal values keep the compiler
  free of the pose-loading stack. A future version could accept either.
- **Per-pose radius.** One radius for the whole set until something needs
  otherwise.

Each of these is a reason the format version exists.
