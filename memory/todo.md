# Roadmap and Execution Tracker

**Version:** 1.0 · **Created:** 2026-08-29 · **Last updated:** 2026-08-29 04:15:00

Legend: `[ ]` open · `[x]` done · `[~]` in progress · `[!]` blocked ·
~~struck~~ = changed, with the reason stated.

---

## M0 — Grounding and scaffolding ✅ COMPLETE

- [x] Build knowledge graph of the codebase (graphify) — 5,392 nodes / 9,319 edges / 358 communities
- [x] Deep subsystem analysis: mesh, modifiers, rig, renderer, GUI, export, assets
- [x] Verify all subagent findings against source (`file:line` spot-checks)
- [x] Establish repo structure (`src/ include/ tests/ benchmarks/ tools/ resources/ memory/ legacy/python/ data/`)
- [x] Preserve the Python reference as a runnable oracle (`legacy/python/`, `data/` symlink)
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
- [x] Face visibility mask + filtered index buffer. `RenderMesh::setFaceMask`
      rebuilds only the index and draw ranges, never the unweld: **0.21 ms vs
      2.19 ms** for a full build, and 8.7x the reference's 1.85 ms. Draw ranges
      match the reference's `grpix` exactly, group by group, for all four
      captured masks.
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

## M3 — Targets and modifiers (`mh-core`)  ✅ core complete

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
- [x] `applyStack` — reset to morph base + replay, matching `applyAllTargets`
- [x] **End-to-end parity: 14 characters**, modifier values → macro factors →
      target weights → applied targets → final vertex positions, all matching
      the reference within 1e-5 across 19,158 vertices
- [ ] Incremental stack application (apply only the delta on a slider drag)
- [x] `.mhm` saved-model parser + `applyMhm`. **Round-trip parity**: a real
      `.mhm` written by the reference, loaded in C++ and applied, reproduces the
      geometry the reference itself produces from that file. Unrecognised lines
      (skeleton / pose / proxy / material) are preserved verbatim for M4-M5.
- [ ] **Parity fixtures**: default stack, extreme macro combinations, every modifier at ±1
- [ ] `data/modifiers/*.json` loader

## RESOLVED — the licence boundary (2026-08-29)

`mh_io` was stamped Apache-2.0 while `PUBLIC`-linking `mh::core` (AGPL) and
calling `core::RenderMesh::build`, a port of `module3d.py`. Its Apache stamp
bought nobody anything: linking io pulled in AGPL regardless.

**Fixed.** `mh_io` now links `mh::foundation` and nothing else of ours.
Verified, not asserted: `nm -u libmh_io.a | c++filt | grep mh::core` returns
**0 undefined symbols**, and CI gates both the CMake link and any
`makehuman/core/` include from a permissive module.

The audit determined what could and could not move:

| Type | Citations | Outcome |
|---|---|---|
| `Types.h` | 3 (facts, not expression) | moved to `mh_foundation` |
| `Mesh` | 14 + 9 | stayed AGPL; exposes `view()` |
| `Material` | 7 + 12 | stayed AGPL; exposes `desc()` |

The bridge is `foundation::MeshView` / `RenderView` / `MeshData` /
`MaterialDesc` — **data, no behaviour**. `core` produces them, `io` consumes
them, so the dependency runs AGPL -> Apache, which is legal.

The unweld moved OUT of the writers: glTF and FBX now take a `RenderView` the
caller has already built. That makes the cost visible at the call site instead
of hidden in a writer, and is what actually removed the last AGPL call from io.

- [ ] `mh_rig`, `mh_asset`, `mh_render`, `mh_ui` must respect the same rule as
      they are created. The CI gate covers `io` and `foundation`; extend its
      module list rather than trusting review.

## M4 — Proxies, materials, assets (`mh-asset`)

- [x] `.mhclo`/`.proxy` parser incl. `TMatrix` scale
- [x] Barycentric fit `P = Σ w_k·H[v_k] + M·d` — **parity on 3 proxies × 2 bodies**,
      including a reshaped body so the `TMatrix` rescaling is actually exercised
- [ ] `TMatrix` shear forms (`shear_*`, `l_shear_*`, `r_shear_*`) — no shipped
      asset uses them; the three eye/converter proxies are all scale-only
- [x] Delete-vert mask -> face hiding. `visibleVertexMask` unions the
      `delete_verts` of the worn proxies; `Mesh::faceMaskForVisibleVertices`
      turns that into a face mask. **A face survives if ANY corner is visible**
      -- pinned by the `stride` fixture, where hiding 2,737 of 19,158 vertices
      hides zero of 18,486 faces. No shipped asset declares `delete_verts`
      (0 across all four), so the fixtures are synthetic but run through the
      reference's own code.
- [ ] Proxy-on-proxy masking (`transferVertexMaskToProxy`) — clothes hiding
      clothes. Needs the render-order stack; the body mask does not.
- [x] `.mhmat` **parser** — all keys, 7 texture channels, shader config
- [x] `.mhmat` **writer** — lossless round-trip on all 3 shipped materials,
      plus a fixed-point check (a second save must reproduce the first).
      Verified in **both** directions: `tools/verify_material_roundtrip.py`
      confirms the Python reference reads our output field for field.
      That cross-check earned its keep immediately — it caught the writer
      emitting `diffusetexture`, which our own case-insensitive reader
      round-trips happily and MakeHuman 1.x silently ignores.
- [x] Asset index (`AssetIndex`) — replaces the pickle `filecache`, which was an
      RCE vector. Metadata is *peeked*: the scan stops at the `verts` line.
- [x] UUID→path resolution. `.mhm` references proxies by UUID **only**
      (`proxychooser.py:550-552` refuses filenames), so this is what makes a
      saved character's clothes resolve. Search paths are ordered, earlier wins,
      and collisions are **reported** rather than silently last-write-wins.
- [ ] macOS path resolution (`~/Library/Application Support`, bundle Resources)
- [ ] **Parity fixtures**: eye proxy fit at several body shapes

## M5 — Rig and skinning (`mh-rig`)

- [ ] **Bone naming/order: Mixamo standard** (owner decision, 2026-08-29).
      Measured and documented in `docs/rig/mixamo_bone_order.md` — 65 bones,
      `mixamorig:` prefix, identical across all 7 reference clips.
      Watch the `$AssimpFbx$` decomposition trap documented there: a naive
      assimp node walk gives ~190 nodes and wrong parents.
- [ ] MakeHuman(163) -> Mixamo(65) retarget map. Lossy by construction; needs
      an explicit table, not a name-matching heuristic.
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
- [~] Shader port to `.qsb`: **litsphere done** (`resources/shaders/rhi/`),
      compiles to SPIR-V + GLSL 450 + MSL 12 via `tools/compile_shaders.sh`;
      the `0.495` constant verified present in the generated Metal.
      Remaining: phong, normalmap, skin, toon, xray (sources stay in
      `data/shaders/glsl/`; copying them into resources/ would just drift).
- [ ] Wire `qt6_add_shaders()` into CMake — deferred until Qt is a real build
      dependency, so CI does not pay a 5-minute Qt install for shaders nothing
      renders yet.
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
- [x] **Export glTF 2.0 / GLB** — from spec, single self-contained `.glb`,
      metallic-roughness material, correct metre units, V flipped for glTF's
      top-left UV origin. Validated by spec conformance **and by assimp**, an
      independent implementation.
- [ ] glTF: skins, blendshapes (morph targets), embedded textures, Draco
- [ ] **Export** FBX 7.4/7.5 — from spec, **not** by translating the GPL Blender code
- [ ] **Export** USD / USDZ
- [x] **Export OBJ** (`mh::io`, Apache-2.0) — face lines byte-identical to the
      reference across all 18,486 faces; unit conversion; feet-on-ground computed
      from the mesh minimum rather than the reference's Y-only joint offset;
      face masking; `.mtl` sidecar
- [ ] Export STL, DAE, BVH
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
- [x] **Assets in place**: 57 Lucide icons (ISC, normalised to the 1.5 px
      stroke `design.md` §4 specifies) and 42dot Sans variable (OFL-1.1,
      structurally validated). See `resources/README.md`.
- [ ] Dark theme from the token table, wired to the token file
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
      rigs — `legacy/python/apps/compat.py:181-188` references `opensim.mhskel`
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
