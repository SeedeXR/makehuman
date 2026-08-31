# Project Context

**Version:** 1.0 · **Created:** 2026-08-29 · **Owner:** alex@bsa.ai

---

## 1. Vision

Take MakeHuman — a mature but ageing Python/PyQt5/legacy-OpenGL parametric human
generator — and rebuild it as **a native C++/Qt6 desktop application for macOS**
that is fast, robust, modern in interchange, and MetaHuman-class in capability,
while staying **open source** and legally clean.

The end state: **no Python in the shipped product.** A compilable, notarisable
`.app` bundle for macOS (arm64 first, universal second).

---

## 2. Objectives

| # | Objective | Definition of done |
|---|---|---|
| O1 | Port core to C++23 | Mesh, targets, modifiers, proxy, skeleton, skinning in `src/core`, `src/rig`, with parity tests against the Python reference |
| O2 | Modern renderer | Qt RHI (Metal on macOS) replacing legacy fixed-function GL; persistent GPU buffers; GPU skinning; PBR |
| O3 | Robust interchange | **Import and export** OBJ, FBX, glTF 2.0/GLB, USD, STL, Collada, BVH — with skeletons, skinning weights, blendshapes, PBR materials |
| O4 | MetaHuman-class character tooling | High-fidelity facial rig (FACS), correctives/PSD, LOD chain, groom, physically-based skin |
| O5 | Modern dockable UI | Dark theme (Lightroom / MetaHuman feel), Lucide icons, 42dot Sans, dockable + snappable panels, saveable/resettable workspaces, per-panel six-dot menu |
| O6 | Data-driven character generation | Train a generative model over the target/modifier space using in-repo CC0 data + permissively licensed public data |
| O7 | Licence hygiene | Every file, dependency, and asset traced to a licence; SBOM; clear commercial-use guidance |
| O8 | Performance | Beat the measured Python baseline (see §6) by a wide margin; hold 60 fps on the subdivided mesh |

---

## 3. Target users

1. **3D artists / character artists** — need fast iteration and clean FBX/GLB/USD into Blender, Unreal, Unity, Maya, Houdini.
2. **Game and film studios** — need batch generation, LODs, deterministic output, and a licence they can reason about.
3. **Researchers** (biomechanics, anthropometry, ML data generation) — need scriptable, reproducible, measurable humans.
4. **Hobbyists** — need it to launch fast and not fight them.

---

## 4. Licensing — the governing constraint

> **This section is authoritative. Read it before adding any dependency or code.**
> All claims below were verified in-session against the files cited.

### 4.1 What we inherited

| Component | Licence | Evidence |
|---|---|---|
| MakeHuman source code | **AGPL-3.0-or-later** | `LICENSE.md` §B, per-file headers e.g. `legacy/python/core/export.py:21-30` |
| MakeHuman bundled assets | **CC0-1.0** | `LICENSE.md` §C, `LICENSE.ASSETS.md`; asset headers e.g. `data/3dobjs/base.obj:3-4` |
| Output of the application | **Unencumbered** — project explicitly claims nothing | `LICENSE.md` §D |
| `plugins/9_export_fbx/{encode_bin,data_types,fbx_utils_bin,fbx_binary}.py` | **GPL-2.0-or-later** (Blender 2.79 derived, © Campbell Barton, Bastien Montagne) | `legacy/python/licenses/pyFbx-license.txt:1-20`, `legacy/python/plugins/9_export_fbx/encode_bin.py:21-23` |
| `plugins/9_massproduce/` | **MIT** | `legacy/python/plugins/9_massproduce/__init__.py:15` |
| numpy, PyOpenGL, transformations.py | BSD-3-Clause | `legacy/python/licenses/README.txt` |
| PyQt5 / Qt5 | GPL-3.0 (open-source track) | `legacy/python/licenses/README.txt` |

**Resolved question:** pyFBX is GPL-**2.0-or-later** ("either version 2 of the
License, or (at your option) any later version", `pyFbx-license.txt:4-6`), not
GPLv2-only. It is therefore upgradeable to GPLv3 and **compatible with AGPLv3**.
There is no licence conflict. *(An earlier automated pass flagged this as a
possible conflict; that flag was wrong and is retracted.)*

### 4.2 What this means for the port — state it plainly

The C++/Qt6 application is a **derivative work** of AGPL-3.0 MakeHuman. Therefore:

- ✅ **The app can be sold commercially.** AGPL does not prohibit charging money.
- ✅ **Everything a user creates with it is theirs, commercially, with no strings.**
  Assets are CC0 and the project disclaims all output (`LICENSE.md` §D). This is
  the freedom that matters most to the target users.
- ✅ **Anyone may fork it, modify it, and sell the result.**
- ⚠️ **A derivative of the application code must remain AGPL-3.0.** A closed-source
  proprietary fork is *not* permitted. This is copyleft and cannot be removed —
  relicensing would require consent from every MakeHuman copyright holder since 2001.
- ⚠️ **AGPL §13**: if the app is ever offered over a network, users of that service
  must be offered the source.

**How we maximise downstream commercial freedom despite copyleft** — the
deliberate architecture decision (see `architecture.md` §Licence boundary):

> New, independently-authored libraries that contain **no MakeHuman-derived logic**
> are placed in separately-licensed modules under **Apache-2.0**, so third parties
> can reuse them commercially without AGPL obligations. Only code that derives from
> MakeHuman stays AGPL.

Candidates for the Apache-2.0 clean side: the glTF/GLB/USD/FBX I/O layer written
from published specifications, the RHI render abstraction, the docking/workspace
UI framework, the math utilities. Candidates that are **irrevocably AGPL**: the
modifier/target system, the proxy fitting, the skeleton/weight logic, the `.mhm`
/`.mhclo`/`.mhmat` parsers — all ported from AGPL sources.

**Rule:** an Apache-2.0 module may never be written by translating an AGPL file.
It must be written from a specification or from scratch. If in doubt, it is AGPL.

### 4.3 MetaHuman — hard boundary

"MetaHuman functionality" in this project means **capability parity**: high-fidelity
facial rigging, FACS-based expression, LODs, groom, physically-based skin.

It does **not** and must never mean using Epic Games' MetaHuman assets, the
MetaHuman SDK/DNA Calibration libraries under their EULA terms, MetaHuman-exported
character data, or Epic trademarks. Epic's MetaHuman licence restricts use to the
Unreal Engine ecosystem and is incompatible with an AGPL open-source product.

*Note:* Epic released **MetaHuman DNA Calibration** tooling under Apache-2.0 in
part; **any** use of it must be licence-verified at the time of use and recorded in
`LICENSING.md`. Do not assume — verify the specific repo and version.

### 4.4 Dependency policy

Acceptable for linking into the AGPL app: MIT, BSD-2/3, Apache-2.0, Zlib, MPL-2.0,
LGPL-2.1/3 (dynamically linked), GPL-2.0-or-later, GPL-3.0, CC0, SIL OFL 1.1 (fonts).

**Forbidden:** GPL-2.0-**only** (incompatible with AGPLv3), proprietary/EULA-bound
SDKs (including the Autodesk FBX SDK — its licence is incompatible with AGPL
redistribution), anything with a non-commercial or field-of-use restriction, and
anything whose licence cannot be identified.

Every dependency is recorded in `/LICENSING.md` with name, version, licence, SPDX
identifier, source URL, and why it is needed.

---

## 5. Constraints

1. **macOS is the delivery target.** arm64 first. OpenGL is deprecated on macOS
   (capped at 4.1, no compute) — the renderer must not depend on it.
2. **Qt 6** under LGPL-3.0 (dynamically linked) or GPL-3.0. Never bundle a statically
   linked LGPL Qt without meeting relinking obligations.
3. **No Python in the shipped product.** Python may remain in `legacy/python/` as a
   reference oracle and in `tools/` for build-time asset processing only.
4. **The CC0 asset set is the crown jewels** — 126 MB of targets, the 19,158-vertex
   base mesh, the 163-bone rig. Never break compatibility with it.
5. **Backwards compatibility** with `.mhm`, `.mhclo`, `.mhmat`, `.mhskel`, `.target`,
   `.mhw`, `.mhpose` is required. Existing user models must load.

---

## 6. Measured baseline (the number to beat)

Measured 2026-08-29 on macOS 26.6.2, Apple arm64, Python 3.14.6, numpy 2.5.1.
Harness: `benchmarks/baseline_python_core.py` → `benchmarks/baseline_python.json`.

Base mesh: **19,158 verts · 18,486 quad faces · 21,334 UVs**.
Subdivided (1 level Catmull-Clark): **75,008 verts · 73,944 faces**.
Targets on disk: **1,280** `.target` files (mean 983 affected verts, max 5,480).

| Operation | Median |
|---|---|
| Load `base.obj` (text parse + `_update_faces`) | **211.8 ms** |
| `calcNormals` full mesh (face + vertex) | 5.18 ms |
| `updateIndexBuffer` (unweld + group sort) | 3.43 ms |
| Load 200 targets (ASCII parse) | 104.4 ms |
| Load **all 1,280** targets (ASCII parse) | **3,225.6 ms** (measured directly, not extrapolated) |
| Apply 200 targets @0.5 (full stack rebuild) | 4.84 ms |
| Apply 1 target (slider delta) | 0.04 ms |
| Catmull-Clark subdivide (build) | **202.3 ms** |
| Subdiv `update_coords` | 7.64 ms |
| Subdiv `calcNormals` | **20.57 ms** |

**Interpretation:** subdivided editing cannot hit 60 fps in the reference — normals
alone (20.57 ms) exceed a 16.6 ms frame budget, before the per-frame client-side
vertex upload (`legacy/python/lib/glmodule.py:479`, `glVertexPointer` — no VBOs).

### Success metrics

| Metric | Target |
|---|---|
| Cold app launch to interactive | ≤ 1.5 s |
| Base mesh load | ≤ 20 ms (10× faster) |
| All 1,280 targets loaded | ≤ 50 ms (**64× faster**) — via mmapped compiled blob |
| Full target-stack rebuild | ≤ 1 ms |
| Subdivided viewport | ≥ 60 fps sustained |
| Slider drag latency | ≤ 8 ms end-to-end |
| Peak RSS, default character | ≤ 400 MB |
| glTF/GLB round-trip | lossless for geometry, UV, skin, blendshapes |

---

## 7. Evaluation criteria

A change is accepted when:
1. It compiles clean at `-Wall -Wextra -Wpedantic` with no new warnings.
2. Its tests pass, including a **parity test against the Python reference** where a
   reference behaviour exists.
3. It does not regress any benchmark beyond 5% (see `test.md`).
4. It passes `/code-review` and `/ponytail-review`.
5. Any new dependency is recorded in `LICENSING.md`.

---

## 8. Known-broken reference behaviour — do NOT port

Verified defects in the Python reference. Parity tests must **exclude** these; the
C++ implementation is expected to be correct where the reference is wrong.

| Defect | Location | Nature |
|---|---|---|
| Tangent `t2` chained assignment | `legacy/python/core/module3d.py:411` | `t2 = w3[:,1] = w1[:,1]` — `t2` gets `w1`, not the difference. Tangents are wrong. |
| Tangent sum missing `axis=` | `legacy/python/core/module3d.py:429-430` | `np.sum(sdir[...])` collapses to a scalar |
| Operator precedence | `legacy/python/core/module3d.py:1212` | `A or B and C` — tangents recomputed regardless of `calculateTangents` |
| FBX unit scale hardcoded | `legacy/python/plugins/9_export_fbx/fbx_binary.py:736` | `scale_factor = 10.0`; correct line commented at `:735`. Combined with `cfg.scale *= 10` (`plugins/9_export_fbx/__init__.py:112`) this makes every non-decimetre FBX export a **10× error** |
| FBX forged Creator string | `legacy/python/plugins/9_export_fbx/fbx_binary.py:676-678` | Claims to be "FBX SDK/FBX Plugins version 2013.3" |
| FBX fixed FileId/CreationTime | `legacy/python/plugins/9_export_fbx/encode_bin.py:285-314` | Every file gets an identical fake FileId |
| Collada morph controller | `legacy/python/plugins/9_export_collada/dae_controller.py:208` | `NameError` on undefined `rmesh`; never called |
| `WarpTarget.apply` arg order | `legacy/python/apps/warpmodifier.py:73-74` | Positional args don't match `algos3d.Target.apply` |
| `mhp` pose loader | `legacy/python/shared/animation.py:1254,1292-1293` | `valid_file` never set True; always logs an error |
| `AnimationTrack.sparsify` | `legacy/python/shared/animation.py:243-261` | Assigns to a read-only property |
| BVH `NameError` | `legacy/python/shared/bvh.py:358` | Undefined `filepath` in a warning path |

### 8.0 Format traps

**`.mhmat` keys are read case-sensitively by the reference** (`material.py:369-448`
compares `words[0]` directly) but case-INsensitively by this port. Writing is
therefore not symmetric with reading: a file saying `diffusetexture` loads here
and is silently ignored by MakeHuman 1.x — the texture just disappears, with no
error. The writer must use the canonical camelCase spellings
(`Material.cpp` `kChannels::writeTexture`). No C++-only test can catch a
regression here, because our own reader accepts either; the guard is a literal
string check plus `tools/verify_material_roundtrip.py`.

**`body-poseunits.json` targets a skeleton MakeHuman does not ship.** Measured
by `tools/audit_poseunits.py`, gated in CI. Of its 61 poses, against **both**
`default.mhskel` and `mixamo_superset.mhskel`:

| | |
|---|---|
| fully resolvable | **29** |
| partially resolvable (silently do less than intended) | 24 |
| resolve to **nothing at all** | **8** |

The 8 dead ones are `TorsoRight`, `UpperLegForwardLeft`, `LowerLegBendLeft1/2`,
`FootDownLeft`, `FootUpLeft`, `Finger1CloseLeft`, `Finger2CloseLeft`.

24 referenced bone names are absent from both rigs. Many are a **different
naming generation** rather than genuinely missing joints — `spine1..spine4` for
our `spine01..spine03`, `neck` for `neck01..neck03`, `shoulder.L` for
`shoulder01.L`, `upperleg.L` for `upperleg01/02.L` — alongside ones we truly
lack (`collisionArm*`, `heel.L`, `metatarsal1..5`, `platysma03/06`, `scapula.L`).

So a pose-unit consumer needs an **explicit bone table**, exactly as the Mixamo
retarget did; name matching will silently drop a third of the data.

**Two poses are authoring errors in the reference asset**: `UpperArmUpLeft1` and
`UpperArmUpLeft2` drive `oris01`/`oris02` — *mouth* bones. Raising the left arm
must not move the lips. Pinned by the audit so it is never mistaken for our bug.

**`normalmapIntensity` does nothing in the reference's litsphere shader.**
`litsphere_fragment_shader.txt:74-77` computes
`(2.0*normalH - 1.0) * normalmapIntensity` and then `normalize(tbnMat * normal)`.
A uniform scale followed by a normalize **cancels exactly**, so the uniform has
no effect on the rendered image at any positive value. It would matter only
under `CALC_NORMAL_Z`, which the reference leaves commented out at `:64`.

Not ported: shipping a control that silently does nothing is worse than
diverging. Our shader scales **XY only, keeping Z**, which is the standard
normal-map strength idiom and what the `CALC_NORMAL_Z` branch was reaching for.
Verified by test — intensity 1.0 vs 0.01 changes the image; under the
reference's formula it changed **0 pixels**.

**The binormal discards handedness there too.** The reference builds
`cross(vNormal, tang)` (`litsphere_vertex_shader.txt:59`), ignoring the sign
Lengyel's method produces. On a symmetric human, mirrored UV islands have
opposite handedness, so one side of the body gets its normal-map lighting
inverted. We carry `tangent.w` through and apply it. Of a piece with not porting
the three tangent bugs already listed here.

### 8.0.1 Reference edits made to keep it runnable

Hard rule 2 permits editing `legacy/python/` only to keep the oracle running.
Recorded here so the diff is never mistaken for a port decision:

| Edit | Why |
|---|---|
| `core/transformations.py`: 18 uses of `numpy.array(x, copy=False)` -> `numpy.asarray(x)` | numpy 2.0 made `copy=False` mean "never copy" and raise if a copy is needed, so every quaternion conversion raised `ValueError` on numpy 2.5. `asarray` is the documented replacement and is behaviour-identical on numpy 1.x. Without this the pose-unit fixture cannot be captured at all. |

### 8.1 Deliberate divergences from the oracle

Not reference *defects* — places where matching the reference exactly would be
worse. Parity tests must expect the C++ behaviour, not the Python one.

| Divergence | Why |
|---|---|
| **Non-finite values rejected** at three boundaries: `.mhmat` parsing, glTF export, and scene import | The oracle's `float()` accepts `nan`/`inf` and lets them propagate. They then reach the exporter, where JSON has no literal for either: the GLB was written, reported as successful, and rejected outright by every parser. Refusing at the boundary is the only place the value is still attributable to a file. |
| **`delete_verts` index capped at 2^24** | The oracle indexes a fixed-size array sized to the body (`proxy.py:115`), so an out-of-range index simply raises. We size a vector from the file, which turned a two-line asset into a 4.29 GB allocation — and `resize(v + 1)` in uint32 wrapped at `UINT32_MAX` into an out-of-bounds **write** (ASan: BUS, WRITE). The base mesh has 19,158 vertices. |
| **Draw ranges stay indexable when everything is hidden** | `module3d.py:859-860` collapses `grpix` to a zero-row array under a fully-hiding mask, so callers can no longer index by group id. We keep one all-zero entry per group. An all-zero range draws nothing, so the rendered result is identical. |
| **A truncated `verts` line is an error** | The oracle raises `IndexError`; we used to skip the line, which shifts every later proxy vertex onto the wrong reference triangle — wrong geometry with no diagnostic. |
| **Duplicate UUID: first wins, collision reported** | The oracle lets the last writer win, making resolution depend on directory iteration order. |
| **An unreachable bone is an error** | `skeleton.py:122-124` logs a warning and builds a partial skeleton. A rig that exports missing limbs with nothing in the log is worse than a refusal. |
| **`.mhmat` save is lossless** | `material.py:511-620` never writes `tag`, `autoBlendSkin` or the viewport colour, and cannot save an `autoBlendSkin` skin at all headlessly — `toFile` raises `AttributeError` on `G.app.selectedHuman` because `diffuseColor` routes through the skin blender (`:654`, `:1414`). In-app it writes the *blended* colour over the authored one. Losing user data on save is a defect, not a format rule. |
