# Roadmap and Execution Tracker

**Version:** 1.0 · **Created:** 2026-08-29 · **Last updated:** 2026-08-29 04:15:00

Legend: `[ ]` open · `[x]` done · `[~]` in progress · `[!]` blocked ·
~~struck~~ = changed, with the reason stated.

---

## M0 — Grounding and scaffolding ✅ COMPLETE

- [x] Build knowledge graph of the codebase (graphify) — 5,392 nodes / 9,319 edges / 358 communities
- [x] Deep subsystem analysis: mesh, modifiers, rig, renderer, GUI, export, assets
- [x] Verify all subagent findings against source (`file:line` spot-checks)
- [x] Establish repo structure (`src/ include/ tests/ benchmarks/ tools/ resources/ memory/ legacy-python/ data/`)
- [x] Preserve the Python reference as a runnable oracle (`legacy-python/`, `data/` symlink)
- [x] Stand up a reference venv (numpy 2.5.1 + PyQt5 + PyOpenGL on Python 3.14.6)
- [x] Measure the performance baseline → `benchmarks/baseline_python.json`
- [x] Verify licence position — AGPL-3.0 code / CC0 assets; pyFBX is GPL-2.0-**or-later**, so compatible
- [x] Write `memory/` — 12 documents
- [x] `CLAUDE.md`, `AGENT.md`, `LICENSING.md`

## M1 — Build system and core skeleton  ✅ COMPLETE

- [x] `CMakeLists.txt` + `CMakePresets.json` (macos-arm64 debug/release/asan)
- [x] Catch2 v3.7.1 via FetchContent (pinned tag); `ctest` wired; Catch2 headers marked SYSTEM
- [x] `-Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion … -Werror` — builds clean
- [x] ASan+UBSan preset; full suite green under it
- [x] First module target `mh_core` (`mh::core`)
- [x] Benchmark target comparing directly against `baseline_python.json`
- ~~Module targets for `mh-foundation`, `mh-rig`, `mh-io`, `mh-render`, `mh-ui`, `mh-app`~~
  → **deferred**: creating empty targets before they have sources is scaffolding for
  its own sake. Each is added in its own milestone when it gets a first source file.
- [x] `.clang-format` + `.clang-tidy`; all sources formatted and compliant
- [x] CI workflow: format · build+test (debug/release/asan) · benchmark · licence gates
      (SPDX header check, FetchContent deps recorded in `LICENSING.md`, forbidden-dep scan)
- [x] `tools/capture_fixture.py` — captures mesh / targets / skeleton fixtures with a
      MANIFEST recording reference commit, interpreter and numpy version

## M2 — Core geometry (`mh-core`)  [~ in progress]

- [x] `Mesh` — SoA layout, dual index space preserved, degenerate-quad triangles
- [x] OBJ reader — `v/vt/f/g/o`, negative indices, leading-dot floats, `vn`/`usemtl` ignored
- [x] Bounds-checked parsing: bad index, n-gon, loose vertex, empty mesh all return errors
- [x] Adjacency build (`vface`/`nfaces`), valence floored at 4 — **matches the reference's 5**
- [x] `calcFaceNormals` / `calcVertexNormals` — area-weighted, zero-guarded
- [x] **Parity fixtures**: base mesh counts, face groups, units, normals, index ranges
- [x] **Byte-level parity vs. the Python reference**: coord, fvert, fuvs, texco, vnorm,
      group — all 19,158 verts and 18,486 faces match within stated tolerances
- [x] Benchmarks vs. the Python baseline
- [x] **Correct** tangents — Lengyel's method, Gram-Schmidt, handedness in `w`.
      Reference has **three** independent bugs (`module3d.py:411` chained assignment,
      `:429` missing `axis=`, and — unlike `calcVertexNormals` at `:366` — no `nfaces`
      mask, so face 0 folds into every low-valence vertex). Tested by mathematical
      property, not parity: all 19,158 base-mesh tangents finite, unit, orthogonal.
- [x] Unweld / index-buffer build → `RenderMesh` (`vmap`/`tmap`/group ranges),
      matching the reference's `np.unique` ordering. Base mesh: 19,158 → **21,833**
      render verts (UV seams), 110,916 triangle indices, 139 draw ranges.
      Quads triangulated (Metal has no `GL_QUADS`); degenerate quads emit one triangle.
- [ ] Face visibility mask + filtered index buffer (arrives with proxy hiding, M4)
- [ ] `RenderMesh::build` is 2.11 ms — dominated by an indirect-comparator sort.
      Sorting packed (key,corner) pairs directly would help. Load-once path, low priority.
- [x] Exact staleness detection: `Mesh::topologyVersion()`, recorded by
      `RenderMesh` and `Subdivider`. Replaces count-comparison, which missed a
      same-size topology swap and silently produced wrong geometry.
- [ ] Dirty-range tracking replacing `ucoor`/`unorm`/… (partial buffer updates)
- [x] Catmull-Clark subdivision (`Subdivider`) — **exact parity: 75,008 verts /
      73,944 faces / 37,364 edges**, matching the reference. Split into a topology
      pass and a geometry pass so a morph costs only the geometry.
      **Resolved the `maxpole` caveat by not needing it:** the reference sizes one
      `vface` array to `max(maxFaces, maxpole, 4)` because it reuses that array for
      both faces and edges. We keep separate `vface`/`nfaces` (faces) and
      `vedge_`/`nedges_` (edges) arrays, each sized from its own measured maximum,
      so there is no shared bound to over-allocate.
- [ ] Subdivision with a face mask (hidden geometry never generated) — with M4
- [ ] Heterogeneous lookup in `findFaceGroup` (currently allocates a `std::string`)

## M3 — Targets and modifiers (`mh-core`)

- [x] `.target` ASCII parser — **all 1,280 shipped targets parse**, 0 failures,
      0 malformed lines, 6,147,800 sparse entries, max index 19,157 (= nVerts-1)
- [x] `applyTarget` — sparse additive `coord[v] += offset * (scale * factor)`,
      out-of-range indices skipped (the reference reads unguarded)
- [x] `TargetLibrary` — path-keyed session cache
- [x] **Byte-level parity**: parsed indices/offsets for 24 sampled targets, and the
      applied 24-target stack vs. the reference's result across all 19,158 verts
- [ ] Compiled target blob (`tools/mhassetc`), uint32 indices, mmapped —
      target ≤50 ms for all 1,280. **Now clearly justified**: ASCII parse of the
      full set is 465 ms in C++ (vs 3,226 ms in Python), still 9x over budget.
- [ ] Target index: filename tokenisation on `-`, `_`, `.` against the 9-category table
- [x] All macro factor formulas (`MacroFactors`), verbatim from the reference,
      **920 assertions of parity** across 34 parameter combinations x 27 values
- [x] Ethnic renormalisation incl. all three degenerate branches
- [x] Macro value table: **27 values / 9 categories** (an earlier note here said
      26 — wrong; `age` has four values, not three)
- [x] Modifier hierarchy (Universal / Macro / Ethnic) + `*_modifiers.json` loader.
      **Exact parity on all 291 shipped modifiers**: full name, range, kind and
      per-side target groups.
- [x] `Human` — macro scalars, slider values, and the weighted target stack
      (zero weights never stored, as `human.py:918-921` requires)
- [x] Target index (`TargetIndex`): filename tokenisation on `-`, `_`, `.` into
      group key + macro dependencies. **Exact parity: 653 groups / 1,280
      components**, every group name and size matching the reference.
- [x] `targetWeight` = `value x groupFactor x PROD(macro factors)`
      (`humanmodifier.py:644-652`)
- [ ] `weight = value × Π factors`
- [ ] `applyStack` — incremental, not full-reset-and-replay
- [ ] End-to-end `.mhm` round trip: load a real model file and compare final
      vertex positions against the reference
- [ ] **Parity fixtures**: default stack, extreme macro combinations, every modifier at ±1
- [ ] `data/modifiers/*.json` loader

## M4 — Proxies, materials, assets (`mh-asset`)

- [ ] `.mhclo`/`.proxy` parser incl. `TMatrix` scale/shear
- [ ] Barycentric fit `P = Σ w_k·H[v_k] + M·d`
- [ ] Delete-vert mask → face hiding
- [ ] `.mhmat` parser/writer — all keys, 7 texture channels, shader config
- [ ] Asset index replacing the pickle `filecache` (pickle is an RCE vector)
- [ ] UUID→path resolution (`.mhm` references proxies by UUID only)
- [ ] macOS path resolution (`~/Library/Application Support`, bundle Resources)
- [ ] **Parity fixtures**: eye proxy fit at several body shapes

## M5 — Rig and skinning (`mh-rig`)

- [ ] `.mhskel` parser — 163 bones, breadth-first ordering, deadlock-guarded
- [ ] Bone rest matrices; `matPoseVerts = matPoseGlobal · inv(matRestGlobal)`
- [ ] Joint positions from vertex clouds (rig follows the mesh)
- [ ] `.mhw` weights; normalisation; influence clamping
- [ ] Euler conventions — all 24, `[w,x,y,z]` quaternions (Eigen `.coeffs()` is `[x,y,z,w]`)
- [ ] CPU LBS (reference parity) **and** GPU LBS (production)
- [ ] Pose units, `.mhpose`, slerp-composition blend (order-dependent — replicate)
- [ ] `mixPoses` for face/foot layering
- [ ] BVH import/export
- [ ] **Parity fixtures**: rest matrices, skinned positions, blended expression

## M6 — Renderer (`mh-render`)

- [ ] Qt RHI device + swapchain, Metal backend, MSAA **on**
- [ ] Persistent vertex/index buffers; partial updates from dirty ranges
- [ ] Quad→triangle conversion at buffer-build time
- [ ] Shader port to `.qsb`: litsphere, phong, normalmap, skin, toon, xray
- [ ] Litsphere/matcap **pixel-faithful** (incl. `0.495` scale, `2.0 − mean` term)
- [ ] Eye-space-fixed light (model rotates, camera does not)
- [ ] Depth range `[0,1]` — rework `perspective`/`ortho`, do not transliterate
- [ ] PBR metallic-roughness path + Blinn-Phong→PBR conversion
- [ ] GPU skinning (matrix palette UBO/SSBO)
- [ ] ID-buffer picking with async readback (replaces the full-window sync readback)
- [ ] Cached bounding box
- [ ] Offscreen render + alpha mask for production render
- [ ] **60 fps on the subdivided mesh** — the headline metric

## M7 — Interchange (`mh-io`)

- [ ] `mh::io::Scene` intermediate representation
- [ ] One `UnitSystem`/`Transform` consumed by **every** reader and writer
- [ ] **Import**: OBJ, glTF/GLB, FBX, USD, STL, DAE (assimp-backed where sensible)
- [ ] **Export** glTF 2.0 / GLB — from spec; PBR, skins, blendshapes, embedded buffers
- [ ] **Export** FBX 7.4/7.5 — from spec, **not** by translating the GPL Blender code
- [ ] **Export** USD / USDZ
- [ ] Export OBJ, STL, DAE, BVH (parity with the reference, minus its bugs)
- [ ] Skin weights: sorted, normalised, clamped to 4 (glTF requirement)
- [ ] **Blendshape export** — dead in the reference everywhere; must actually work
- [ ] Texture packing (ORM), GLB embedding, KTX2/Basis, optional Draco
- [ ] Unit-correctness tests at dm/m/cm/inch (the reference is 10× wrong except dm)
- [ ] `docs/formats/*.md` for all seven MakeHuman formats
- [ ] Round-trip + malformed-input tests for every format

## M8 — Application shell (`mh-app`, `mh-ui`)

- [ ] `QMainWindow` + `QDockWidget`, nested and tabbed docking
- [ ] Snapping with drop indicators
- [ ] Six-dot panel menu (float/dock/tab/reset/close) — `design.md` §6.3
- [ ] Workspaces: save / load / reset, 4 shipped presets, versioned schema
- [ ] Dark theme from the token table; Lucide icons; 42dot Sans
- [ ] `QRhiWidget` viewport with the documented navigation bindings
- [ ] Task registry replacing filename-sort plugin ordering
- [ ] Symbolic shortcut/mouse persistence (not raw Qt enum ints)
- [ ] Port the 50 task views (tracked as a sub-checklist — `architecture.md` §I.8)
- [ ] Undo/redo
- [ ] Live language switching; **working** RTL
- [ ] Accessibility pass: VoiceOver, keyboard-only, contrast, 200% text

## M9 — MetaHuman-class character tooling

- [ ] FACS-based facial rig extending the 60 existing pose units
- [ ] **Translation-capable pose blending** (reference drops translations, so jaw
      slide and lip pursing are currently inexpressible)
- [ ] Pose-space deformation / correctives (does not exist in the reference)
- [ ] LOD chain generation with weight and UV transfer
- [ ] Groom / hair card and strand support
- [ ] Physically-based skin: SSS, multi-layer, tension maps
- [ ] Eye, teeth, tongue rigging refinement
- [ ] Wrinkle/detail normal blending driven by expression

## M10 — Data-driven character generation

- [ ] Headless deterministic `parameters → mesh` API (no Qt in `mh-core`)
- [ ] Parameter-space definition and sampling
- [ ] **Licence audit of every candidate dataset before any use** — record in `LICENSING.md`
- [ ] Generative model over the modifier vector
- [ ] Image/scan → parameters fitting
- [ ] Guardrails: no MetaHuman-derived data, ever (`project_context.md` §4.3)

## M11 — Packaging and release

- [ ] `MakeHuman.app` bundle, `Info.plist`, Resources
- [ ] `macdeployqt` + CMake install
- [ ] Codesign, hardened runtime, notarize, staple
- [ ] DMG with background and layout
- [ ] Bundle `LICENSING.md` + LGPL relinking notice + AGPL source offer
- [ ] Universal binary (arm64 + x86_64)
- [ ] Auto-update channel
- [ ] Migration guide for existing `.mhm` users

---

## Research and open questions

- [ ] **Confirm the typeface.** Instruction was "red 42 dot sans"; `42dot Sans`
      (SIL OFL 1.1) is the assumed match. Needs a one-word confirmation.
- [ ] **Clarify "open rig".** The instruction mentioned integrating "open rig".
      Candidate readings: (a) the existing open `.mhskel` rig format, (b) OpenSim
      rigs — `legacy-python/apps/compat.py:181-188` references `opensim.mhskel`
      as a downloadable community asset, (c) a specific third-party project.
      **Not guessing.** Blocked on clarification; does not block M1–M8.
- [ ] Evaluate MetaHuman **DNA Calibration** (parts are Apache-2.0) — licence-verify
      the exact repo and version before any use.
- [ ] Decide runtime data location on macOS (bundle Resources vs Application Support)
- [ ] Decide Widgets vs QML for panel content (leaning Widgets)
- [ ] Survey current research: neural morphable body models, PSD/corrective learning,
      differentiable skinning, LOD auto-generation

## Known reference defects to fix rather than port

Tracked in full in `project_context.md` §8. Each becomes a regression test.

- [ ] Tangent computation (3 separate bugs, `core/module3d.py:411,429,1212`)
- [ ] FBX 10× unit error at every scale but decimetre
- [ ] FBX forged Creator string and fixed fake FileId
- [ ] Collada morph controller `NameError`
- [ ] `.mhscene` is a Python pickle → replace with JSON (RCE vector)
- [ ] `AnimationTrack.sparsify` assigns to a read-only property
- [ ] `.mhp` pose loader always logs an error
- [ ] Bare `except: pass` swallowing export failures
