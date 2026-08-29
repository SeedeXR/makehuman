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
| UI images, icons, themes, GLSL shaders | **AGPL-3.0** | `LICENSE.md` §B explicitly includes "glsl shaders" and UI images |
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

## 5. Dependencies

### 5.1 Current (verified present on the build machine)

| Dependency | Version | Licence | SPDX | Use |
|---|---|---|---|---|
| Qt | 6.11.0 | LGPL-3.0 (dynamic) / GPL-3.0 | `LGPL-3.0-only` | UI, RHI, shader tools |
| Eigen | 5.0.1 | MPL-2.0 | `MPL-2.0` | Linear algebra |
| oneTBB | 2023.1.0 | Apache-2.0 | `Apache-2.0` | Job system |
| assimp | 6.0.4_1 | BSD-3-Clause | `BSD-3-Clause` | **Multi-format import/export** (FBX 7500 binary, Collada, STL, 3MF) and an independent reader validating the formats we write ourselves. Linked into `mh_io`. See §5.3. |
| Catch2 | v3 (planned) | BSL-1.0 | `BSL-1.0` | Tests |
| Lucide | latest (planned) | ISC | `ISC` | Icons |
| 42dot Sans | (planned, **unconfirmed**) | SIL OFL 1.1 | `OFL-1.1` | Typeface — see §7 |

All are compatible with AGPL-3.0 distribution.

**Qt/LGPL obligation:** Qt must be **dynamically linked**, and the distribution
must permit relinking against a modified Qt. The macOS bundle ships Qt as
frameworks and includes this notice. Static Qt would require the commercial
licence or full GPL relinking provisions.

### 5.2 Forbidden

| Item | Reason |
|---|---|
| **Autodesk FBX SDK** | See §5.3 — free of charge, but its EULA cannot be combined with AGPL *distribution* |
| **Epic MetaHuman** assets, SDK, DNA, or exported data | EULA restricts use to the Unreal ecosystem |
| GPL-2.0-**only** code | Incompatible with AGPL-3.0 |
| Anything non-commercial or field-of-use restricted | Conflicts with the freedoms we promise |
| Anything with an unidentifiable licence | Cannot be audited |

*Note:* Epic's **MetaHuman DNA Calibration** tooling is partly Apache-2.0. Any use
requires verifying the specific repository and version at the time of use and
recording it here first.

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

### 5.4 Mixamo reference rigs (`references/human_based_fbx_mixamo_animations/`)

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
