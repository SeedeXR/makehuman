# Licensing

**Version:** 1.0 · **Created:** 2026-08-29 · **Status:** authoritative inventory

> Every claim in this document was verified against the cited file in-session.
> This is a factual inventory, **not legal advice**.

---

## 1. Summary for users

| Question | Answer |
|---|---|
| Can I use MakeHuman commercially? | **Yes.** |
| Can I sell what I create with it — models, renders, games, films? | **Yes, unconditionally.** Bundled assets are CC0 and the project disclaims all output. |
| Can I sell the application itself? | **Yes.** AGPL does not prohibit charging. |
| Can I fork it and modify it? | **Yes.** |
| Can I make a **closed-source** commercial fork? | **No.** AGPL-3.0 is copyleft — derivatives of the application code must also be AGPL-3.0. |
| If I run it as a network service, must I share source? | **Yes** — AGPL-3.0 §13. |
| Are there parts I can reuse commercially without copyleft? | **Yes** — the Apache-2.0 modules (§4). |

**In one line:** everything you *make* with MakeHuman is yours with no strings;
the application's *source code* stays open.

## 2. Why the code cannot be relicensed

The application is a derivative work of MakeHuman, which has been AGPL-3.0 since
2001 with contributions from many authors. Relicensing would require consent from
every copyright holder. That is not achievable, so **the AGPL obligation is
permanent** for the ported application code.

This is stated plainly rather than glossed, because the practical consequence
matters: a downstream user can build a business on MakeHuman, but cannot build a
proprietary closed fork of it.

## 3. Inherited licences (verified)

| Component | Licence | Evidence |
|---|---|---|
| MakeHuman source code | **AGPL-3.0-or-later** | `LICENSE.md` §B; per-file headers, e.g. `legacy/python/core/export.py:21-30` |
| Bundled assets (base mesh, targets, proxies, materials, poses, rigs, UVs, litspheres) | **CC0-1.0** | `LICENSE.md` §C; `LICENSE.ASSETS.md`; per-file headers, e.g. `data/3dobjs/base.obj:3-4`, `data/targets/asym/asym-eye-6-l.target:3-4` |
| UI images, icons, themes, GLSL shaders **written by the MakeHuman team** | **AGPL-3.0** | `LICENSE.md` §B explicitly includes "glsl shaders" and UI images; per-file headers, e.g. `data/shaders/glsl/litsphere_fragment_shader.txt:16` (`**Licensing:** AGPL3`) |
| `data/shaders/glsl/xray_{vertex,fragment}_shader.txt` | **GPL-2.0-or-later** (MeshLab, © 2005, 2009 Visual Computing Lab, ISTI-CNR) | `data/shaders/glsl/xray_fragment_shader.txt:5-19` — see §3.4 |
| Output of the application | **Unencumbered** — project claims nothing | `LICENSE.md` §D |
| `legacy/python/plugins/9_export_fbx/{encode_bin,data_types,fbx_utils_bin,fbx_binary}.py` | **GPL-2.0-or-later** (Blender 2.79, © Campbell Barton, Bastien Montagne) | `legacy/python/licenses/pyFbx-license.txt:1-20`; `legacy/python/plugins/9_export_fbx/encode_bin.py:21-23` |
| `legacy/python/plugins/9_massproduce/` | **MIT** | `legacy/python/plugins/9_massproduce/__init__.py:15` |
| `legacy/python/plugins/8_asset_downloader/` | **MIT**, © Joel Palmius 2016 | `legacy/python/plugins/8_asset_downloader/assetdb.py:13-15` |
| `legacy/python/data/animations/{walks,zombie}` | **AGPL-3.0**, author Jonas Hauquier | `data/animations/zombie/zombie.mhanim:1-3` |
| numpy, PyOpenGL, `transformations.py` | BSD-3-Clause | `legacy/python/licenses/README.txt` |
| PyQt5 / Qt5 | GPL-3.0 (open-source track) | `legacy/python/licenses/README.txt` |

### 3.1 Resolved: pyFBX is compatible

An automated pass flagged pyFBX (GPLv2) as possibly incompatible with AGPLv3.
**That flag was wrong.** The header reads:

> "either version 2 of the License, or (at your option) any later version"
> — `legacy/python/licenses/pyFbx-license.txt:4-6`

GPL-2.0-**or-later** is upgradeable to GPL-3.0, and GPL-3.0 is explicitly
compatible with AGPL-3.0 (§13 of both). **No conflict.**

Nonetheless the C++ port does **not** translate this code — the FBX writer is
written from the published specification, both to avoid inheriting GPLv2 and
because the existing writer targets FBX 7300 (2013) and is riddled with defects
(`memory/project_context.md` §8).

### 3.2 Unresolved upstream contradiction: target licence

Two sources disagree about `.target` files:

| Source | Says |
|---|---|
| `LICENSE.md` §C and `LICENSE.ASSETS.md` | Assets — explicitly including "Targets and modifiers" — are **CC0** |
| Every `.target` file header, e.g. `data/targets/asym/asym-eye-6-l.target:3-4` | "This asset was explicitly released as **CC0** in september 2020" |
| `legacy/python/core/algos3d.py:507-509` (`defaultTargetLicense()`) | `"license": "AGPL3"` |

**Authoritative reading: CC0.** Two independent explicit statements (the project's
licence file and the per-asset headers) outweigh a stale default in code. The code
fallback appears to be an un-updated remnant. Recorded here so the discrepancy is
not silently resolved by whoever reads the code first.

### 3.3 Asset licence headers are not machine-readable

`LicenseInfo.updateFromComment` (`legacy/python/makehuman.py:378-393`) accepts only
four lowercase keys — `author`, `license`, `copyright`, `homepage`. Every shipped
asset header uses prose and a capitalised `# Copyright (C) …`, so **none of them
set any field**; assets load with the default `LicenseInfo`. Verified: only
`data/animations/{walks,zombie}/*.mhanim` carry machine-readable keys.

**Action for the port:** parse the prose headers properly, or add a structured
sidecar. Licence metadata that silently defaults is worse than none.

### 3.4 The xray shader is MeshLab, not MakeHuman

The table in §3 used to read "GLSL shaders | AGPL-3.0" without qualification,
and that is wrong for one pair. `data/shaders/glsl/xray_{vertex,fragment}_shader.txt`
carries a MeshLab header — © 2005, 2009 Visual Computing Lab, ISTI - Italian
National Research Council — granting the

> "GNU General Public License ... either version 2 of the License, or (at your
> option) any later version"
> — `data/shaders/glsl/xray_fragment_shader.txt:12-14`

**Compatibility: no conflict**, by exactly the pyFBX reasoning in §3.1.
GPL-2.0-**or-later** upgrades to GPL-3.0, which is compatible with AGPL-3.0
(§13 of both). Shipping the file inside an AGPL-3.0 application is fine, and it
ships as reference data only: `xray.mhmat`'s `shader` line is parsed down to a
path stem (`src/core/Material.cpp:59`) and the GLSL itself is never opened —
this renderer picks its shading model from settings, not from the material.

**Porting: forbidden.** `memory/todo.md` listed `xray` among the reference
shaders still to translate into `resources/shaders/rhi/`. It is now excluded
explicitly. Translating it would pull GPL-2.0-or-later text into our shader
tree, and `resources/shaders/rhi/` is where the Apache-2.0 `mh_render` looks —
rule 1 below forbids exactly that direction, and rule 4 ("when in doubt it is
AGPL") does not rescue a file that is not ours to relicense. If the xray LOOK
is ever wanted, it is written from the technique — one minus a power of the
view/normal dot product — the same call §3.1 records for FBX.

Re-derived rather than trusted: `tools/audit_licences.py` scans every bundled
text file for a GPL or non-commercial grant and fails on any not recorded here.
It found this one.

### 3.5 FACS is a taxonomy used as fact, not as text

`src/rig/Facs.cpp` holds a table of 30 Facial Action Coding System Action
Units. What it takes from FACS is the **AU number, the short action name, and
the muscle FACS attributes the action to** — "AU12, Lip Corner Puller,
zygomaticus major". Those are facts about faces and about a numbering scheme in
universal scientific use; no sentence, table, scoring rule or intensity
criterion is reproduced from the FACS manual, which is a copyrighted work we do
not hold and have not copied.

The other half of each row is MakeHuman's own: the unit names come from
`data/poseunits/face-poseunits.json`, CC0 like the rest of `data/`. The mapping
between the two halves is anatomy — the AU names a muscle and the unit is named
after the same muscle — which is why `ActionUnitInfo` stores the muscle in the
row. It is there so the next reader can re-derive the mapping instead of
trusting it, the same discipline §3.4 applies to the xray shader from the other
direction.

**Nothing is linked and nothing is a dependency**, so §5 has no row to gain.
This section exists because "where did 30 rows of Action Units come from" is a
provenance question, and this is the file the project answers those from.

## 4. The Apache-2.0 clean-room boundary

To give downstream users genuine commercial reuse of the parts that are ours to
give, new independently-authored modules containing **no MakeHuman-derived logic**
are licensed **Apache-2.0**:

| Module | Licence | Why |
|---|---|---|
| `mh-foundation` | Apache-2.0 | Math, jobs, logging — written from scratch |
| `mh-rhi` | Apache-2.0 | Render abstraction over Qt RHI — new |
| `mh-scene` | Apache-2.0 | Generic scene graph — no counterpart in the reference |
| `mh-io` | Apache-2.0 | Format readers/writers from **published specifications** |
| `mh-ui-dock` | Apache-2.0 | Docking/workspaces — the reference has none |
| `mh-core` | **AGPL-3.0** | Mesh, targets, modifiers — ported from AGPL sources |
| `mh-rig` | **AGPL-3.0** | Skeleton, skinning, pose — ported |
| `mh-asset` | **AGPL-3.0** | `.mhm`/`.mhclo`/`.mhmat`/`.mhskel` parsers — ported |
| `mh-render` | Apache-2.0 | New renderer; material *semantics* it consumes are data, not code |
| `mh-app` | **AGPL-3.0** | The application, linking everything |

**The rules that make this real, not cosmetic:**

1. An Apache-2.0 module is **never** produced by translating an AGPL source file.
   It is written from a specification or from scratch.
2. Dependency arrows point **upward only** — AGPL may depend on Apache-2.0, never
   the reverse.
3. Every file carries an SPDX header: `// SPDX-License-Identifier: Apache-2.0`
   or `AGPL-3.0-or-later`.
4. **When in doubt, it is AGPL.** Doubt is resolved conservatively, always.
5. The combined *binary* is AGPL-3.0 — that is what users receive. The Apache-2.0
   modules are separately reusable **as source**.

### 4.1 A third licence: BSD-3-Clause, for one vendored file

`legacy/python/core/transformations.py` carries **two** licence statements, and
they conflict:

- Lines 6-33 are Christoph Gohlke's **BSD-3-Clause** notice, in real `#`
  comments — the file's actual licence header.
- Lines 47-66 are MakeHuman's **AGPL-3.0** boilerplate, inside the module
  **docstring**, on a file whose stated author is Gohlke.

Applying an AGPL header to someone else's BSD code does not relicense it.
MakeHuman may distribute it under BSD terms and the combined work may be AGPL,
but the file itself remains BSD-3-Clause, and BSD's one real condition is that
the notice travels with the code.

So `include/makehuman/foundation/Transform.h` and `src/foundation/Transform.cpp`
are **SPDX `BSD-3-Clause`** and reproduce Gohlke's notice verbatim. They are the
only files in this project under a third licence.

This is *better* for us, not worse: BSD is permissive, so these conversions sit
legitimately in the Apache-2.0 `mh_foundation` module rather than being trapped
on the AGPL side.

**Two CI gates cover it:** the SPDX gate admits `BSD-3-Clause`, and a second
gate fails any BSD-licensed file that does not carry a `Copyright (c)` line —
because dropping the notice is the single way to actually breach BSD.

## 5. Dependencies

### 5.1 Current (verified present on the build machine)

| Dependency | Version | Licence | SPDX | Use |
|---|---|---|---|---|
| Qt | 6.11.1 | LGPL-3.0 (dynamic) / GPL-3.0 | `LGPL-3.0-only` | Core, Gui, GuiPrivate, Widgets, **Svg**, ShaderTools — UI, RHI, icon rendering, shader tools |
| Eigen | 5.0.1 | MPL-2.0 | `MPL-2.0` | Linear algebra |
| oneTBB | 2023.1.0 | Apache-2.0 | `Apache-2.0` | Job system |
| assimp | 6.0.4_1 | BSD-3-Clause | `BSD-3-Clause` | **Multi-format import/export** (FBX 7500 binary, Collada, STL, 3MF) and an independent reader validating the formats we write ourselves. Linked into `mh_io`. See §5.3. |
| draco | 1.5.7 | Apache-2.0 | `Apache-2.0` | **glTF geometry compression** (`KHR_draco_mesh_compression`). Linked into `mh_io`, and **OPTIONAL** on the same terms as assimp: `find_package(draco QUIET)`, so a build without it writes glTF uncompressed rather than failing to configure. `io::dracoAvailable()` reports which build this is. Licence text is in the headers themselves, e.g. `/opt/homebrew/include/draco/compression/encode.h:1-13`. |
| Catch2 | v3.7.1 (pinned) | BSL-1.0 | `BSL-1.0` | Tests |
| **AppKit** (macOS system framework) | ships with macOS | Apple SDK licence | — | One call: `accessibilityDisplayShouldReduceMotion` in `src/ui/Motion.mm`. There is no Qt API for the reduce-motion setting and `QSettings` cannot read another application's preference domain (measured: `com.apple.dock` reports 0 keys through QSettings while `defaults read` lists dozens). A system framework on the target OS, dynamically linked, imposing no obligation on this project's licensing — recorded because the rule is to record every dependency, not only the redistributable ones. |
| nlohmann/json (`nlohmann_json`) | v3.11.3 (pinned, SHA256-verified) | MIT | `MIT` | `.mhskel` parsing in `mh_rig`. **`ordered_json` specifically**: the reference loads with `OrderedDict` and its bone ordering pass iterates in file order (`skeleton.py:112-121`), so a parser that sorts or hashes keys silently produces a different bone order. |
| Lucide | 1.37.0 (bundled) | ISC | `ISC` | Icons — see §5.4 |
| 42dot Sans | bundled, OFL text included | SIL OFL 1.1 | `OFL-1.1` | Typeface — see §5.4 |

**Developer tooling only — not linked, not redistributed:**

| Tool | Version | Licence | SPDX | Use |
|---|---|---|---|---|
| Pillow | 12.3.0 (unpinned) | HPND (MIT-like) | `HPND` | Two tools. `tools/make_appicon.py` masks the brand logo into Apple's squircle geometry and emits the `.iconset`; `tools/make_eyes.py` recolours the shipped iris into the six eye colours and, with `--check`, verifies they still match the generator. The committed `AppIcon.icns` and the committed `*_eye.png` are what the build consumes, so a machine without Pillow builds fine. Installed in `.venv-mh` locally and **pip-installed in the `inventories` CI job** since 2026-09-07, so that `--check` can run there. Never in the application. |
| NumPy | 2.5.2 local / 2.5.3 CI (unpinned) | BSD-3-Clause | `BSD-3-Clause` | The array maths behind `tools/make_eyes.py`'s iris mask and its staleness comparison. Same standing as Pillow: a developer and CI tool, never linked, never redistributed, and unpinned because pinning a dev tool is churn for no safety. The honest trade: if a future NumPy or Pillow changes rounding, the gate reports the committed assets as stale when they are not. That failure is loud and harmless, and the fix would be to pin at that point. Verified 2026-09-07 that 2.5.2 and 2.5.3 both agree with the committed PNGs. |
| **Autodesk FBX SDK** | 2020.3.9 | Autodesk proprietary | — | **VALIDATOR ONLY**, out-of-process, exactly like Blender. Never linked, never a build dependency, never redistributed. `CLAUDE.md` hard rule 6 forbids it as a *dependency* and that stands: reading our own output with a separate tool is not one. **Linking it would require this file and hard rule 6 to be changed first.** |
| **Maya** | 2027 (`mayapy`) | Autodesk proprietary | — | Same standing: an external reader used to check that what we write imports correctly. |
| Blender | 5.2 | GPL-3.0 | `GPL-3.0` | Same standing — the precedent. A separate process reading our files; no Blender code is linked or copied. |

All redistributed dependencies are compatible with AGPL-3.0 distribution.

**MIT (nlohmann/json)** is permissive and imposes only attribution; the licence
text ships in the fetched archive (`LICENSE.MIT`). It is fetched as a pinned
release archive of the headers only, verified by `URL_HASH SHA256=...`, so the
build cannot be changed under us by a moved tag.

**Qt/LGPL obligation:** Qt must be **dynamically linked**, and the distribution
must permit relinking against a modified Qt. The macOS bundle ships Qt as
frameworks and includes this notice. Static Qt would require the commercial
licence or full GPL relinking provisions.

### 5.1.1 Cleared for use, no caller yet

A library recorded here has had its licence verified and may be depended on; it
simply has none of our code calling it so far. `tools/audit_dependencies.py`
treats 5.1 and its subsections as the allow-list, so adding the `find_package`
does not need this file changed again -- move the row up into 5.1 when it lands,
with the version and linkage as verified on the build machine at that time.

| Library | Licence, as verified 2026-09-09 | Cleared for | Conditions |
|---|---|---|---|
| **Eigen** 5.0.1 (installed) | `Eigen/` is **MPL-2.0** — `COPYING.MPL2`, and the notice in `include/eigen3/Eigen/src/Core/Matrix.h`. A grep for "GNU Lesser General Public" and "GNU General Public" across the whole of `include/eigen3` returns **nothing**. The only non-MPL files are MINPACK-BSD, and every one is under `unsupported/Eigen/src/LevenbergMarquardt/`. | The **offline** RBF solve (directive 12 step 4) and, later, the per-vertex 3x3 SVD that optimised centres of rotation needs. | Header-only, so no linking obligation. MPL-2.0 is file-level copyleft and does not reach our files, so it sits under both the Apache-2.0 and the AGPL-3.0 modules. **`unsupported/` is excluded.** Offline tooling only — see the runtime note in 5.2.1. **No Eigen type crosses into ours**: `.coeffs()` is `[x,y,z,w]` against our `[w,x,y,z]`, the trap `CLAUDE.md` already names. |

### 5.2 Forbidden

| Item | Reason |
|---|---|
| **Autodesk FBX SDK** | See §5.3 — free of charge, but its EULA cannot be combined with AGPL *distribution* |
| **Epic MetaHuman** assets, SDK, DNA, or exported data | EULA restricts use to the Unreal ecosystem |
| GPL-2.0-**only** code | Incompatible with AGPL-3.0 |
| Anything non-commercial or field-of-use restricted | Conflicts with the freedoms we promise |
| **FFTW** | GPL-2.0-or-later without the paid MIT licence; would put GPL behind the Apache-2.0 modules. No FFT in the roadmap either — see 5.2.1 |
| **Ceres Solver**, as packaged | BSD-3 core, but links SuiteSparse (SPQR is GPL-2.0-or-later), METIS and OpenBLAS — see 5.2.1 |
| Anything with an unidentifiable licence | Cannot be audited |

*Note:* Epic's **MetaHuman DNA Calibration** tooling is partly Apache-2.0. Any use
requires verifying the specific repository and version at the time of use and
recording it here first.

### 5.2.1 Numerical libraries refused, decided 2026-09-09

Asked by the owner before directive 12 step 4 (the offline RBF compiler) needs a
linear solve. Every licence below was read from the copy installed on this
machine, or recorded as not installed. Eigen was the one cleared, and it is in
5.1.1 rather than here so the dependency gate reads it as allowed.

| Library | Licence, as verified | Why refused |
|---|---|---|
| **Ceres Solver** 2.2.0 (installed) | Core is BSD-3-Clause (`include/ceres/ceres.h`), but `otool -L lib/libceres.dylib` shows it linking **SuiteSparse** (`libcholmod`, `libspqr` — SPQR is GPL-2.0-or-later), plus METIS, OpenBLAS, glog, gflags and TBB. | Linking it would put a GPL-2.0-or-later library behind the Apache-2.0 modules, which rule 1 of section 4 forbids in that direction. It also solves a problem we do not have: nothing in the roadmap is non-linear — corrective fitting is linear least-squares and optimised centres of rotation is a similarity precompute. Revisit only with a real non-linear problem AND a SuiteSparse-free build. Recorded in 5.2 as forbidden **as currently built**. |
| **NLopt** (not installed) | Mixed MIT and LGPL-2.1. | No non-linear problem exists to solve. Not a licence refusal; if one ever appears, the MIT-licensed subset would need identifying first. |
| **Boost.Math** 1.90 (installed) | BSL-1.0, permissive, no obstacle at all. | Refused on **engineering** grounds only. The RBF kernels are `exp`, `sqrt` and `log`; it would save zero lines for a large dependency. |

**FFTW** is in the 5.2 forbidden table rather than here: it is GPL-2.0-or-later
without the paid MIT licence, so it is a licence refusal, and there is no FFT
anywhere in the roadmap in any case — wrinkle maps are texture blending, not
frequency-domain.

**The RBF runtime stays hand-written regardless of any of this**, and that is a
requirement rather than a preference: directive 12.5 fixes the per-vertex
accumulation order because "parallel scatter-add reorders float additions", and
a vectorised library reduction reorders float additions by design. A library
there would fight the determinism test.

**Why Eigen is worth it offline.** Thin-plate-spline interpolation matrices are
only *conditionally* positive definite, so plain Cholesky is the wrong
algorithm, and they become ill-conditioned when example poses cluster. A pivoted
`LDLT` or `ColPivHouseholderQR` is the difference between a solve that reports
trouble and one that silently returns something plausible and wrong.

### 5.2a Licence-cleared alternatives to SMPL-X (owner-supplied, 2026-09-07)

§5.2 says what we **cannot** use. The owner supplied three candidates for what
we can, and every claim below was verified against the primary source on
2026-09-07 rather than taken from the summary — one of them changes the answer.

**NVIDIA SOMA-X — the SMPL-X replacement. Usable, with one hard condition.**

| Thing | Licence | Verified at |
|---|---|---|
| Code | **Apache-2.0** | `github.com/NVlabs/SOMA-X` README, "SOMA-X is licensed under Apache-2.0" |
| Model weights | **Apache-2.0**, "This model is ready for commercial use" | `huggingface.co/nvidia/SOMA-X`, ungated, no extra agreement |

SOMA maps several parametric body models onto one canonical topology and
skeleton (77 joints) so a single LBS pipeline serves all of them. **That is
exactly where the trap is:** the identity backends are not equally licensed.

| Backend | Third-party licence needed |
|---|---|
| `SOMA` (native), `MHR` (default), `Anny`, `GarmentMeasurements` | **No** — clean |
| `SMPL`, `SMPL-H`, `SMPL-X`, `MANO` | **YES** — user-supplied licensed model files, which NVIDIA does not redistribute |

So **SOMA-X is usable only with the SOMA / MHR / Anny / GarmentMeasurements
backends.** Selecting an SMPL backend re-imports the research-only licence §5.2
forbids, through an Apache-2.0 front door. The README's own words: "Optional
third-party models and dependencies retain their own license terms." If SOMA-X is
ever wired in, the backend must be pinned in code and asserted by a test — a
runtime string that happens to default to `MHR` is not a licence guarantee.

**HSRD-100 — 3D human scans. Usable.**
`huggingface.co/datasets/digitalrealitylab/HSRD-100`, licence tag **`cc-by-4.0`**
("Creative Commons Attribution 4.0 International"). 246 GB, 100 poses across 10
subjects, OBJ plus PNG textures, ungated — no login or agreement. Commercial use,
modification and redistribution permitted **with attribution**, which means the
attribution goes in §6 the moment anything derived from it ships. The dataset
card also declares biometric identification, medical diagnosis and surveillance
out of scope; none of those are things we do.

**Quaternius — NOT verified, do not use yet.**
Reported as CC0. The site's licence page did not yield terms to either of the
two fetches attempted on 2026-09-07, and Quaternius has historically had both a
free and a patron tier. CC0 is *plausible* and unconfirmed, so by hard rule 6 it
stays out until someone reads the actual licence text and records it here. An
asset whose licence we assumed is the one that has to be torn out later.

**Also re-confirmed as forbidden**, matching §5.2 and the owner's own reading:
Human-M3 (data licence bars commercial products and services), the Texel body
scan dataset (MIT **code**, CC-BY-**NC** data — the split is the point), and
H3WB / Human3.6M (MIT repository, but the images stay under the Human3.6M
licence; a permissive wrapper does not launder the payload).

### 5.3 The Autodesk FBX SDK — why it is not used

The SDK **is** free of charge, and version 2020.3.9 is installed on the
development machine. The obstacle is not cost; it is that its licence and
AGPL-3.0 impose contradictory obligations on *distribution*. Read directly from
`/Applications/Autodesk/FBX SDK/2020.3.9/License.rtf`:

| Clause | Text (abridged) | Conflict |
|---|---|---|
| §2.1.5 | Licensee shall ensure its use of open-source software does not "cause the Software to be subject to any licensing terms other than those set forth in this Agreement" | AGPL-3.0 §5 requires exactly that — the whole combined work is licensed under AGPL |
| §21 | Defines "Open Source" by explicitly naming **GPL and LGPL** as licences requiring source disclosure, derivative-work rights, and free redistribution | The SDK's authors identified this exact incompatibility class by name |
| §10.2.1(g) | Permitted redistribution must be under an EULA that **prohibits** further redistribution of the component | AGPL-3.0 §6 requires the opposite: recipients must be free to redistribute |
| §2.1.1(e) | No right to "distribute … or otherwise provide all or any portion of the Autodesk Materials" except as the agreement allows | |

These attach on **distribution**. Building locally for yourself is unaffected;
shipping an AGPL binary linked against the SDK is not permissible.

**What is used instead: assimp (BSD-3-Clause).** It writes **binary FBX 7500
(FBX 2016)** — verified from the written bytes — which is *newer* than the
7300 (FBX 2013) the Python reference emits, and it carries no such restriction.
It also supplies Collada, STL and 3MF export plus the import capability the
reference lacks entirely.

**PLY is deliberately not offered.** assimp 6.0.4's PLY exporter writes corrupt
face indices whenever the mesh has UVs; reimporting segfaults, and assimp's own
validator rejects the file. Minimal repro and reasoning are recorded at the top
of `include/makehuman/io/SceneIO.h`.

### 5.4 Bundled runtime assets (`resources/`)

Not code dependencies — files shipped with the application. All permissive and
all compatible with both the AGPL and Apache-2.0 halves of this project.

| Asset | Version | Licence | Text |
|---|---|---|---|
| **Lucide** icons (57 SVG) | 1.37.0 | ISC | `resources/icons/lucide/LICENSE` |
| **42dot Sans** (variable) | google/fonts `ofl/42dotsans` | OFL-1.1 | `resources/fonts/OFL.txt` |

**ISC** is a permissive BSD-style licence: use, copy, modify and distribute with
the copyright notice retained. Compatible with AGPL-3.0 and Apache-2.0 alike.
The notice is kept verbatim, and each SVG retains its `@license lucide-static`
comment.

**OFL-1.1** permits bundling and redistribution, including commercially. Its two
real obligations, both met: the font is not sold on its own, and a **Reserved
Font Name** must not be reused — so a modified build of this font may not be
called "42dot Sans". We ship it unmodified, so this does not bite; anyone
subsetting or re-hinting it must rename.

The `stroke-width` normalisation applied to the Lucide SVGs (2 -> 1.5, per
`design.md` §4) is a modification, which ISC expressly permits.

**`resources/shaders/` is mixed, and the split is per FILE, not per directory.**
Check the SPDX header before moving or reusing any of them.

| File | Licence | Why |
|---|---|---|
| `litsphere.vert`, `litsphere.frag` | **AGPL-3.0-or-later** | Ports of the reference's `data/shaders/glsl/litsphere_*_shader.txt`. Derivative works; may never be moved into a permissive module. See §4. |
| `pbr.vert`, `pbr.frag` | **Apache-2.0** | Original work. The reference has **no PBR path at all** — it shades exclusively with the litsphere matcap — so there was nothing to translate. The equations are the published microfacet model (Cook-Torrance/GGX, height-correlated Smith, Schlick's Fresnel) as written up in Karis, *Real Shading in Unreal Engine 4* (2013) and Lagarde & de Rousiers, *Moving Frostbite to PBR* (2014). |

**Verified, not assumed** (2026-09-05): `legacy/python/data/shaders/glsl/`
holds twelve files — litsphere, normalmap, phong, skin, toon and xray, vertex
and fragment each. A case-insensitive grep across all of them for
`pbr|metallic|roughness|cook|ggx|microfacet` matches **nothing**. There is no
PBR shader in the reference to derive from.

`pbr.*` shares a uniform block layout and a vertex attribute layout with
`litsphere.*` so one `SceneResources` can serve both. **Layout compatibility is
not derivation**: the layouts are dictated by our own C++ (`SceneResources.cpp`,
Apache-2.0) and by the vertex data we build, not by anything in the reference.
Loading an AGPL `.qsb` and an Apache-2.0 `.qsb` in the same process is the same
runtime pairing §4 already covers.

### 5.5 Mixamo reference rigs (`references/human_based_fbx_mixamo_animations/`)

Seven FBX animation clips from Adobe Mixamo, 2.3 MB, added by the project owner
**at their explicit direction** on 2026-08-29 to serve as the reference for
bone naming and ordering (see `memory/todo.md` M5) and as import test input.

**Basis:** Mixamo's terms grant a royalty-free licence to use its characters and
animations in commercial and non-commercial projects, with no attribution
required. The project owner has stated this covers our use and instructed that
they be committed.

**Recorded caveat, not a blocker:** Mixamo's terms are aimed at *using* the
animations in a project. Redistributing the raw `.fbx` files as standalone
downloadable assets is a different act from shipping a work that incorporates
them, and Adobe's terms are not explicit about a public source repository. These
files are therefore:

- kept in `references/`, **outside** `data/`, so they are never confused with
  the CC0 asset set and are not packaged into a release build;
- used as **test input and naming reference only** — no Mixamo geometry, skin
  weights or animation data is copied into `data/` or into any shipped artefact;
- excluded from the CC0 claim in `LICENSE.ASSETS.md`.

Anyone redistributing this repository commercially should satisfy themselves
about Mixamo's terms for the `references/` directory specifically, or delete it —
nothing in the build depends on it.

**This is not a code dependency.** No target links or reads these files; the
`references/` tree is not referenced by any `CMakeLists.txt`.

## 6. Obligations we must meet when distributing

- [ ] Ship the complete corresponding source, or a written offer (AGPL-3.0 §6).
- [ ] Ship `LICENSE.md`, `LICENSE.CODE.md`, `LICENSE.ASSETS.md`, and this file inside the bundle.
- [ ] State prominently that the software is AGPL-3.0 and where source is obtained.
- [ ] Ship the LGPL relinking notice for Qt; link Qt dynamically.
- [ ] Preserve all copyright notices and attributions.
- [ ] If offered as a network service: offer source to users of that service (§13).
- [ ] Generate an SBOM (CycloneDX) as a build artefact.

## 7. Open licence questions

1. **Typeface.** The instruction was *"red 42 dot sans"*. **42dot Sans**
   (SIL OFL 1.1, Google Fonts) is the assumed match; the leading "red" is
   unexplained. Confirm the family, then verify and record its licence here
   before bundling. Fonts are bundled, so this is a redistribution question.
2. **Training data** for Objective O6 (character generation). Every dataset must
   be licence-audited and recorded here **before** any use. A model trained on
   licence-incompatible data contaminates the output.
3. **Community assets** downloaded at runtime carry per-asset licences from the
   server as free text, with no validation and nothing written into the installed
   file (`legacy/python/plugins/8_asset_downloader/remoteasset.py:97`). The port
   should persist the declared licence alongside each installed asset.

## 8. Adding a dependency — required procedure

1. Check it against §5.2. If forbidden, stop.
2. Identify the exact licence and SPDX identifier from the source repository.
3. Confirm compatibility with AGPL-3.0 distribution.
4. Add a row to §5.1 with name, version, licence, SPDX, and purpose.
5. Pin an exact tag or commit SHA in CMake — never a moving branch.
6. Add its licence text to `third_party/licenses/`.
7. Confirm the CI licence scan passes.

**A dependency not listed in §5.1 fails CI.**
