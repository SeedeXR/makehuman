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

## M2 — Core geometry (`mh-core`)  ✅ COMPLETE

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
- [x] **`RenderMesh::build` 2.23 -> 1.56 ms** (1.5x -> 2.2x vs Python). Three
      approaches measured, not guessed: indirect comparator 2.23, sorting
      (key, index) pairs 2.04, LSD radix sort **1.56**. The win is memory
      behaviour — six sequential passes beat 1.26M cache-missing comparisons.
      Numbers recorded in the code.
      **Mutation testing found a hole in the suite**: a radix sort skipping its
      top byte passed *every* existing test. They checked render vertices were
      valid, never that they were in `np.unique` order — which every downstream
      buffer is indexed by. Now pinned (`[rendermesh][order]`): the broken sort
      leaves 4,469 vertices out of order.
      Sorting packed (key,corner) pairs directly would help. Load-once path, low priority.
- [x] Exact staleness detection: `Mesh::topologyVersion()`, recorded by
      `RenderMesh` and `Subdivider`. Replaces count-comparison, which missed a
      same-size topology swap and silently produced wrong geometry.
- [x] **Dirty-range tracking: measured, deliberately NOT built.**
      Two independent reasons, both observed:
      1. **There is no headroom to win.** The 60 fps target means a 16.7 ms
         frame. The interactive mesh is the *subdivided* one (smoothing is on
         by default), and a morph there costs `Subdivider::refresh` 0.48 ms +
         `RenderMesh::refreshPositions` **0.11 ms** — the gather is 0.7% of the
         frame budget. (Medians of 5 runs. A single cold run after linking read
         0.12 ms for the *base* case against a 0.04 ms median, so single reads at
         this resolution are noise; every figure here is a median.) Benchmarked, not assumed: the subdivided case was
         unmeasured until this session, and it is the only case that mattered.
      2. **The reference's partial path is a pessimisation.** `module3d.py:880`
         reads
         `r_coord[ucoor[vmap]] = coord[vmap][ucoor[vmap]]`.
         `coord[vmap]` is fancy indexing — it materialises the **full** gather
         first, then masks it. The "partial" update does all the work of the
         full copy plus a mask allocation. Porting it would be slower.
      The cost side is real: a dirty bitmask threaded through every mutation
      site (`applyTarget`, `fitProxy`, skinning, `resetToOriginal`), where one
      missed mark silently renders stale geometry. Paying that for 0.6% of a
      frame, to copy a design that is slower than what we already do, is a bad
      trade. **Reopen if** the profile ever shows the gather above ~2 ms.
- [x] Catmull-Clark subdivision (`Subdivider`) — **exact parity: 75,008 verts /
      73,944 faces / 37,364 edges**, matching the reference. Split into a topology
      pass and a geometry pass so a morph costs only the geometry.
      **Resolved the `maxpole` caveat by not needing it:** the reference sizes one
      `vface` array to `max(maxFaces, maxpole, 4)` because it reuses that array for
      both faces and edges. We keep separate `vface`/`nfaces` (faces) and
      `vedge_`/`nedges_` (edges) arrays, each sized from its own measured maximum,
      so there is no shared bound to over-allocate.
- [x] **Masked subdivision** — `Subdivider::build(parent, faceMask)`. This was
      filed as an optimisation and is really a **correctness gap**: the app path
      in the reference (`guicommon.py:433`) passes `staticFaceMask` into
      `createSubdivisionObject`, whose docstring says masked faces "are not
      included as geometry". We subdivided all 18,486 faces; the reference
      subdivides 13,378.
      **The old fixture never caught it** because it was captured with
      `createSubdivisionObject(mesh, None)` — it tested a branch the app does
      not take. New fixture `tests/golden/subdiv_masked/` (53,512 faces) matches
      byte-for-byte.
      Implemented by compacting to visible faces and running the *existing*
      verified algorithm, mirroring the reference's `face_map`/`vtx_map`.
      **6.97 -> 5.25 ms** on the app's real path.
- [x] **`findFaceGroup` heterogeneous lookup: NOT built, by the same rule.**
      `findFaceGroup` has **zero production callers** — it appears only in its
      own definition (`src/core/Mesh.cpp:119`), its declaration, and one unit
      test. The `std::string{name}` it allocates is on a path nothing takes, so
      the transparent-hasher machinery would buy a measured nothing, and no
      test could distinguish it (both spellings pass every input identically).
      A change with no measurable effect and no test that can fail is not an
      improvement. The API stays as-is; add transparent lookup the day a real
      caller appears on a hot path.

## M3 — Targets and modifiers (`mh-core`)  ✅ core complete

- [x] `.target` ASCII parser — **all 1,280 shipped targets parse**, 0 failures,
      0 malformed lines, 6,147,800 sparse entries, max index 19,157 (= nVerts-1)
- [x] `applyTarget` — sparse additive `coord[v] += offset * (scale * factor)`,
      out-of-range indices skipped (the reference reads unguarded)
- [x] `TargetLibrary` — path-keyed session cache
- [x] **Byte-level parity**: parsed indices/offsets for 24 sampled targets, and the
      applied 24-target stack vs. the reference's result across all 19,158 verts
- [x] **Target loading hit the ≤50 ms goal WITHOUT the compiled blob.**
      The old justification here — "ASCII parse of all 1,280 is 465 ms" — cited a
      path **nothing takes**. Targets load lazily (`src/app/main.cpp:687`). The
      default character touches **8** targets; driving all 291 modifiers, the
      worst a single character can reach is **364** (28% of the set), measured.
      Where the 196 ms actually went, for that worst-case character:
      read-only, no parsing **158 ms** (364 separate `open()`s) — so it is
      dominated by per-file I/O, not by `strtof`.
      The files are independent, so `TargetLibrary::prewarm()` loads them
      concurrently: **196 ms -> ~20 ms** end-to-end, and warm-to-warm the parse
      is **98.1 -> 20.0 ms** (4.9x, 10 threads). Benchmarked at 6.09 ms for 364.
      `Human::applyStack` prewarms the cold part of its own stack, so opening a
      saved `.mhm` gets this for free; when everything is cached the scan
      allocates nothing.
      **The blob is therefore not built.** It would need a tool, a binary
      format, versioning, a staleness gate and a CI job to beat a number that is
      already under budget. **Reopen if** cold-start target loading is ever
      measured above ~50 ms — the blob's real advantage is turning 364 `open()`s
      into one `mmap`, which is the 158 ms term, and that term is unmeasured on
      a genuinely cold page cache (no way to purge it here without sudo).
- [x] **ThreadSanitizer is now a build preset and a CI job.** This is the
      project's first concurrent code, and **ASan does not detect data races**.
      `macos-arm64-tsan` + `MH_ENABLE_TSAN`. Verified the gate is not theatre:
      the TSan runtime is linked (`otool -L`), and writing to the shared cache
      from the worker threads is reported as a race **42 times**.
- [x] Target index: filename tokenisation on `-`, `_`, `.` against the 9-category table
      — `TargetIndex.cpp:9-15`, cited to `targets.py:203`; 4 parity tests.
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
- [x] `weight = value × Π factors` — `TargetIndex.cpp:125-132`, cited to
      `humanmodifier.py:644-652`.
- [x] `applyStack` — reset to morph base + replay, matching `applyAllTargets`
- [x] **End-to-end parity: 14 characters**, modifier values → macro factors →
      target weights → applied targets → final vertex positions, all matching
      the reference within 1e-5 across 19,158 vertices
- [x] **Incremental stack application: measured, deliberately NOT built.**
      Same rule, same session's measurements. The whole slider-drag path on the
      subdivided mesh is `Human::rebuildStack` 0.01 + `Human::applyStack` 0.07 +
      `Subdivider::refresh` 0.48 + `RenderMesh::refreshPositions` 0.11 =
      **0.67 ms** of a 16.7 ms frame. Applying only the delta targets the 0.07 ms
      term — at best **0.4% of a frame** — in exchange for tracking which
      modifiers changed and reasoning about targets shared between modifiers,
      where a missed invalidation silently produces the wrong body.
      A full stack rebuild is already 43.5x the reference (0.11 ms vs 4.86 ms for
      200 targets). Reopen if `applyStack` ever exceeds ~2 ms.
- [x] `.mhm` saved-model parser + `applyMhm`. **Round-trip parity**: a real
      `.mhm` written by the reference, loaded in C++ and applied, reproduces the
      geometry the reference itself produces from that file. Unrecognised lines
      (skeleton / pose / proxy / material) are preserved verbatim for M4-M5.
- [x] **Parity fixtures**: default stack, extreme macro combinations, every
      modifier at ±1 — `tests/golden/modifiers.txt` holds all **291** modifiers
      with min/max ±1.0 and `test_modifier_parity` checks every one; character
      and macro fixtures cover the stack.
- [x] `data/modifiers/*.json` loader — `loadModifiers`, exercised on all three
      shipped files by `test_mhm_parity` and `test_theme`.

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

- [x] **Every Apache-2.0 module is gated.** The list was `io foundation render`;
      **`ui` is Apache-2.0 and was ungated**, enforced only by review — which is
      what the gate exists to replace. Verified it bites: an AGPL include added
      to `src/ui/Theme.cpp` now fails it.
      `app`, `core` and `rig` are AGPL, so nothing else needs adding; keep the
      list equal to the set of modules declaring Apache-2.0.

## M4 — Proxies, materials, assets (`mh-asset`)

- [x] `.mhclo`/`.proxy` parser incl. `TMatrix` scale
- [x] Barycentric fit `P = Σ w_k·H[v_k] + M·d` — **parity on 3 proxies × 2 bodies**,
      including a reshaped body so the `TMatrix` rescaling is actually exercised
- [~] **`TMatrix` shear is REFUSED, not implemented.** All nine spellings
      (`shear_*`, `l_shear_*`, `r_shear_*`) now return `ProxyErrorKind::Unsupported`
      naming the key.
      Previously they parsed "successfully" and were **silently dropped** — the
      proxy then fitted with the wrong transform and reported success, which is
      the worst of the three outcomes. Refusing is worse than supporting and far
      better than pretending.
      Implementing needs `matrixFromShear` → `affine_matrix_from_points`
      (`shared/proxy.py:476-492`), a general SVD-based affine solve rather than
      the diagonal scale case. **No shipped asset uses shear** — all four
      `.mhclo`/`.proxy` files are scale-only — so it would be untestable
      machinery until one does.
- [x] Delete-vert mask -> face hiding. `visibleVertexMask` unions the
      `delete_verts` of the worn proxies; `Mesh::faceMaskForVisibleVertices`
      turns that into a face mask. **A face survives if ANY corner is visible**
      -- pinned by the `stride` fixture, where hiding 2,737 of 19,158 vertices
      hides zero of 18,486 faces. No shipped asset declares `delete_verts`
      (0 across all four), so the fixtures are synthetic but run through the
      reference's own code.
- [x] **Proxy-on-proxy masking** (2026-09-05) — clothes hiding clothes, not just
      clothes hiding body.
      **The recorded blocker was stale.** "Needs the render-order stack" — but
      `z_depth` has been parsed since the proxy reader landed (`Proxy.h:73`,
      `Proxy.cpp:202-203, 336-339`), so the ordering data was already there.
      Checked before believing the note.
      `core::transferVertexMaskToProxy` ports `shared/proxy.py:960-983`, whose
      two rules are **not** the same rule: a proxy vertex fitted to ONE base
      vertex copies its visibility, while an interpolated one is hidden only
      when at least **two** of its three references are hidden. The natural
      guess — hide if any reference is hidden — erodes a much wider band around
      every hole.
      `core::wornVertexMasks` walks the stack **outermost first**
      (`reversed(sorted by (z_depth, uuid))`, matching
      `3_libraries_clothes_chooser.py:92-99, 125`), handing each garment the
      mask accumulated by the layers above it and folding in its own
      `delete_verts` only afterwards — so a garment is never masked by itself.
      Ties break on uuid, so the answer does not depend on the order the caller
      collected the proxies in. `body` is the same union `visibleVertexMask`
      returns, and that equality is asserted.
      Wired into `main.cpp`'s `applyBodyMask`, **below the early-out**: the
      per-garment masks depend on what is WORN, exactly as the body mask does,
      and nothing there changes when a slider moves. A failure is announced
      rather than swallowed — a garment still rendering through a hole looks
      like a modelling problem, not a bug.
      Five mutations; four caught immediately. The fifth — folding a proxy's own
      deletions in **before** taking its mask, so every garment erases itself —
      survived until a test put a garment's own vertex on a body vertex it
      deletes. Still no shipped asset exercises any of this (all four shipped
      `.mhclo`/`.proxy` files declare zero `delete_verts`), so coverage is
      synthetic, as `visibleVertexMask`'s already was.
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
- [x] **Runtime asset-path resolution** — `foundation::resolveDataDir`.
      `MH_DATA_DIR` was baked in at compile time as an absolute path into
      whichever source tree built the binary, so **an installed or bundled app
      had no assets at all**. CMake's own comment conceded it ("used by
      development builds").
      Order: `$MH_DATA_DIR` → bundle `Resources/data` → `share/makehuman/data`
      → `~/Library/Application Support/MakeHuman/data` → compiled default.
      A candidate counts only if it **looks like an asset tree**
      (`3dobjs/base.obj` present); an override pointing nowhere is ignored, not
      obeyed, so the failure surfaces at startup rather than later and vaguer.
      Verified: a relocated binary loaded a 140 MB tree from a bundle path.
- [x] **Parity fixtures: eye proxy fit at six body shapes** (was two).
      The fit's only body-dependent term is the TMatrix scale, and for the eye
      proxies it is read off HEAD vertices (`x_scale 5399 11998`,
      `y_scale 791 881`, `z_scale 962 5320`) — so the shapes worth capturing are
      the ones that move head proportions, not the ones that merely look
      different. Added `extreme_min` (Age 0.0 — the largest head-to-body ratio
      the model makes), `extreme_max`, and `head_small`/`head_large` driving
      `head-scale-depth|horiz|vert` straight at the three axes the matrix
      divides by.
      **They genuinely widen the range**: neutral + mixed spanned a y-scale of
      0.851..1.034; the six now span **0.417..1.168**. Per-body diagonals are
      captured in `tests/golden/proxy/tmatrix_scales.json` so a body that
      exercises nothing new is visible rather than silently reassuring.
      **18 comparisons** (3 proxies x 6 bodies) against the Python oracle, all
      matching within 1e-5.
- [x] **Found while doing it: 6 of the 18 fixtures cannot fail on scale.**
      All 96 vertices of the low-poly eye proxy use the single-index form —
      weights (1,0,0) and a **zero offset** — so `M·d` is zero whatever the
      TMatrix says. Established by mutation, not inspection: swapping the y and
      z scale terms in `fitProxy` is caught by all 12 high-poly/base comparisons
      (worst delta 0.00046..0.646 dm) and by **none** of the six low-poly ones.
      They are not worthless — they pin the exact-copy path — so that property
      is now asserted outright (`[proxy][exact]`, bit-exact against the body
      vertex) rather than left to look like scale coverage.

## M5 — Rig and skinning (`mh-rig`)

- [x] **Mixamo compatibility answered: a 179-bone superset, not a swap.**
      `tools/mixamo_mapping.py` + `docs/rig/mixamo_mapping.md`. Adopting
      Mixamo's 65 would lose 59 facial bones, 28 toe bones, 8 metacarpals and
      every twist bone. Instead: keep all 163, add 16, and every Mixamo name
      resolves. **49 of 65 already exist** under other names.
      Measured, not assumed — across all seven reference clips Mixamo drives
      **52 of 65** bones, and those 52 are exactly the 49 we have plus `Hips`
      and the two `ToeBase`. The other 13 are never keyed.
- [x] **Superset skeleton built** — `data/rigs/mixamo_superset.mhskel`, 179
      bones, generated by `tools/build_mixamo_superset.py` (never hand-edited;
      CI fails if the file goes stale). Loads through the **same C++ loader** as
      the default rig, which still loads unchanged beside it.
      **No new joint vertex-groups were needed** — every added bone reuses an
      existing joint (a fingertip is the tail of the last phalanx, `hips` sits
      on `root`'s tail), so the superset adds no new dependency on the base
      mesh's helper geometry.
- [x] **`ball.L/R` weighted** — 309 vertices each, in
      `data/rigs/mixamo_superset_weights.mhw` (generated, staleness-gated).
      **Correction to the previous entry: a foot roll never "deformed nothing".**
      Rotating `ball` already moved 1,042 toe vertices through its children. The
      real gap was only the crease at the ball of the foot, whose skin stayed
      with `foot`. Measuring corrected an overstatement.
      The rule is a linear ramp between the ball joint and the toe roots, and it
      **moves weight, never creates it** — worst per-vertex influence drift
      across all 19,158 vertices is 2.22e-16. Centroids run heel → ball → toe
      (z = 0.348 → 1.141 → 1.495), so the region is anatomically coherent.
- [x] **Ball crease judged — and it beats a shipped joint.** Turned the
      subjective question into a measured one: under the same mesh, the same LBS
      and the same 45° bend, compare our generated crease against a joint
      MakeHuman itself ships.

      | region | faces | min area ratio | inverted normals |
      |---|---|---|---|
      | `ball.L/R` (generated) | 792 | 0.364 | **7 (0.88%)** |
      | `lowerarm01.L/R` (shipped) | 1152 | 0.023 | **22 (1.91%)** |

      Half the flip rate and 16x less area collapse than MakeHuman's own elbow.
      At 25° (a walking toe-off) **nothing inverts at all**. The residual flips
      at 45° are inherent to linear blend skinning, not to these weights.
      Gated by `[rig][mixamo][crease]`, which pins the *comparison* rather than
      an absolute number so it stays self-calibrating.
- [x] **CRITICAL, found by trying to use it: the superset rig could not skin.**
      13 of its 179 bones are tip markers whose head and tail resolve to the
      same joint, and `buildRestMatrices` rejected the **entire skeleton** on the
      first zero-length bone. The rig shipped, had a CI staleness gate, and was
      **completely unusable** — one bad bone took the other 178 with it.
      The 13 are legitimate Mixamo counterparts, not junk: Mixamo's own 65
      include `HeadTop_End`, `Left/RightToe_End` and a 4th segment on every
      finger (`LeftHandIndex4` …). They sit at their parent's tail and deform
      nothing — 0 weighted vertices on all 13, verified — so head == tail is how
      "the tip is here" is expressed.
      A tip marker now inherits its parent's basis (the convention Blender
      applies to leaf bones), keeping its own head as the translation. A
      length-less **root** is still an error.
      **The lesson**: the staleness gate asked "is this file current?" and never
      "does it work?". Both new tests ask the second question.
- [x] **`--rig` wired.** `--rig default|mixamo_superset|<path>` selects the
      skeleton the app poses and skins with; weights are taken from
      `<stem>_weights.mhw` beside it. Set once from the CLI, mirroring
      `setDataRoot`, so the four `loadPoseRig` call sites need no new parameter.
      An unknown rig **fails and lists what is installed** rather than silently
      falling back to `default` and posing the wrong skeleton; it exits 1 and
      writes no file (verified).
      Three ctest cases run the real binary and assert the app **announced which
      rig it posed with** (`rig mixamo_superset (179 bones)`) — an export-only
      check would have passed the entire time the superset was unreachable.
      Stem, explicit path, and path-without-extension all resolve through one
      base: `--rig /x/foo.mhskel`, `--rig /x/foo` and `--rig default` behave the
      same. The extensionless path form was **broken in my first version** (it
      resolved to a filename with no extension) and is now covered by
      `app_rig_path`.
      **Scope**: `--rig` selects the skeleton used to pose and skin. The app's
      multi-mesh export path deliberately carries no bones — see
      `include/makehuman/io/SceneIO.h:116`, "a skin belongs to the single-mesh
      overload" — so exported bone data is unaffected.
      **`hips` is not `root`**: the legs and spine meet at root's *tail*, 0.92 dm
      from its head, so binding Mixamo's Hips to `root` would pivot the whole
      character ~9 cm off on every rotation and every root-motion translation.
      `root`'s rest direction also points forward where Mixamo's Hips points up,
      so the hip basis needs an explicit rest offset.
- [x] **`LeftArm` decided: `shoulder01.L`.** Settled by measurement, not
      argument — along the clavicle→wrist chain Mixamo's `Arm` is at 16.2%,
      `shoulder01.L` at 16.0%, `upperarm01.L` at 26.8%. `upperarm01` remains in
      the rig, just unmapped.
- [x] **Geometric oracle built.** Positions alone do not work — the rigs are in
      different rest poses, so arm error grows down the chain and 42 of 49
      correct mappings "fail". **Arc length along a chain is pose-invariant**
      (bone lengths do not change), so that is what is compared, with Mixamo
      rest positions committed to `docs/rig/mixamo_rest_pose.json` so CI needs
      no Blender. Chain roots need a separate positional check — a root is 0% on
      both sides by definition, so the arc measure was blind to the very error
      it was built for.
- [x] **SonarQube UNBLOCKED (2026-09-01, owner pointed at Docker).** Self-hosted
      SonarQube **26.8.0 Community** in the `m2m-sonarqube` container (shared
      with the mesh2motion project), `http://localhost:9000`.

      ```bash
      docker start m2m-sonarqube            # persists in docker_sonarqube_* volumes
      set -a; . ./.sonar-token; set +a      # SONAR_HOST_URL + SONAR_TOKEN, gitignored
      sonar-scanner                         # reads sonar-project.properties
      ```

      The token is a **PROJECT_ANALYSIS_TOKEN scoped to `makehuman`**: it can
      analyse this project and nothing else, so it is not a way into the server.
      It lives only in `./.sonar-token` (gitignored, 0600) — never committed.

      **READ THIS BEFORE TRUSTING A GREEN GATE. Community Edition ships NO C or
      C++ analyser** — CFamily is Developer Edition and above. Verified against
      the server, not assumed: `GET /api/languages/list` returns 26 languages
      and neither `cpp` nor `c` is among them. Confirmed by the measures:
      **`ncloc_language_distribution = py=1956`** for a repo that is ~60k lines
      of C++23.
      So Sonar covers `tools/` and `benchmarks/` Python (which gate three CI
      jobs), the workflow YAML, and the **secrets** analyser across every
      indexed file — `src/` and `include/` are in `sonar.sources` for that
      reason alone. **The C++ gates remain `-Werror` + ctest in four presets +
      ASan + TSan.** Those are the real ones.

      **First scan: 70 violations (1 bug, 69 smells). Now 8, 0 bugs.**
      - **36 were one false-positive class.** `capture_fixture.py` stubs the
        reference's own API — `getRestposeCoordinates`, `callAsync`,
        `addSetting`, `zoomFactor`, `modelCamera` — and Sonar flagged every one
        for not being snake_case. Renaming would simply stop the stubs matching.
        Suppressed per rule, per file, each with the reason in the properties.
      - **Real and fixed**: a lambda capturing a loop variable
        (`mixamo_mapping.py`, the one *bug*); an `assert` inside
        `except Exception`, which swallows AssertionError and vanishes under
        `python -O`; a `# noqa: BLE001 - prose` whose trailing text can make the
        suppression inert; three chained `endswith`/`startswith`; 19 duplicated
        literals turned into named constants.
      - **One rejected with a reason, not deferred**: `python:S6353` wants `\w`
        for `[A-Za-z0-9_]`. Python's `\w` is Unicode-aware, so it would also
        match accented and non-Latin letters. The class is deliberately ASCII.
      - **Behaviour-preservation proved, not assumed**: re-captured **every**
        golden fixture after the constant extraction — all `.bin` blobs
        byte-identical.
      **The gate** is a `MakeHuman` gate copying the pattern the owner already
      accepted for Mesh2Motion: `new_violations = 0`,
      `new_security_hotspots_reviewed = 100%`, `new_duplicated_lines_density
      <= 3`. The default gate's `new_coverage >= 80` was removed **because this
      server cannot see the coverage that exists**: there are no Python tests,
      the 449 C++ tests are invisible to it, so the condition would read 0%
      forever and make the gate uninformative rather than informative. Python
      coverage really is 0 and that is recorded here rather than hidden.
- [x] **All 8 `python:S3776` findings cleared** (2026-09-02) — SonarQube now
      reports **0 open issues** on the project.
      Each tool was refactored into named helpers, one per independent
      responsibility, and each proved by **output diff**, not by reading:
      `blender_validate.py` (32) — 11 files, byte-identical JSON;
      `mixamo_mapping.py` (55 and 22) — identical stdout and `--check` still
      clean; `audit_taskviews.py` (32), `audit_poseunits.py` (20),
      `build_mixamo_superset.py` (16) — identical stdout, and the regenerated
      `mixamo_superset.mhskel` stayed byte-identical in git;
      `baseline_python_core.py` (17) — all **12** benchmark sections identical
      in name, order and every non-timing field (timings vary by nature, so the
      diff is structural and says so).
      **`capture_fixture.py` (40) included**, with the re-capture-and-diff proof
      this item demanded: `tests/golden/slider_layout/layout.json` is
      byte-identical, and `MANIFEST.json` differs only in `captured_at` and
      `reference_commit` — **exactly the delta a re-capture with no code change
      produces**, which was measured first so the baseline was known.
      Two findings I *introduced* were caught by the re-scan and fixed: an
      unused `mixamo` parameter (`python:S1172`) and one extracted function
      still at 27. Also restored an edge case my own refactor lost —
      `max(..., default=0)`, because `max()` on an empty `RIGS` raises where the
      old accumulator stayed at zero.
      All five tools CI runs verified with **system `python3`**, not the venv.
- [ ] **The `character` fixture is not reproducible, and it is not my change.**
      Re-running `capture_fixture.py character` rewrites `cases.json`'s stack
      keys from `../../data/targets/...` to `../../../data/...`. Verified
      environmental by re-running the ORIGINAL script: it does the same. Benign
      today — `test_character_parity.cpp` reads only `name`, `settings` and
      `stack_size`, and every `.bin` is byte-identical — but an oracle whose
      content depends on where the repo sits is a fragile one, and it makes
      "re-capture and diff" noisier than it should be.

- [ ] **Bone naming/order: Mixamo standard** (owner decision, 2026-08-29).
      Measured and documented in `docs/rig/mixamo_bone_order.md` — 65 bones,
      `mixamorig:` prefix, identical across all 7 reference clips.
      Watch the `$AssimpFbx$` decomposition trap documented there: a naive
      assimp node walk gives ~190 nodes and wrong parents.
- [x] **Retarget table shipped: `data/rigs/mixamo_retarget.json`.** And it is
      **not lossy** — that assumption was wrong once the superset existed.
      Against the 179-bone rig the table is **TOTAL**: all 65 Mixamo bones have
      a counterpart, because the 16 `MAPPING` recorded as `None` ("the superset
      must add this") are exactly the 16 the superset added. Verified as a set
      equality, not by eye.
      Generated by `tools/mixamo_mapping.py --emit` from the already-proven
      mapping, so there is one source of truth and no second copy to drift.
      **Not a name-matching heuristic**, which the item explicitly warned
      against: every target must *descend from its Mixamo parent's target* in
      the superset hierarchy. 0 violations across all 64 parented bones.
      Both gates were mutation-tested rather than assumed:
      - editing the committed file → `is stale`;
      - reversing the finger tips (thumb onto the pinky chain) → caught by
        ancestry, naming each wrong bone.
      CI runs `--check` in the `inventories` job.
      **No C++ loader yet, deliberately** — nothing retargets yet, and an API
      with no caller is the speculative kind. The table is data; add the loader
      with its first consumer.
- [x] `.mhskel` parser (`mh_rig`, AGPL) — **exact bone-order parity on all 163
      bones** against the captured reference list. 1.21 ms to load;
      `updateJoints` (rig follows the mesh) is under 0.01 ms, so re-fitting the
      rig every frame is free.
      **There are TWO orderings in the reference and they differ.** `fromFile`
      relaxes in *file order* (fixes sibling order); `getBones()` then does a
      real BFS from the roots (fixes index order — the rest-matrix rows and
      every exporter). Using the first alone put 153 of 163 bones in the wrong
      slot while still producing a valid-looking parents-first list.
      Divergence: an unreachable bone is an **error** here; the reference warns
      and exports a partial rig (`skeleton.py:122-124`).
      Note: the Python skeleton baseline cannot run headlessly — `mhskeleton.load`
      reaches `G.app.selectedHuman` — so those benchmark rows have no reference
      number rather than a fabricated one.
- [x] Bone rest matrices — **full element-wise parity on all 163 bones**, both
      `matRestGlobal` and `matRestRelative`, against the captured reference
      (1,987 of 2,608 fixture floats non-zero, so this is real parity not a
      comparison of zeros). 0.02 ms to rebuild the whole rig.
      Conventions that had to be right together: row-major storage with axes as
      **columns** and translation in the last column; `cross(yvec, pvec)` in the
      plane normal (swapping it flips every bone's roll 180 degrees and still
      yields a valid orthonormal basis); and X rebuilt as `cross(Y, Z)` rather
      than using the plane normal directly, because the normal is generally not
      perpendicular to the bone.
      `rigidInverse` is exact here because the basis is orthonormal by
      construction — verified as its own test, not assumed.
- [x] `matPoseVerts = matPoseGlobal · inv(matRestGlobal)` — **already done**, this
      entry was stale. `src/rig/Skinning.cpp:27`:
      `skinning[i] = global[i] * rigidInverse(b.matRestGlobal)`, covered by the
      skinning parity fixtures and benchmarked at 0.12 ms for 19,158 verts.
- [x] Joint positions from vertex clouds — **already done**, stale entry.
      `src/rig/Skeleton.cpp:38`, the mean of each joint's vertex cloud
      (`skeleton.py:428-434`), computed for *every* joint because rotation
      planes name joints no bone uses. `updateJoints` benchmarks at 0.01 ms.
- [x] `.mhw` weights — **parity across all 139 weighted bones and 57,107
      entries**, plus the compiled 4-influence form across all 19,158 vertices.
      15.8 ms to load, 1.0 ms to compile to 4 influences.
      The file's numbers are **relative**: `wtot[v]` is summed over every bone
      first and each weight stored as `w / wtot[v]`, so a vertex always ends up
      summing to 1. An unweighted vertex binds to the **root bone at weight 1**
      — without that it collapses to the origin the moment the rig is posed.
      Truncation to 4 re-normalises (5,923 vertices actually take that path;
      the rig reaches 12 influences), and ties break by **descending bone
      index**, matching Python's `sorted(reverse=True)` over `(w, idx)` tuples.
      Arbitrary, but it decides which influence survives on symmetric vertices.
- [x] **Influence clamping surfaced.** `CompiledWeights` now carries
      `clampedVertices` and `maxInfluences`; `compile()` always computed both and
      **discarded them**, so the loss was silent.
      It is not a rare edge case: compiling the shipped rig to 4 — what glTF's
      `JOINTS_0`/`WEIGHTS_0` require — clamps **3,665 of 19,158 vertices
      (19.1%)**, the worst carrying **12**. The app now says so:
      `clamped 3665 of 19158 vertices to 4 influences (rig uses up to 12)`.
      The C++ count matches an independent count over the raw `.mhw` in Python,
      so the number is agreed by two implementations, not just recorded.
- [x] **Clamping to 4 measured, and deliberately NOT removed.** The reference
      truncates nothing — `shared/skeleton.py:616` iterates every bone's mapping
      — so 4 is a deviation. Its cost, under a hard 60° bend of both elbows and
      both knees: **1 vertex of 19,158** moves more than 10 µm, that one by
      2.0 mm. Using all 12 would cost 0.127 → 0.238 ms.
      A new API to recover 2 mm on a single vertex is not worth it. **Reopen if**
      a rig ever appears whose 5th-and-beyond influences carry real weight — the
      report now makes that visible instead of silent.
- [x] Euler conventions — **all 24**, plus quaternion matrix/from-matrix/
      multiply/slerp and arbitrary-axis rotation. 120 captured cases
      (24 conventions x 5 angle sets), worst matrix delta at float32 epsilon.
      `[w,x,y,z]` scalar-FIRST throughout; Eigen's `.coeffs()` is `[x,y,z,w]`,
      so any future Eigen interop must reorder.
      Licensing: `transformations.py` is **BSD-3-Clause** (C. Gohlke) with
      MakeHuman's AGPL boilerplate wrongly stamped into its docstring — see
      `LICENSING.md` §4.1. The port is BSD and lives in `mh_foundation`.
- [x] **Euler helpers are wired into BVH** — this entry was half stale.
      `src/io/BvhReader.cpp:224` builds the convention with
      `foundation::eulerOrderFromString`, and `:336` builds the matrix with
      `foundation::eulerMatrix`. No duplicated rotation maths to drift.
- [ ] `.mhpose` loading — **no asset to verify against.** `find . -name '*.mhpose'`
      returns nothing; the format is referenced only by reference *plugins*
      (`7_expression_mixer.py`, `2_posing_expression.py`). Implementing it now
      would be unverifiable machinery, the same call as the proxy shear forms.
      Reopen when a `.mhpose` file exists to test against.
- [x] **CPU LBS — parity on all 19,158 vertices** under a real pose (7 bones
      rotated, 18,069 vertices moved, 3.2 dm max displacement). Worst vertex
      delta **3.8e-6 dm (0.38 um)**; worst pose-matrix delta 6.7e-6 — float32
      rounding, not an algorithmic difference. Pose matrices also compared
      element-wise across all 163 bones.
      **0.11 ms** for the whole body at 4 influences; skinning matrices under
      0.01 ms. Both comfortably per-frame.
      It is *accumulated matrix skinning*: blend the MATRICES, apply once —
      one matrix-vector multiply per vertex rather than one per influence.
- [~] **Skin normals/tangents (the w=0 direction path) — deliberately NOT built**
      (decided 2026-09-05, with the evidence, not deferred again).
      `skinMesh` takes `coords[n,4]` and uses the homogeneous coordinate to
      switch: **w=1 for positions, w=0 for directions** — normals, tangents and
      targets — so translation does not move a direction
      (`shared/animation.py:1129-1145`).
      **It would have no caller.** Every `poseInPlace` site recomputes normals
      and tangents from the DEFORMED geometry immediately afterwards
      (`main.cpp:1305-1306` and `1556-1557`), and `refitProxy` does the same for
      every worn proxy (`main.cpp:422-423`). `skinPositions` has exactly one
      caller in `src/`. Adding `skinDirections` today is precisely the
      built-and-never-wired pattern this port has had to undo repeatedly.
      **And recomputing is the better answer here**: it is exact for the
      deformed surface, where skinning the rest normals only blends them. A
      deliberate divergence from the reference, not an omission.
      **Reopen when GPU skinning lands** (M6): a vertex shader has no adjacency
      to recompute from, so that is the first real consumer of the w=0 path.
- [ ] GPU LBS (matrix palette UBO/SSBO) — M6
- [x] Pose units + slerp-composition blend — **parity on all 60 units x 163
      bones (9,780 transforms)** and on a five-unit weighted blend, in both
      orders.
      The blend is **order-dependent and that is replicated, not fixed**:
      `quat = q_i * quat`, left-multiplied, and quaternion multiplication does
      not commute. Measured: reversing a five-unit blend moves the result by
      **0.034**. Expression files are authored against this. The test asserts
      both that reversing matches the reference's own reversed result AND that
      the two differ — without the second, a symmetrising implementation would
      pass by accident.
      Weights are **not** normalised: the blend is additive.
- [ ] `.mhupb` expression files (weighted unit references) — the consumer of
      the blend above
- [~] **Body pose units: investigated, and the asset does not fit our rig.**
      Format confirmed — 61 poses, each bone -> `[w,x,y,z]` quaternion directly,
      no BVH frames. But it was authored against a richer, differently-named
      skeleton. Against **both** shipped rigs:
      **29 of 61 resolve fully, 24 partially, and 8 resolve to nothing at all**
      (`TorsoRight`, `UpperLegForwardLeft`, `LowerLegBendLeft1/2`, `FootDownLeft`,
      `FootUpLeft`, `Finger1CloseLeft`, `Finger2CloseLeft`).
      24 bone names are absent from both rigs. Many are a naming *generation*
      difference (`spine1..4` vs `spine01..03`, `neck` vs `neck01..03`,
      `shoulder.L` vs `shoulder01.L`) rather than genuinely missing joints;
      others we truly lack (`collisionArm*`, `heel.L`, `metatarsal1..5`).
      **A consumer therefore needs an explicit bone table**, exactly as the
      Mixamo retarget did — name matching silently drops a third of the data.
      Also found: `UpperArmUpLeft1/2` drive `oris01`/`oris02`, **mouth** bones.
      That is an authoring error in the reference asset, now pinned so it is
      never mistaken for ours.
      Measured and gated by `tools/audit_poseunits.py` (CI). **Loader not built**
      — building one before the bone table exists would produce poses that
      silently do nothing. Recorded in `memory/project_context.md` §8.0.
- [x] **Whole-body poses: A-pose and T-pose, render and export** (owner request,
      2026-08-29). `rig::loadBodyPose` reads a single-frame BVH;
      `rig::poseToBoneLocal` converts it into each bone's rest frame.
      The A-pose needs **no file** — the base mesh is authored in one, so the
      rest mesh IS the A-pose.
      **The trap:** a BVH holds rotations in the file's global axes;
      `computeSkinningMatrices` wants them bone-local. Feeding the raw matrices
      in does not fail — it yields a smooth, plausible, wrong pose (arm span
      10.67 dm instead of 16.0). The reference conjugates in
      `shared/skeleton.py:566-593`. Parity fixture: `tests/golden/body_pose/`.
      Agreement C++ / Python / Blender: 15.897 / 15.989 / 15.989 dm.
      App: `makehuman --pose rest|tpose|<path.bvh> [--export out.ext]`,
      7 formats (obj fbx glb usda dae stl 3mf).
- [x] Pose library UI — **stale entry, the window does select one.**
      `src/app/main.cpp:443` builds a `Pose` AssetGroup ("A-pose (rest)" plus
      every `.bvh` under `data/poses`), it is presented by the Materials
      `AssetPanel`, and the `chosen` callback **probes the pose first** — a pose
      that will not load restores the picker instead of becoming an undo entry
      that does nothing. Undo/redo included.
- [x] **`mixPoses`** — `rig::mixPoses(base, overlay, bones)` copies `base` and
      takes `overlay`'s transforms for the listed bones, which is what puts a
      facial expression on a posed body (`shared/animation.py:449-467`).
      Both of the reference's refusals are kept: differing bone counts
      (`FrameCountMismatch` — mixing poses from two rigs yields a
      plausible-looking wrong body) and an out-of-range index (`Malformed`;
      silently skipping it would drop part of the expression).
      **No parity fixture, by explicit exclusion**: this is index replacement
      with no numerical content, so a captured `.bin` would only re-assert that
      a copy copies. Five property tests pin what can break, and a mutation
      that ignores the bone list fails 3 of them (922 assertions).
      The reference's `[[bonesList]]` double-bracket is a numpy quirk, not
      replicated.
- [x] BVH **import** — parity on both shipped files: `tpose.bvh` (222 joints)
      and `face-poseunits.bvh` (212 joints x 60 frames). 12,942 joint-frames
      compared, worst delta < 1e-5. 2.04 ms for the 60-frame file.
      **Written clean-room from the published BVH format**, not translated, so
      it lives in the Apache-2.0 `mh_io` module rather than under AGPL.
      Both files auto-detect as **Z-up**: offsets become `(x, z, -y)`, position
      channels swap with a sign flip, and the rotation order remaps `szyx` ->
      `syzx`. A reader that skips the detection yields a plausible skeleton
      lying on its side.
      Improvement over the reference: every End Site there is named
      "End effector", so they collide and are unaddressable; ours derives
      `<parent>_end`.
- [x] **BVH export** — `io::writeBvh(path, BvhFile)` (`src/io/BvhWriter.cpp`).
      Round-trips `tpose.bvh` and the 60-frame, 212-joint `face-poseunits.bvh`:
      worst matrix delta **5.96e-08**, which is float32 epsilon and exactly the
      floor the pure Euler decompose/recompose achieves.
      **The bug worth remembering**: the reader assigns channel values by *axis
      identity* (`Xrotation`->ax, …) and always calls
      `eulerMatrix(az, ay, ax, order)` (`BvhReader.cpp:323-337`). My first writer
      emitted the angles *positionally*, which agrees only when the channels
      happen to be Z,Y,X — measured **1.69** off on a matrix element otherwise.
      Angles are now written against their own axis's channel.
      **Y-up on output, deliberately.** BVH does not record its up axis;
      `readBvh` converts Z-up input (both shipped MakeHuman poses measure Z-up),
      so Y-up is what was actually parsed. Writing the original channel names
      against converted data would re-read with a different Euler order.
      Standard BVH is conventionally Y-up, so the written file is the more
      conformant of the two — but a Z-up source does **not** come back as Z-up.
      **Blender, a third-party importer, agrees**: it reads generations 1 and 2
      to the same pose (worst delta **0.0** across all 163 bones at frame 30),
      while placing generation 1 **11.19** from the Z-up original — that gap is
      the documented input conversion, not an export error. Our own reader
      agreeing with our own writer would only have proved self-consistency.
      Idempotent in **meaning, not bytes**: generation 2 differs by 7 bytes of
      340,964 because angles pass through a float32 `Mat4` between generations.
      Byte-identity was my first claim and it was wrong.
- [x] Pose units wired — **stale entry**. `loadPoseUnitNames` +
      `makePoseUnits` already map the 60 `framemapping` names onto the BVH
      frames; `test_poseunits_parity.cpp:79` pins all 60 in order, and the
      fixtures (`unit_data.bin`, `blended.bin`, `blended_reversed.bin`) exist.
- [x] **Parity fixtures: rest matrices, skinned positions, blended expression**
      — **stale entry**, all three shipped:
      `tests/golden/skeleton/{rest_global,rest_relative}.bin`,
      `tests/golden/skinning/{skinned,mat_pose,pose_verts}.bin`, and
      `tests/golden/poseunits/{blended,blended_reversed}.bin`.

## M6 — Renderer (`mh-render`)

- [x] **It renders.** Qt RHI on Metal, offscreen, with the ported litsphere
      shader. `mh_render_probe` draws the base mesh to a PNG and reports pixel
      statistics, because a blank frame is the failure that reads as success in
      a log. Verified: **7.9% coverage, luminance 37..212, mean 156.1**, and the
      image is a recognisable human.
      The renderer is **optional in the build** — `find_package(Qt6 QUIET)`, so
      the CI runner (no Qt) still builds and tests everything else. Verified
      with `-DCMAKE_DISABLE_FIND_PACKAGE_Qt6=ON`: renderer disabled, 301/301.
      **`base.obj` is 138 parts helper geometry to 1 part body.** Drawing it raw
      gives a figure in a solid skirt with a box over its face — which reads as
      a renderer bug. `Mesh::staticFaceMask()` applies the reference's rule
      (`apps/human.py:274-289`): hide any group named `joint-*` or `helper-*`.
      13,378 of 18,486 faces remain.
- [x] **Qt RHI swapchain and MSAA — stale entry, and it hid a real defect.**
      The interactive widget has had both since it was written: `QRhiWidget`
      owns the swapchain and `ViewportWidget.cpp` asks for 4x MSAA. The entry
      said "the interactive widget, **not offscreen**", which turned out to be
      an accurate description of a bug rather than a scope note.
      **The production render was aliased.** `OffscreenRenderer.h` claims the
      two "cannot drift" because they share `SceneResources`. They shared
      everything except the one number that is not in `SceneResources`: the
      widget requested **4** and the offscreen path passed a hard-coded **1**.
      Measured on `makehuman --render out.png --transparent`, 1024x1024:

      | | alpha 0 | alpha 255 | partial |
      |---|---|---|---|
      | before | 963,682 | 84,894 | **0** |
      | after  | 962,487 | 83,720 | **2,369** |

      **Zero** pixels of partial coverage anywhere — a hard stair-step
      silhouette in the one output meant for compositing.
      Partial alpha is the assertion because nothing else here can produce it:
      the shader writes alpha from the diffuse map and the no-map stand-in is
      opaque white, so a fragment is always alpha 1. Between 0 and 255 means
      coverage resolved from several samples, and nothing else.
      Now one `render::kSampleCount` that both users read, so the claim in the
      header is true rather than aspirational. Multisample colour buffer
      resolving into the single-sample texture that gets read back —
      `readBackTexture` cannot resolve and a multisample texture is not
      readable. Falls back to single-sample where the backend does not offer
      4x, and the test **skips** rather than fails there: it asserts MSAA is
      used when available, not that every machine has it.
      **Cost: not measurable at this noise level.** Three runs each of the
      subdivided render: 1x gave 2.67 / 2.99 / 4.79 ms, 4x gave 4.89 / 3.26 /
      3.83 ms. The ranges overlap; both are far inside a 16.7 ms budget. Stated
      as indistinguishable rather than free.
- [x] **Persistent vertex/index buffers — already done for the path that
      matters.** `ViewportWidget` creates its `SceneResources` **once**
      (`ViewportWidget.cpp:94`) and re-uploads only when something changes
      (`:113`), so interactive frames do not rebuild anything.
      `OffscreenRenderer` rebuilds per call by design — it is one-shot, and that
      is what makes its timing a pessimistic upper bound on the interactive one.
      Partial updates from dirty ranges were measured and closed in M2: the
      gather is 0.7% of a frame, and the reference's own "partial" path is a
      pessimisation.
- [x] Quad→triangle at buffer-build time — **already done**, stale entry.
      `src/core/RenderMesh.cpp:172` fan-triangulates at unweld time: `(0,1,2)`,
      `(0,2,3)`, … for any corner count, and a triangle stored as a degenerate
      quad emits one triangle.
- [~] Shader port to `.qsb`: **litsphere done** (`resources/shaders/rhi/`),
      compiles to SPIR-V + GLSL 450 + MSL 12 via `tools/compile_shaders.sh`;
      the `0.495` constant verified present in the generated Metal.
      Remaining: phong, normalmap, skin, toon, xray (sources stay in
      `data/shaders/glsl/`; copying them into resources/ would just drift).
- [ ] Wire `qt6_add_shaders()` into CMake — deferred until Qt is a real build
      dependency, so CI does not pay a 5-minute Qt install for shaders nothing
      renders yet.
- [x] **Litsphere/matcap pixel-faithful** — and it was **not** faithful.
      `0.495` and the `2.0 − mean` term were already correct. A **third** term
      was not: the reference samples the **raw interpolated normal**
      (`litsphere_fragment_shader.txt:78`, `vec3 normal = vNormal;`) and does
      **not** renormalize per fragment. We did.
      Interpolation shortens the normal across a triangle, pulling the litsphere
      UV toward the sphere centre, and the shipped litspheres were authored
      against that. **Measured**: renormalizing changes **0.98% of the frame**,
      by up to **107/255** in a channel — visible, not rounding. The
      `normalize()` is gone.
      **A tempting argument I tested and had to discard**: that renormalizing is
      more stable under tessellation. Base vs subdivided differs by 3.09%
      (reference) against 2.92% (renormalized) — confounded, because
      subdividing moves the silhouette too. It favours nothing, and is not
      offered as a reason.
      Renormalizing remains available as a deliberate *quality* choice; it just
      is not parity.
      Guarded by `[render][litsphere]`, a **source-literal** check — no rendered
      image can defend these, since every variant still draws a plausible lit
      figure, which is precisely why the divergence survived. Both mutations
      fail it (restoring `normalize()`; `0.495`->`0.5`).
- [x] Eye-space-fixed light — **already done**, stale entry.
      `src/render/SceneResources.cpp:277`: the MODEL rotates and the camera
      stays put, which is what keeps the litsphere's fixed eye-space lighting
      correct (`glmodule.py`).
- [x] **Depth range `[0,1]` — already correct by construction, now tested.**
      The renderer never hand-rolls a projection: `SceneResources.cpp:273` takes
      Qt RHI's own `clipSpaceCorrMatrix()`, which supplies the live backend's
      depth convention ([0,1] on Metal, not OpenGL's [-1,1]). The item's premise
      — that we would transliterate the reference's GL projection — never
      applied.
      It was **untested**, which matters because an ignored depth range still
      renders: a "does it draw" test passes while the picture is wrong. Now
      `[render][proxy][depth]` uses the eye proxy as the probe, since it sits
      entirely inside the skull: from **behind**, adding the eyes must change
      **0 pixels**; from the front it must change more than 0, so the test
      cannot pass by the proxy simply not drawing.
      Mutation-verified: disabling depth testing puts **16 eye pixels through
      the back of the head** and fails the test.
- [x] **Per-mesh diffuse (albedo) maps — the skin-texture path now exists.**
      Every mesh used to sample ONE shared **1x1 white** diffuse
      (`SceneResources.cpp`), so a skin texture could not be shown at all.
      `MeshInstance` now carries a `diffuse` path, each `Drawable` owns its own
      texture, and the app feeds each mesh **its own material's**
      `diffuseTexture` — body from the skin `.mhmat`, each proxy from its own,
      so one shared map cannot paint the eyes with body skin.
      Empty stays the 1x1 white (pure matcap), which is what every mesh had
      before. A **named-but-unloadable** map is an error, not a silent fallback:
      otherwise a wrong path renders a plausible untextured body and reads as a
      shading bug.
      Tests generate their texture in-process — no skin texture ships, and this
      also keeps the suite free of any third-party asset licence.
      Mutation-verified: forcing the shared white back fails 2 of the 3.
- [ ] **OWNER REQUEST (2026-09-01): varied human skin tones, MetaHuman-level
      detail.** Sources given: freepbr.com/product/human-skin1, texturecan.com
      /details/574, 3dtextures.me/tag/skin, texturing.xyz/collections/vface.
      **Current state**: `data/skins/` holds exactly ONE material,
      `default.mhmat`, which names **no texture** (`shaderConfig diffuse false`)
      and sets `autoBlendSkin true`; `data/textures/` holds only
      `texture_notfound.png`. The three "Skin" choices in the UI are
      **litspheres (matcaps)** — lighting captures, not skin colour. So there is
      no albedo, normal, roughness or cavity map anywhere in the tree.
      **Licence triage before anything lands in `data/`** (hard rule 6):
      - `texturing.xyz` VFace — **paid commercial, redistribution forbidden.
        Cannot go in this AGPL repo.** Usable only as a private local asset.
      - `3dtextures.me`, `texturecan.com` — CC0, fine, record in `LICENSING.md`.
      - `freepbr.com` — free to use but restricts redistributing the raw files;
        check per asset.
      I also cannot fetch binaries (the fetch tool returns text), so the image
      files have to come from the owner.
      **Next, in order**: (1) ~~normal~~ and ~~AO~~ **done**; (2)
      ~~`autoBlendSkin`~~ **done**; (3) a real PBR metallic-roughness pipeline —
      **roughness has no `.mhmat` channel at all** (the seven are Diffuse,
      BumpMap, NormalMap, DisplacementMap, SpecularMap, TransparencyMap, AoMap),
      so it belongs to that pipeline rather than this one; (4) the skin `.mhmat`
      set, which is what the owner still has to supply.
- [x] **AO maps** — multiplied over the result, and **after** the additive term,
      matching `litsphere_fragment_shader.txt:103-105`. Folding it into
      `shading` earlier would also scale the additive contribution, which the
      reference does not do.
      Tested directionally, not just "the image changed": AO may only **darken**,
      so the test asserts **0 pixels got brighter**. A change-only check would
      pass on a map that lit the model up.
- [x] **Materials now drive the viewport.** One `viewportMapsOf()` read supplies
      diffuse, normal, AO, intensity and `autoBlendSkin` — replacing two
      separate `.mhmat` loads per rebuild, which ran on every slider drag.
      Body and each worn proxy read **their own** material, so clothing detail
      does not inherit the body's.
      **Verified inert until assets exist**: with no maps shipping, the render
      is **byte-identical (0 of 4,096,000 pixels)** to the previous commit.
- [x] **Normal maps — where surface detail actually lives.** Pores, wrinkles and
      fine skin structure come from a tangent-space normal map, not the albedo,
      so this is the mechanism behind the "MetaHuman-level detail" request.
      The mesh already had **correct** tangents (Lengyel, with the reference's
      three bugs fixed); they were simply never uploaded. Vertex layout is now
      pos+normal+uv+**tangent** (12 floats, ~1.3 MB more on the subdivided
      mesh).
      **One pipeline, not two.** The vertex shader's own comment planned a
      separate shader per variant; a uniform branch is uniform control flow and
      avoids doubling pipeline state for a per-mesh runtime choice. Whether a
      map exists is per MESH, so it needed a **per-mesh uniform block** —
      the existing `Buf` is per frame.
      A flat 1x1 placeholder cannot replace the branch: unpacking a flat map
      gives `normalize(TBN * (0,0,1))` = `normalize(vNormal)`, and normalizing
      is exactly what the no-map path must not do.
- [x] **TWO reference defects found and NOT ported** (recorded in
      `project_context.md` §8.0):
      1. **`normalmapIntensity` does nothing in the reference.** It computes
         `(2*normalH - 1) * intensity` then `normalize(tbnMat * normal)` — a
         uniform scale followed by a normalize **cancels exactly**. It would
         only matter under `CALC_NORMAL_Z`, commented out at `:64`. Caught
         because the test measured **0 pixels** between intensity 1.0 and 0.01.
         Ours scales **XY only, keeping Z** — the standard strength idiom and
         what `CALC_NORMAL_Z` was reaching for.
      2. **The binormal discards handedness**: `cross(vNormal, tang)` ignores
         Lengyel's sign, so mirrored UV islands on a symmetric human get
         inverted normal-map lighting on one side. We carry `tangent.w`.
- [x] **The litsphere source guard had to become more precise.** It banned
      `normalize(vNormal)` outright, which broke the moment normal mapping
      arrived — for a *correct* change, since the TBN basis needs a unit normal.
      It now bans the **assignment** `normal = normalize(vNormal)`, which is the
      regression it was actually written for. Re-verified by mutation.
- [x] **`autoBlendSkin` ported — continuous skin tone from the ethnic sliders.**
      `mh::core::SkinTone` (**AGPL**: a translation of `apps/autoskinblender.py`,
      so it belongs in `core`, never in a permissive module).
      This is how ONE asset set becomes a *range* of skin tones instead of three
      presets — the mechanism behind varied human colouring.
      `ethnicDiffuseColor` is `asian*asianColor + african*africanColor +
      caucasian*caucasianColor` (`:116-118`) with the constants at `:46-48`.
      `blendEthnicLitsphere` reproduces three things that are easy to get wrong
      and still look plausible:
      - only weights **> 0** contribute, gathered caucasian, african, asian;
      - **one** contributor returns the image **unmixed** (no rounding pass);
      - two are `w0*a + w1*b` — **both weights given, not a lerp**;
      - a third folds in at **weight 1.0 on the accumulator**, not a running
        average.
      Rounding is the reference's `int(w1*d1 + w2*d2 + 0.5)`.
      **Cross-checked against the reference itself**, not just my own
      arithmetic: running `image_operations.mixData` in Python gives the same
      75, 40, 60 and the same mid tone `[0.59033, 0.44, 0.338]`.
      Operates on raw RGBA bytes so `core` needs no image library and the
      licence boundary holds.
- [x] **`autoBlendSkin` wired to the viewport — skin tone follows the sliders.**
      `MeshInstance` gained `litsphereRgba`/`Width`/`Height`: a blended tone has
      no file behind it, and writing a temp PNG per drag would put disk I/O on
      the interactive path. The three ethnic litspheres are decoded **once**;
      the blend is a pass over bytes.
      Verified end to end, both directions:
      - `Caucasian=1.0` renders **byte-identical** (0 differing pixels of
        4,096,000) to using the caucasian litsphere directly — the blend is
        exact, not merely plausible;
      - `African=1.0` differs by **214,693 pixels (5.24%)**, worst 82/255.
      **A real bug found doing it**: I first fed the blender
      `modifierValue("macrodetails/Caucasian")` — the **raw slider**. Those are
      not renormalised: setting Caucasian to 1.0 leaves the other two at 1/3, so
      the weights summed to **1.667** and a "pure" caucasian skin blended as
      1.0/0.33/0.33 of all three. It looked like a perfectly good skin tone.
      `Human::factors()` returns the renormalised values, which is what the
      reference reads (`human.getCaucasian()`). Pinned by
      `[core][macro][ethnic]`.
      A size-mismatched in-memory litsphere is refused rather than trusted —
      reading past the buffer would be a heap overflow.
- [~] **Blinn-Phong→PBR conversion: done and shared** (2026-09-05).
      `foundation::metallicRoughnessOf` is the one conversion every writer uses.
      It already existed in effect — `GltfWriter.cpp` and `UsdWriter.cpp` each
      computed `clamp(1 - shininess, 0, 1)` separately, with the reasoning for
      `metallic = 0` living in only ONE of the two comments and the other
      deferring to it by reference. Two writers computing the same thing apart
      is how they end up disagreeing about one character.
      **`metallic = 0` is a modelling statement, not a stub**: skin, cloth, hair
      and eyes are all dielectric, and `.mhmat` has no field that could say
      otherwise. Now stated once, where a writer has to read it.
      Verified the exported numbers are unchanged: skin roughness 0.04000002,
      eye 0, metallic 0, in both `.glb` and `.usda`.
- [ ] **PBR metallic-roughness in the VIEWPORT — owner decision, not a task.**
      The viewport shades with a **litsphere/matcap**, which is what the
      reference does and what makes the shipped skins look the way they do. A
      metallic-roughness path is not an addition to that, it is a different
      lighting model and a different look: it needs light rigs, an environment
      or IBL, and every shipped litsphere stops being the thing that defines the
      material. **Ask before building** — the export side is already PBR, so
      nothing downstream is blocked on it.
- [ ] GPU skinning (matrix palette UBO/SSBO)
- [ ] ID-buffer picking with async readback (replaces the full-window sync readback)
- [x] **Cached bounding box: measured, deliberately NOT built.**
      `Mesh::boundingBox()` has **zero production callers** — it appears only in
      three tests. It is one linear pass over 19,158 `Vec3`.
      Caching a value nothing asks for adds invalidation to every mutation site
      for no measurable gain. Same call as `findFaceGroup` in M2.
      **Reopen if** a per-frame caller appears (a camera framing or culling
      path would be the plausible one).
- [x] **Offscreen render + alpha mask — and the app can finally reach it.**
      `OffscreenRenderer` existed but **nothing in the app ever used it**: it was
      library-and-test-only, so the "production render" capability had no
      user-facing entry point at all.
      `RenderSettings::transparentBackground` clears alpha to 0. The readback was
      always RGBA8888, so alpha was carried all along — the clear simply
      hard-coded it to 1. The body stays opaque because the shader writes
      `outColor.a` from the diffuse, and the no-map stand-in is opaque white.
      `--render <png> [--transparent]` renders headlessly: it needs a GPU but
      **no window**, unlike `--screenshot`, so it works on a machine with no
      display.
      The scene assembly was factored into `buildScene()` and is now **shared**
      by the viewport and the production render — a duplicate would let the two
      quietly disagree about what a character looks like.
      Verified end to end by a third party: Blender reads the opaque PNG as
      1,048,576/1,048,576 opaque, and the transparent one as **84,894 opaque
      (the figure) against 963,682 clear**, corner alpha 0.00. Gated by
      `app_render`, plus `[render][alpha]` which also pins that the **default**
      render stays fully opaque — a default that drifted transparent would break
      every screenshot path downstream.
- [x] **60 fps on the subdivided mesh — MET, with a wide margin.**
      Subdivided mesh, 55,784 render verts, 1280x960:
      **2.5-4.6 ms median in release, i.e. 218-354 fps** against a 16.7 ms
      budget. Base mesh 1.86 ms.
      **Deliberately measured pessimistically**: the offscreen path builds a
      whole `SceneResources`, uploads every buffer and texture, draws AND reads
      the image back on every call. The interactive widget does none of that per
      frame, so its steady-state frame is strictly cheaper. An upper bound that
      fits the budget is the useful direction.
      Guarded by `[render][fps]`, which asserts the **budget** rather than the
      measurement so it stays meaningful on slower hardware.
      **The test MEASURES; it does not assert a time.** Three red runs taught
      that:

      | build / machine | subdivided median |
      |---|---|
      | release, dev machine | **2.5-4.6 ms** |
      | ASan, dev machine | **59.5 ms** (24x) |
      | debug, CI runner | **35.5 ms** (~10x) |
      | **release, CI runner** | **17.1 ms** (over budget) |

      I excluded sanitizers, then excluded debug, and CI **still** went red — at
      which point the premise was the problem, not the exemptions. A wall-clock
      budget is a claim about **target** hardware, and a shared, virtualised CI
      runner is not that.
      So the 60 fps claim lives **here, as a measurement on stated hardware**
      (this dev machine, release, 1280x960). The test guards what is
      hardware-independent — the subdivided mesh renders repeatedly and returns
      a correctly sized image — and prints the timing every run.
      **Rule**: performance gates belong on controlled hardware. Do not assert
      wall-clock in CI.
      **Not yet measured**: the real interactive swapchain path (`QRhiWidget`),
      which remains an open M6 item.

## M7 — Interchange (`mh-io`)

- [~] **`mh::io::Scene` IR / shared export options — the type was the wrong
      answer.** Four writers carry five identically-named, identically-meaning
      settings (`unit`, `scale`, `feetOnGround`, `writeNormals`, `writeUVs`),
      and this session found **six** defects that were exactly one writer
      diverging: import units, skins, morph targets, `feetOnGround`, vertex
      compaction and Collada's unit declaration.
      A shared base looked like the fix and is not: it expresses the
      relationship without enforcing it — a writer can inherit a field and
      ignore it — and it costs the aggregate-init the per-format `unit` defaults
      rely on. Only a test that reads the FILE catches a writer ignoring a
      field.
      So the mechanism is
      `[units][io][options]`: every shared option toggled on all four writers,
      checked in the output (`vn `/`vt `, `"NORMAL"`/`"TEXCOORD_0"`,
      `normal3f[] normals`/`primvars:st`, and an assimp readback for FBX).
      **All four already honoured all five** — a null result on the product, a
      standing gate from here. Mutation-verified: dropping `options.scale` in
      the USD writer, or `options.writeUVs` in the OBJ writer, each fails it.
      The IR itself stays open, but its motivation is now smaller than it
      looked: the drift it would prevent is prevented by these tests.
- [x] **One unit system across every WRITER — verified, not assumed.**
      `unitScale()` is defined once (`src/io/ObjWriter.cpp:41`) and used by all
      four writers (OBJ, glTF, USD, assimp/SceneIO). USD's second switch is a
      different quantity (`metersPerUnit`, real-world scale), not a duplicate.
      **A new unit cannot silently go wrong**: adding `Unit::Millimeter`
      produced three `-Werror,-Wswitch` errors, one per switch. Measured, not
      assumed.
      `test_unit_correctness.cpp` already had per-writer tests plus "all writers
      agree at the same unit".
- [x] **Import had NO unit contract — a 10x error, now recoverable.**
      Our own GLB round-trips a **16.9455 dm** human back as **1.6946**, because
      glTF is metres and import treats file numbers as internal units. Used
      as-is that is a **17 cm doll** — the same class as the reference's FBX 10x
      defect (§8).
      Fixed by stating the contract rather than silently rescaling:
      `ImportedScene::metersPerUnit` says what one file unit is worth.
      Coordinates stay in **file units on purpose** — `fbxHeight` in the unit
      tests measures a file precisely because import does not rescale, and
      converting would break that. Now `decimetres = units * metersPerUnit * 10`
      is available instead of guesswork.
      Sources by trust: **glTF/GLB = 1.0 by specification** (verified: assimp
      reports no unit metadata at all for it); **FBX = its own
      `UnitScaleFactor`**, centimetres per unit; anything else **0, meaning
      "you decide"** — OBJ and STL are genuinely unitless and inventing 1.0
      would let a caller convert confidently and wrongly.
      **Our FBX export is fine for real tools**: Blender reads it at
      **1.694 m**, a correct human.
- [x] **`io::Transform`, shared by every writer** (2026-09-02).
      `include/makehuman/io/Transform.h` holds `Unit`, `unitScale`, and a
      `Transform{scale, groundOffset}` with `place()` / `placedY()`, plus
      `sceneTransform()` and `meshTransform()`.
      **The duplication was real, not speculative**: the same eight lines
      appeared in **five** places — `GltfWriter`, `SceneIO` (twice), `UsdWriter`
      and `ObjWriter` — four of them character-for-character identical. Net
      **-56 lines** across the writers.
      Also fixed the smell that made this obvious: `UsdWriter.h` was including
      `ObjWriter.h` purely for `Unit`/`unitScale`. Both now live in
      `Transform.h`; `ObjWriter.h` includes it, so every existing includer still
      sees `Unit` and nothing downstream changed.
      **`ObjWriter` deliberately keeps its own**, now with a comment saying why:
      every other writer levels by the lowest vertex in the buffer, but OBJ
      writes only the vertices its kept faces reference, so it must skip the
      dropped ones. Levelling by a vertex that never reaches the file lifts the
      model off the floor by however far the hidden helper cage hangs below it.
      A template over the entry type, because the three writers' entry structs
      agree only on having a `.mesh`; a common span would allocate on every
      export to satisfy a signature.
      Three mutations, all caught: take the minimum before scaling (looks
      correct at the default scale of 1); drop the `isfinite` guard (an empty
      scene would be lifted by -inf and write a file of NaNs); offset x and z as
      well as y.
      Verified end to end: Blender still **11/11**, so no export geometry moved.
- [~] **Import: multi-mesh now works** (`io::importScene`). Formats come from
      assimp, so FBX, glTF/GLB, DAE, STL and OBJ all read.
      **The gap this closed**: export has been multi-mesh for a while — a
      dressed character is body + one entry per worn proxy — while import read
      only `mMeshes[0]`. A round trip therefore returned a **naked character**:
      the clothes exported correctly and vanished coming back in.
      Round-trip tested through a **third-party reader**, so agreement is not
      self-confirming, and including our **hand-rolled GLB writer** — the same
      reasoning that made `usdchecker` worth using.
      Mutation-verified: restricting the loop to the first mesh fails 2 of the 3
      cases.
      A mesh with no triangles is **skipped, not fatal** — real scenes carry
      empty or non-triangular helper meshes, and failing the whole file for one
      would make many usable assets unopenable. The scene errors only when
      nothing usable came back.
      `importMesh` stays as the single-mesh entry point and still reports
      `meshCount`, so callers can tell what they are dropping.
- [x] **Node transforms on import — meshes were all landing at the origin.**
      A glTF/FBX/DAE scene places meshes with a **node graph**: the mesh data is
      local and the node carries where it goes. `importScene` read
      `aiScene::mMeshes` directly and never walked `mRootNode`, so **every
      imported object stacked at the origin**, silently, with no error.
      Caught with an independent fixture rather than our own output:
      `tools/make_scene_fixture.py` has Blender write two identical cubes whose
      **only** difference is a node translation of ±5. Before the fix both
      imported at x ∈ [-1, 1].
      Now walks the graph accumulating world transforms. That also gives
      **instancing** for free — one mesh referenced by two nodes is two placed
      objects, which a mesh-array loop cannot express at all.
      A file with no node graph still falls back to reading the mesh array.
      Only positions need transforming: normals and tangents are not imported
      yet, so there is no inverse-transpose to get wrong.
- [x] **Materials on import — and a missing half of EXPORT found doing it.**
      `ImportedSceneMesh::material` carries name, colours, opacity/transparency
      and texture paths. **Absent means the file had none**, not that it had a
      default, so a caller can tell before substituting its own.
      **The export gap**: chasing why no texture survived showed our exporter
      **never wrote texture paths at all** to the assimp formats. A character
      exported to FBX/DAE reached a DCC tool with colours but **no skin**, which
      reads as a broken exporter rather than a moved file. `fillMaterial` now
      writes `AI_MATKEY_TEXTURE_DIFFUSE` and `..._NORMALS`.
      **What each format actually keeps, measured rather than assumed**:

      | | name | diffuse | specular | opacity | textures |
      |---|---|---|---|---|---|
      | FBX | yes | yes | **no** | yes | **written, not read back** |
      | Collada | yes | replaced by texture | yes | yes | yes |

      Two need explaining, not asserting around:
      - **FBX textures ARE written** — the path is in the exported bytes three
        times — but assimp's FBX *importer* does not read material textures.
        A reader limitation, not a missing export, so the test checks the FILE.
      - **Collada replaces the diffuse colour with the texture**
        (`<diffuse><texture/></diffuse>`) — the format's own semantics.
      Shininess was deliberately **not** asserted, on the belief that "the
      conventions differ per format". **They did not — see the next entry.**
- [x] **Shininess was never exported at all, and our FBX arrived in Blender as
      chrome.** Two defects, one measured through our own reader and one through
      a third party.
      1. **`fillMaterial` never wrote `AI_MATKEY_SHININESS`** while `importScene`
         **read** it. A 0.96 skin came back **0.2** from FBX (our own struct
         default — the file carried nothing) and **10** from Collada (assimp's
         exporter substituting a fixed exponent). 10 in a field every consumer
         treats as 0..1 is not merely wrong: glTF and USD roughness is
         `1 - shininess`, so a Collada round trip asked for roughness **-9**,
         clamped to **0** — a perfect mirror. Now written and read as an
         **exponent** (`foundation::specularExponentOf` /
         `shininessFromExponent`, the 0..128 `GL_SHININESS` scale), clamped on
         the way in because the number comes from a file. FBX and Collada both
         round-trip 0.96 → 0.96, measured.
      2. **assimp's FBX exporter fills its material template with
         `ReflectionFactor` 1**, and Blender reads that key straight into
         Principled `metallic` (`import_fbx.py:2101`). Measured in **Blender
         5.2** on `makehuman --export x.fbx`: `DefaultSkin` came in at
         **metallic 1.0** — a chrome mirror — while the **GLB of the same
         character read 0.04 / 0.0 and always did**. Two files from one material
         disagreeing about whether skin is metal. Stating
         `AI_MATKEY_REFLECTIVITY = 0` takes it to **0.0**; `.mhmat` has no
         metalness concept at all, which is why the glTF writer already
         hard-coded `"metallicFactor":0`.
      **Not fixed, and not ours**: Blender reads FBX `Shininess` as 0..100
      through `1 - sqrt(S)/10` (`import_fbx.py:2083`, whose own comment calls it
      "totally empirical"), so any shininess above **0.78** clamps its roughness
      to 0. Chasing that curve would mean abandoning the exponent's defined
      meaning to please one importer.
      Mutation-verified: reverting either the export scale or the import
      conversion fails 2 tests; `ReflectionFactor` 1 fails the FBX property
      test.
- [x] **Skins on import — a rigged export now comes back rigged.** All **163
      joints** survive a glTF round trip, with names and inverse-bind matrices.
      Without this the geometry and the bones both survived and **nothing
      connected them**: a character round-tripped through glTF silently lost its
      skeleton binding.
      `ImportedSkin` is **owning**, unlike `foundation::SkinView` — a view over
      assimp's scene dangles the moment the importer goes out of scope.
      The test asserts the property a bad vertex-id remap would break:
      **weights are a partition of unity per vertex**. 110,113 assertions; every
      weight in [0,1], every weighted vertex summing to 1 within 1e-3.
      Mutation-verified: halving the weights fails it.
      A weight naming a vertex the mesh does not have is an **error, not a
      silent drop** — dropping it would leave a body part unbound and moving
      with the wrong bone, which looks like a rigging bug rather than a corrupt
      file.
      Bone vertex ids are post-`JoinIdenticalVertices`: assimp remaps them, so
      they index the vertices we just read rather than the file's originals.

- [x] **M7 import is functionally complete**: geometry, names, node placement,
      materials and skins. Remaining gaps are format-specific and recorded
      above (assimp's FBX importer does not read material textures back;
      Collada replaces diffuse colour with its texture).
- [x] **Export glTF 2.0 / GLB** — from spec, single self-contained `.glb`,
      metallic-roughness material, correct metre units, V flipped for glTF's
      top-left UV origin. Validated by spec conformance **and by assimp**, an
      independent implementation.
- [x] glTF **skins** — `JOINTS_0` / `WEIGHTS_0` / `inverseBindMatrices`, one
      node per bone with the hierarchy, 163 joints. Confirmed by Blender.
      Two traps pinned: glTF matrices are **column-major** (ours are row-major,
      so they transpose on the way out), and the joints are scaled by the writer
      from one array so they cannot drift from the mesh — a test asserts both
      scale by exactly 10x between metres and decimetres.
      Weights are expanded from mesh vertices (19,158) onto **render** vertices
      (21,833) via `vmap`; glTF's are vertex attributes, so skipping that leaves
      everything past the first UV seam weighted to the wrong bone.
- [x] glTF **embedded textures** — already done, stale entry. `EmbeddedImage`
      packs images into the BIN chunk with MIME sniffed from magic bytes and
      identical images deduplicated; covered by `test_gltf_writer.cpp` including
      the rejection path for a format GLB cannot carry.
- [ ] glTF **Draco** compression (blendshapes done — session 029)
- [ ] **Export** FBX 7.4/7.5 — from spec, **not** by translating the GPL Blender code
- [x] **Export USD (`.usda`)** — written from the published format, **not** by
      linking OpenUSD. Checked first: assimp has **no USD support at all**,
      neither import nor export (verified against the linked build's format
      list), and OpenUSD is a very large build carrying Pixar's modified
      Apache-2.0 with a trademark clause. `.usda` is documented text, so
      writing it keeps this permissive with no new dependency.
      Blender confirms: 21,833 verts / 36,972 tris / 1 UV layer / 169.5 cm —
      identical to glTF and FBX. **7/7 exports now agree.**
      Two conventions that differ from glTF and are easy to carry over wrongly:
      USD's UV origin is **bottom-left** (so V is *not* flipped, unlike glTF),
      and USD **records** its up axis rather than leaving it to be guessed.
      `subdivisionScheme = "none"` is written explicitly: without it a consumer
      may treat the mesh as a subdivision cage and render a smoothed, shrunken
      body.
- [x] **USDZ** — `io::writeUsdzScene`. One self-contained file: stage plus
      every texture.
      **`usdchecker --arkit` reports Success** — Apple's own validator on its
      strictest profile. That is the check that matters; the unit test pins the
      structure so a regression is caught without the tool.
      The format rules came from **a reference archive `usdzip` actually
      produced**, not from memory of the spec:
      - every entry **STORED**, never deflated — a consumer memory-maps the
        archive and reads the stage in place, so deflated data is unreadable;
      - every entry's **data on a 64-byte boundary**, padded through the zip
        extra field with header id **0x1986** (usdzip emits id 0x1986 / size 22
        / zeros, giving 30 + 8 + 26 = 64). The padding is a **well-formed TLV**,
        not loose bytes, because a strict reader parses it as one — which is why
        a 1..3 byte gap takes another whole 64 rather than emitting a malformed
        field.
      - the stage is the **first** entry, which is how a reader finds it.
      Verified independently by Python's `zipfile`: CRCs valid, `stored=True`,
      data at offset 64, and the payload reads back as `#usda 1.0`.
      No second code path decides what a self-contained stage needs:
      `writeUsdaScene` already copies textures beside the stage, so whatever
      lands in the scratch directory **is** the archive's contents.;
      needs a zip writer.
- [x] **UsdSkel** — skeleton and skinning in USD. `usdchecker` reports
      **Success** on a 163-joint skinned stage.
      `SkelRoot` root, a `Skeleton` prim with `joints`/`bindTransforms`/
      `restTransforms`, and the first entry bound via `SkelBindingAPI` with
      `primvars:skel:jointIndices`/`jointWeights` at `elementSize = 4`.
      **Two traps that `usdchecker` cannot see** — both produce a valid stage
      that poses wrongly, so both are pinned by tests:
      1. **USD uses ROW vectors; this codebase uses COLUMN vectors.** They are
         transposes, so an untransposed write emits every joint transposed.
         Verified positionally: the root bone's head `(0, 0.5639, -0.7609)` must
         be the **last ROW**, and it is.
      2. **Joint tokens are USD paths of identifiers.** MakeHuman bone names
         carry a dot and a dash (`upperarm01.L`, `finger1-1.L`) and a dot is the
         path property separator, so they are sanitised —
         `clavicle_L/shoulder01_L/upperarm01_L`. This one `usdchecker` *does*
         reject, which is how it was caught.
      `restTransforms` are **local**, unlike `bindTransforms` which are world:
      emitting world for both compounds every joint by its ancestors.
      **A vacuous assertion of my own, caught by mutation**: the first transpose
      check only looked for the number `0.5639` *somewhere* in the matrix — it
      appears either way, just in a different place — and passed with the
      transpose removed. Strengthened to match the whole last-row tuple; the
      mutation now fails 2 assertions.
      Forwarded through `writeUsdzScene`, so a skinned USDZ works too.
- [x] **Export OBJ** (`mh::io`, Apache-2.0) — face lines byte-identical to the
      reference across all 18,486 faces; unit conversion; feet-on-ground computed
      from the mesh minimum rather than the reference's Y-only joint offset;
      face masking; `.mtl` sidecar
- [x] Export STL, DAE, BVH — **already done**, stale entry. STL (binary and
      ASCII), Collada and 3MF are `SceneFormat` values through `exportScene`;
      BVH export is `io::writeBvh` (session 076, round-tripped to float32
      epsilon).
- [x] Skin weights: sorted, normalised, clamped to 4 — `VertexWeights::compile`
      (session 022), consumed by both the glTF and FBX skin exports.
- [x] **Blendshape export (glTF)** — dead in the reference everywhere, so there
      is **no oracle**: Blender is the primary check, not a cross-check.
      Verified by exact moved-vertex counts, which reconcile to the source files:
      | target | non-zero rows | Blender moved | seam duplicates |
      |---|---|---|---|
      | head-oval | 2,143 | 2,200 | +57 |
      | head-trans-backward | 5,498 | 5,865 | +367 |
      | nose-base-up | 294 of **305 rows** | 294 | +0 |
      The nose figure looked wrong until it wasn't: **11 of its 305 rows are
      literally (0,0,0)** — no-ops the reference stores happily.
      Deltas take the unit scale but **not** the ground offset — a delta is a
      displacement, not a point, so adding the offset would shift the body once
      per active target.
      `extras.targetNames` is written: without it a DCC labels the keys
      "Key 1", "Key 2" and they are unusable.
- [x] **FBX rig + blendshape export.** Checked before promising: a probe scene
      with bones and an `aiAnimMesh` round-tripped through assimp's FBX writer,
      and Blender read both back. Only then was it built.
      Blender confirms the real export: **163 bones, 21,833/21,833 skinned,
      3 shape keys**, and the moved-vertex counts (2200 / 5865 / 294) match
      `morphed.glb` **exactly** — two independent formats agreeing.
      139 vertex groups, not 163: only 139 bones carry weight in the `.mhw`, and
      both assimp and Blender drop the empty ones.
      **Every `aiBone` needs a NODE of the same name**, or the writer fails with
      `Failed to find node for bone: root`. The bone array is not a skeleton;
      the hierarchy lives in the nodes.
      `aiAnimMesh` holds **absolute positions**, not deltas — sending deltas
      collapses the model toward the origin when a key is enabled.
- [x] **Sparse morph accessors — a dense target cost the same whatever it moved.**
      A morph target is a delta per RENDER vertex and almost all of them are
      zero: `nose/nose-base-up.target` moves **305 of 19,158** mesh vertices,
      1.6%. Written densely every target cost the same **261,996 bytes**, and
      the three-target fixture GLB was 1,930,380 bytes with **785,988 of it —
      41% — morph deltas that were overwhelmingly zero.**
      Not merely wasteful: it is what made exporting the modifier set
      impossible. 1,280 targets dense is **335 MB**.
      glTF's sparse accessor stores an index list plus a value list and takes
      the base as zero — exactly what an unmoved vertex means. Chosen **per
      target**, because a target that moves most of the mesh is genuinely
      cheaper dense (16 bytes a vertex against 12).

      | | morph bytes | file |
      |---|---|---|
      | dense | 785,988 (3 x 261,996) | 1,930,380 |
      | sparse | 133,744 (35,200 + 93,840 + 4,704) | 1,278,576 |

      **−83% of the morph payload, −34% of the file.**
      **Blender 5.2 reports the two files identical**: shape keys `head-oval`
      2200 / `head-trans-backward` 5865 / `nose-base-up` 294 moved vertices,
      max deltas matching to six decimals. The unit test also decodes the sparse
      encoding back out of the BIN chunk and compares delta for delta — a wrong
      index mapping shrinks the file just as well and writes a valid GLB that
      moves the wrong vertices.
      **A test that looked right and was not.** `min`/`max` must describe the
      accessor's *effective* values, zeros included, or glTF-Validator reports
      `ACCESSOR_MIN_MISMATCH`. Asserting "the bounds straddle zero" on
      nose-base-up proves nothing: its deltas already go both ways on every
      axis, and commenting out the fold left every test green. It took a
      **one-directional** morph — ten vertices moving only +y — to separate
      them.
      Not done, and it is the interesting part: **nothing in the application
      exports morph targets yet.** Only `mh_export_fixture` does.
- [ ] Texture packing (ORM), GLB embedding, KTX2/Basis, optional Draco
- [x] **Unit-correctness at dm/m/cm/inch, for every writer.** Each height is
      measured back out of the file the writer produced, not taken from its
      return value: OBJ from its `v` lines, glTF from the POSITION min/max, USD
      from `extent`, FBX by re-importing.
      Plus the property a user actually depends on: **all writers agree at the
      same unit**, so the same character is the same size in two tools.
      This is the reference's documented defect — `fbx_binary.py:736` hardcodes
      `scale_factor = 10.0` with the correct `10.0/config.scale` commented out
      one line above, so only its decimetre case is right.
      Checked the test is not self-cancelling: assimp does **not** normalise
      units on FBX import, so dm and cm really do come back 16.9455 and
      169.455 — ratio exactly 10.0.
- [x] `docs/formats/*.md` — **8 documents**: the seven native formats plus a
      cross-format conventions page. Every Python citation was checked to
      resolve (14/14) and every repo path to exist (17/17); one misquoted
      example was corrected against the real file.
      `.mhpose` is documented as **`bvh.md`** because there are no `.mhpose`
      files in `data/` at all — the shipped pose data is BVH, and documenting a
      format nothing uses would have been fiction.
      The conventions table is the point: UV origin, up axis, matrix order and
      units for every format we touch, with what each disagreement actually
      cost (glTF's flipped V, USD's un-flipped V, BVH's unrecorded up axis,
      Blender's Z-up world).
- [x] **Malformed-input sweep — 11 readers, one corpus, and it found nothing.**
      Reported as the null result it is: every reader already survived, in debug
      and under **ASan**. What the sweep buys is that it stays true.
      Corpus is derived from **real shipped files**, not invented junk — junk
      bounces off the first `if` in a parser, a file that is valid for a hundred
      thousand lines and then is not gets deep into the state machine. Mutants:
      unmodified (the control), empty, truncated at 1/10/50/99%, valid-prefix +
      0xFF, valid-prefix + NUL, every digit turned to 9 (so every count and
      index is enormous), and 8 × 32 scattered byte flips from a fixed
      xorshift32 seed.
      Readers: `.obj`, `.mhclo`, `.mhmat`, `.mhm`, `.target`, `.mhskel`, `.mhw`,
      `.bvh`, sliders JSON, poseunits JSON, and **`.glb` through assimp** — the
      one parser we do not own and the one a user is most likely to point at an
      untrusted file. It survived too.
      **Two flaws found in the test itself, both by mutation:**
      1. The 256 KB sample cap made the OBJ corpus **vacuous**. An OBJ is
         *sectioned* — every `v`, then every `vt`, then every `f` — so 256 KB of
         the 1.7 MB `base.obj` held **no face lines at all**. Removing *both* of
         the codebase's vertex-index bounds checks (`ObjReader.cpp:75` and
         `Mesh.cpp:61`) did not fail the sweep. Fixed by using the 6 KB
         `axis.obj`, and guarded by a per-reader `mustContain` so a cap can
         never silently truncate the interesting records again.
      2. There was **no unmodified control**, so "nothing crashed" was satisfied
         just as well by a reader that rejects everything — which is exactly
         what the OBJ reader does to all 14 mutants.
      The codebase's own guard is fine: the mutation *is* caught by
      `test_obj_reader.cpp:109`. The sweep is defence in depth, not the gate.
- [x] **Round-trip tests for every readable format — and Collada was lying about
      its unit.** `tests/golden/test_roundtrip.cpp`: FBX, DAE, STL, 3MF, GLB and
      OBJ exported and read back, asserting the CHARACTER survives — same
      triangles, same real-world size. Vertex counts deliberately not compared:
      importers weld and split differently, and demanding equality would pin
      whichever behaviour assimp happens to have. USD has no reader here at all,
      so `usdchecker` and Blender remain its check.
      **The finding**: assimp's Collada exporter writes
      `<unit name="meter" meter="1"/>` **unconditionally**, while we hand it
      centimetre-scaled coordinates — so the file declared a head vertex at
      `155.593674` to be **155 metres** up. A spec-conforming consumer reads the
      character as 155 m tall. The same 100x class as the reference's FBX
      defect, in a file we produce, and worse than declaring nothing.
      Fixed by rewriting the element to the unit actually used, not by forcing
      metres — forcing would make Collada the one format that ignores the
      caller's `unit`, the per-format exception this milestone keeps paying for.
      **And it made the import contract truer**: assimp's Collada reader APPLIES
      the declaration, so a DAE now comes back in metres whatever it was written
      in — measured at the metre, decimetre and centimetre, all importing at
      1.69455 — and `importScene` reports `metersPerUnit = 1.0` for `.dae`
      instead of 0.
      **Two of my own claims corrected by checking**: I wrote that 3MF's
      importer rejects what assimp writes (it round-trips fine, so it is in the
      table) and that the masked OBJ should round-trip to `Mesh::heightCm`
      (it round-trips to the VISIBLE height, 166.589 cm against 169.455 — the
      helper cage again).

## M8 — Application shell (`mh-app`, `mh-ui`)

- [x] **What a character was WEARING was not saved.** Measured: saved wearing
      **Low-Poly**, reopened wearing **HighPolyEyes**; saved wearing **none**,
      reopened wearing eyes. Silent substitution by the chooser's default, not
      an error.
      Two causes, both the session's usual shape:
      1. **`--save` ran BEFORE the choosers**, so it could not know what was
         worn. Moved after them.
      2. **Nothing wrote or read the proxy line.** `AssetIndex::findByUuid`
         exists precisely to resolve it and had **no caller in `src/`**.
      Format from the reference (`apps/gui/proxychooser.py:554-556`):
      `<slot> <name> <uuid>`, saved after the modifiers where its save handlers
      run. Resolution is by **UUID** — `:550-552` refuses a filename outright
      ("Loading proxies from filename is no longer supported"), and a
      two-token line is now reported the same way rather than guessed at.
      Precedence, and each half is tested: an explicit `--eyes` beats the file,
      the file beats the chooser's default.
      Carried in `MhmFile::unhandled` rather than a new typed field: the
      writer already emits those lines in the right place, so the byte-exact
      `.mhm` round-trip fixture is untouched. `recordProxy` **replaces** rather
      than appends, or a document loaded with one selection and saved with
      another would carry both.
      **A file with no `eyes` line still gets the default, and that is
      reference behaviour**, not a gap: the reference's chooser selects
      high-poly on reset and only overrides it when a line exists, so "wearing
      none" is not expressible upstream either.
      Mutation-verified: ignoring the document's line fails `app_reload_eyes`.
      Low-Poly is 96 verts against High-Poly's 1,064, so the two cannot be
      confused.
- [x] **The rig and the pose were lost the same way.** Saved with
      `--rig mixamo_superset --pose tpose`, the file named **neither** and
      reopened as the 163-bone default in the rest pose. Measured after the fix:
      `skeleton mixamo_superset.mhskel` + `pose tpose` in the file, reload
      reports **179 bones**, and the arm span goes **10.516 dm → 16.863 dm** —
      the pose genuinely applies, not just the rig name.
      Lines from the reference: `skeleton <relative path>`
      (`skeletonlibrary.py:336-339`), `pose <relative path>`
      (`3_libraries_pose.py:265-268`).
      `setRigName` necessarily runs before the document loads — the flag is
      available first — so the file's choice is applied right after the load,
      and only when `--rig` was not given. Same precedence for `--pose`.
      **`--skin` is deliberately NOT saved.** The reference's line is
      `skinMaterial <path to a .mhmat>` (`3_libraries_material_chooser.py:304`),
      and our `--skin` names a **litsphere PNG**, not a material — writing it as
      `skinMaterial` would be a lie about what the value is. Blocked on the
      `--skin`/`--litsphere` naming decision already raised for the owner.
- [ ] **The other six choosers are not saved**, for want of the choosers
      themselves — `recordProxy`/`proxyFromDocument` take the slot name, so each
      is one line once its asset group exists.

- [x] **`--inspect <file>`: the application can finally read a mesh.**
      `io::importScene` is five sessions of work — multi-mesh, node transforms,
      materials, skins, a unit contract — and **nothing in the application ever
      called it**. Its only consumers were tests, so every "what does our reader
      actually see in this file?" during those five sessions was answered with a
      throwaway probe.
      Found by a systematic scan rather than by hand: of 274 names declared in
      `include/`, 57 appear at most once in `src/`. Most are accessors or Qt
      overrides; `importScene` and `writeBvh` were the two real capabilities
      with no application path.
      Prints meshes, vertices, triangles, per-mesh UVs/material/skin, and what
      the file says about its unit — `metersPerUnit` 0 stays "the format does not
      say" rather than becoming a guess. Side by side it makes the unit contract
      visible: the GLB reports `1 = 1 m` and the FBX `1 = 0.01 m`, and **both**
      report the same 1.6594 m character.
      **The GLB test could not catch a dropped conversion** — glTF is metres, so
      `height * metersPerUnit` is a no-op there, and removing the multiplication
      left it green. The FBX case is the one with teeth, and the mutation fails
      it.
- [x] **`--export x.bvh` — and it found a latent defect in `writeBvh`.**
      `rig::toBvhPose(skeleton, localPose)` builds a single-frame `BvhFile`;
      `writeBvh` could always serialise one but nothing could produce one from a
      posed character, so the app could READ a pose and never write the one it
      was showing.
      Conventions taken from the reference (`shared/bvh.py:369-428`), not
      invented: root carries `Xposition Yposition Zposition Zrotation Xrotation
      Yrotation` and every other joint `Zrotation Xrotation Yrotation`; a
      childless bone gets an `End Site` of `tail - head`; a joint's offset is
      its head relative to its parent's head. 163 bones → **212 joints**
      (49 End Sites).
      **`dummyJoints` deliberately not done.** The reference inserts a `__name`
      joint wherever a bone's head is not its parent's tail, because tools
      disagree about where a bone ends when a parent has several children
      (`bvh.py:374-387`). Omitting them is a supported reference mode
      (`dummyJoints=False`) and keeps one joint per bone, which is what lets the
      file round-trip onto the same skeleton.
- [x] **`writeBvh` wrote MOTION in array order and HIERARCHY depth-first.**
      Found by building a `BvhFile` from a skeleton instead of from a parsed
      file. `readBvh` always produces depth-first joints, so for every file the
      round-trip tests had ever seen the two orders coincided — and a
      skeleton-built file is in the skeleton's parents-first order, where they
      do not. The result parses, every joint gets three plausible angles, and
      **they belong to other joints**: measured at **0.96** on a matrix element,
      on the arms of a T-pose.
      Fixed in the writer rather than the builder: HIERARCHY and MOTION must
      agree by definition, whatever order the caller's array is in.
      Mutation-verified — reverting to array order fails the new test and leaves
      the other 11 BVH tests green, which is exactly why it survived.
      Diagnosis took four wrong hypotheses (Euler order, angle-to-channel
      mapping, up-axis, local-vs-global pose), each eliminated by measurement.
      The one that settled it: the Euler round trip is exact (1.19e-07) with no
      file in the loop, so the loss had to be in the file.
      **Blender reads it**: 163-bone armature, 40 bones posed.

- [x] **Every export from the application was a statue.** The app loaded the
      skeleton, fitted the joints to the morphed body, compiled the weights,
      reported *"clamped 3,725 of 19,158 vertices to 4 influences"*, posed the
      mesh with them — and handed **none** of it to a writer. Measured on
      `makehuman --rig mixamo_superset --pose tpose --export out.glb`:
      **`skins: 0`, `nodes: 2`, attributes `POSITION/NORMAL/TEXCOORD_0`** and
      nothing else. `app_rig_superset` passed throughout, because it only
      checked that the app *announced* the rig.
      Everything needed already existed and was tested: `buildSkinData`,
      `GltfSceneEntry::skin`, `writeGlb`'s skin path, and `mh_export_fixture`'s
      `rigged.glb` validated in Blender. Nothing connected them.
      **A second defect found doing it: `--rig` alone did nothing.**
      `loadPoseRig` returned early for `"rest"`, so a rig with no `--pose`
      loaded no skeleton at all — and the bind pose is precisely the most useful
      thing to export. The rig now loads either way.
      **Bind pose = the exported pose.** The mesh written out is the POSED one,
      so joint b's bind global is `skinning[b] * restGlobal[b]`. That makes the
      skinning matrices identity in the file and the mesh arrive exactly as the
      app draws it. Writing the REST globals instead would let a DCC apply the
      pose a second time.
      Now: **`skins: 1`, 179 joints, 181 nodes, `JOINTS_0` + `WEIGHTS_0`**.
      **Blender 5.2 confirms it**, and confirms the bind choice: armature with
      **179 bones**, body carrying an ARMATURE modifier and **179 vertex
      groups**, and the armature-evaluated mesh differing from the raw mesh by
      at most **1.2e-5** — no double transform. (The stray `Icosphere` Blender
      lists is its own bone-shape object, not ours: the GLB declares exactly
      two meshes, `body` and `eyes`.)
      **Limits, stated rather than silent**: only `.glb` carries it —
      `GltfSceneEntry` has a skin field, the assimp and USD scene entries do
      not — so every other format now *says* it is dropping the skeleton. A
      subdivided mesh is refused too: weights are per BASE vertex while a
      subdivided `vmap` indexes subdivided vertices, so `buildSkinData` would
      silently weight the wrong points.
      **Cost**: loading the rig unconditionally adds **~40 ms** to a headless
      run (0.17 s against 0.13 s). Kept, because it also removes the stall when
      the pose chooser is first used.
- [x] **Rigged export for the assimp scene path — a dressed character was a
      statue in FBX and DAE.** `exportScene(entries…)` took no skin at all while
      the single-mesh overload did, so the moment a character wore anything its
      FBX and Collada exports lost the rig — and FBX is the format a rigged
      character is usually handed over in.
      `SceneEntry::skin` now, with the same one-skinned-entry rule glTF has:
      only the body is rigged, worn proxies follow it by being re-fitted.
      **No second copy of the bone code.** `attachSkin` and `addJointNodes` are
      the single-mesh path's own blocks, lifted so both callers share them.
      Verified as a **move**, not a rewrite: the fixture's `rigged.fbx` came out
      byte-identical afterwards apart from **3 bytes** of FBX creation
      timestamp (and 2 after the ponytail pass).
      **The extraction had a real bug the test caught immediately**:
      `addJointNodes` still *replaced* `root->mChildren`, which is fine for a
      single mesh with no children and a **SIGSEGV** for a scene that already
      has one child node per mesh. It grows the array now.
      **Measured, not assumed — the two formats differ**: FBX writes all 163
      bones, Collada writes only the **139** that carry weights, because
      `default_weights.mhw` names 139 of the rig's 163 and assimp's Collada
      exporter prunes the empty ones. The test asserts the weighted set with the
      extra allowed, rather than a magic number that is right for one format.
      **Blender 5.2 on a dressed, rigged FBX from the app**: armature with
      **179 bones**, body carrying an ARMATURE modifier and **141 vertex
      groups**, eyes unskinned with 0. Exactly the intended shape.
      **USD closed the same day — and it was never the writer's fault.**
      `writeUsdaScene` has taken a `SkinView*` all along (as a parameter, bound
      to the first entry, rather than a field on `UsdSceneEntry` — my earlier
      note said otherwise and was wrong). `main.cpp` simply passed none. One
      argument, and a dressed rigged stage now emits `SkelRoot`, `def Skeleton`
      and `primvars:skel:jointIndices/jointWeights`. **`usdchecker` says
      Success!**
      **`.usdz` was implemented and unreachable.** `writeUsdzScene` existed,
      validated, tested — and no branch in the app could reach it, so the format
      an Apple or AR pipeline actually takes could not be produced. Now
      `--export x.usdz` works and **`usdchecker --arkit` passes** on a dressed,
      rigged archive.
      **OBJ is the only format left with no skeleton**, because it has no
      concept of one, and it says so.
      The GLB/USDA/USDZ file checks share one `tests/file_contains.cmake`. It
      greps a USDZ directly: the stage inside is STORED uncompressed, which is a
      format requirement rather than luck.
- [x] **A third of every export was vertices nothing referenced.**
      `RenderMesh::setFaceMask` filters the INDEX buffer and leaves the vertex
      buffer alone — right for the renderer, which uploads it once and must keep
      mask toggles cheap, and wrong for a file. Export inherited it. Measured on
      a default character's GLB: **21,833 body vertices, 14,517 referenced,
      7,316 dead — 33.5%** of every position, normal, UV, tangent, joint and
      weight written.
      **Not only size.** A consumer that bounds the vertex buffer sees the
      hidden helper cages: Blender read the exported body as **1.6940 m** where
      the visible mesh is **1.6594 m**, a 2% error inherited by anything that
      frames or scales by bounds. That number is also why the earlier
      "Blender reads our FBX at 1.694 m" note in §M7 was measuring the buffer,
      not the character.
      `io::compactUnusedVertices` + `compactSkinAttributes`, called once at the
      app's export site. **Deliberately not inside the writers**: a writer
      renumbering vertices behind a caller's back would break anyone
      round-tripping indices, and the tests asserting
      `result.vertices == view.vertexCount()` are that caller.

      | | before | after | |
      |---|---|---|---|
      | GLB | 2,255,576 | 1,845,876 | **−18.2%** |
      | FBX | 4,529,552 | 4,154,624 | **−8.3%** |
      | USD | 3,833,150 | 2,831,381 | **−26.1%** |

      Blender now reads the FBX body at **1.6594 m** with **14,517** vertices,
      and the rigged GLB still binds exactly: 179 bones, 179 vertex groups, max
      shift **1.2e-5**. (The FBX's vertex-group count drops 141 → 126: bones
      that only weighted helper vertices now weight nothing, so Blender makes no
      group for them. The bones are all still there.)
- [x] **OBJ was worse than wasteful: our own reader refused our own export.**
      Same dead weight, different mechanism — **20,222 `v` lines of which 14,444
      were referenced**, 5,778 dead (28.6%), plus 22,142 `vt` of which 15,325.
      The face mask skips FACES while the vertex and UV lists were written whole.
      The file-size half is real (**17.9% smaller**, `v` 20,222 → 14,444,
      `vt` 22,142 → 15,325) but secondary. `loadObj` rejects a vertex no face
      references (`ObjErrorKind::LooseVertex`), so `makehuman --export x.obj`
      produced a file the application **could not reopen**:

          vertex referenced by no face (vertex 13380)

      Measured both ways: the pre-fix file fails to load, the post-fix one loads
      with 14,444 vertices. That round trip is now a test, and it is the one
      that turns "wasteful" into "wrong".
      Compaction lives in `writeObjScene` rather than `io::compactUnusedVertices`
      because OBJ indexes vertices and UVs separately, in mesh space, with its
      own face mask — a different index space from the render-vertex path.
      **Blender is unaffected either way**: its OBJ importer already discarded
      the unreferenced vertices, reading 14,444 and identical dimensions before
      and after. Only our own stricter reader noticed.
- [x] **USD wrote a skeleton TEN TIMES the size of the body.** `points` are
      multiplied by the unit scale; `bindTransforms` and `restTransforms`
      emitted `globalRest` verbatim. At the default unit — metre, s = 0.1 — the
      mesh came out in metres and the rig in decimetres. Our own instance of the
      defect class recorded against the reference's FBX (§8).
      **Measured in Blender on our own export**: mesh z **−0.8178..0.8416**,
      armature z **−8.0385..8.4481**. After: armature **−0.8038..0.8448**, inside
      the body where it belongs.
      **`usdchecker --arkit` passed the whole time**, before and after. It
      validates the stage's structure, not whether the rig fits the mesh — worth
      remembering, because that validator has been the trusted oracle for USD in
      three earlier sessions.
      **The existing test was asserting the defect.** `test_usd_writer.cpp`
      pinned the root bind translation as the literal `(0, 0.5639, -0.7609, 1)`
      — the *unscaled* value. It is derived from `unitScale(Unit::Meter)` now:
      a literal there is precisely how the bug hid.
      The new check is containment rather than a scale factor: every joint's
      bind translation must lie inside the mesh's own declared `extent`. 155 of
      163 were outside before; a 10x rig fails by an order of magnitude and the
      property survives a change of unit.
- [x] **Every export put the character half underground.** Measured in Blender:
      feet at **−0.82 m**, head at **+0.84 m**, origin at hip height, in OBJ,
      GLB, FBX and USD alike — a character imported anywhere arrived buried to
      the waist. The reference defaults **"Feet on ground" to True** for every
      one of its exporters (`legacy/python/core/export.py:58`); our app passed
      `{}` so it was false everywhere, and `UsdWriteOptions` had no such field
      at all — the "one exporter that cannot be set like the rest" its own
      `unit` comment says M7 exists to remove.
      Added to USD (points, `extent`, and the skeleton's bind/rest transforms),
      and defaulted on for every format in the app. **All four now land at
      0.0000** and the USD armature moves with the body: mesh z 0.0000..1.6594,
      armature 0.0139..1.6626, `usdchecker --arkit` Success.
- [x] **Two correct features were wrong together.** Compaction drops vertices no
      surviving face names; `feetOnGround` levels by the lowest point. Taking
      the offset over ALL vertices means levelling by one that is then
      dropped — and the body's helper cage reaches below the visible feet, so
      the OBJ came out **floating 0.27 m above the ground** with both changes
      in and each passing its own tests. Caught by measuring the finished file
      in Blender, not by either test suite.
      `writeObjScene` computes its reachability pre-pass **before** the ground
      offset now, and the offset is taken only over vertices that will be
      written.
- [x] **Each preset's tests now own their temp directory.** ~80 fixed names
      (`mh_units_agree.obj`, `mh_fuzz_obj`, `mh_glb_*.glb`) live under
      `temp_directory_path()` through a dozen per-file helpers, so two ctest
      runs of the same binary share them — an ASan and a background TSan run
      collided exactly that way.
      Renaming 80 call sites is the large fix; `TMPDIR` is the small one.
      `std::filesystem::temp_directory_path()` honours it, so one
      `catch_discover_tests(... ENVIRONMENT "TMPDIR=...")` per target makes
      every existing name unique per preset without touching a single test.
      **Verified on the mechanism, not on a repro.** After a full debug run the
      system temp's `mh_*` count is **unchanged (15 before, 15 after)** while
      **11** files land in `build/macos-arm64-debug/tests/tmp/` — the suite no
      longer writes to the shared location, so two presets cannot collide.
      **The original flake was NOT reproduced.** Five rounds of the offending
      test in both presets at once passed with the fix *and* passed without it
      as a control, so this rests on the identified mechanism rather than a
      demonstrated failure. The one observed failure remains a single
      observation.
- [x] **The application exports blendshapes** (`--blendshapes`, 2026-09-02).
      `writeGlb` had taken morph targets all along and `main.cpp` passed none --
      the same built-and-never-wired pattern as `visibleVertexMask`,
      `writeUsdzScene` and the rest.
      **34, not the 102 files on disk.** The files under
      `data/targets/expression/units/` are **34 units x 3 ethnicities**, so each
      shape key is the ethnic blend
      `SUM over race of factors.value(race) * delta(race)` -- ordinary
      `targetWeight` (`humanmodifier.py:644-652`) on a group whose only macro
      dependency is race. The three weights sum to 1, so no renormalisation.
      Verified independently: `tests/golden/target_groups.txt` already recorded
      34 `expression-units-*` groups of 3 components each.
      `core::buildExpressionBlendshapes` (`include/makehuman/core/Blendshape.h`)
      + `io::compactDeltas`, which `Compact.h` had explicitly deferred until
      something exported morph targets. It now has a caller.
      **GLB only.** `GltfSceneEntry` has a `morphTargets` field and
      `io::SceneEntry` does not, so FBX/USD/OBJ print what they are dropping
      rather than writing an expressionless mesh in silence.
      **Refused with `--subdivided`**: targets index the base mesh, and a
      subdivided vmap names vertices past its end (`Target.cpp:203` rejects it,
      so the result is empty rather than wrong). Tested both ways.
      Cost: **+10 ms, +210 KB** for 34 targets (dense would be 5.9 MB -- the
      sparse accessors are what make the set affordable).
      Blender, independently: **34 shape keys, all 34 deforming**, left/right
      pairs agreeing where the data is symmetric. Pinned per key in
      `tools/blender_check.py`; validation now **8/8**.
      **Two tests passed on their own mutation before this shipped**, both
      fixed: (a) an identity remap in `compactDeltas` is indistinguishable on
      the shipped mesh, where every dropped vertex sits at the END -- caught
      only by a synthetic mesh dropping from the middle; (b) a sort I added was
      dead code, because `groupNames()` already sorts (`TargetIndex.cpp:121`).
- [x] **FBX and Collada carry blendshapes too** (2026-09-02). `io::SceneEntry`
      gained a `morphTargets` span and the multi-mesh `exportScene` attaches
      them, so `--blendshapes --export x.fbx` works for a **dressed** character
      -- previously the scene overload had no morph field at all, and the
      single-mesh overload that did was unreachable the moment anything was worn.
      One entry may carry morphs, exactly as one may carry the skin: a worn
      proxy is re-fitted to the body, not blended. A second set is refused, and
      so is a delta array that is not parallel to its entry's mesh.
      The `aiAnimMesh` writer was extracted from the single-mesh path into
      `attachMorphs`, so both paths share it rather than diverging.
      **Blender, independently: 9/9 exports agree.** `expressions.fbx` matches
      `expressions.glb` **key-for-key on all 34** -- our glTF writer and
      assimp's FBX writer are independent implementations, so their agreeing is
      a stronger statement than either matching an expectation. The body carries
      34 deforming keys and the worn eyes carry none.
- [x] **USD carries blendshapes too** (2026-09-02) -- UsdSkel `BlendShape`
      child prims, `uniform token[] skel:blendShapes` and
      `rel skel:blendShapeTargets`. Sparse via `pointIndices`, the same reason
      glTF uses sparse accessors. `.usda`, `.usd` and `.usdz` all carry them;
      **OBJ is now the only format that says it drops them**, and its format has
      no blendshape channel at all.
      **The trap, found by probing before writing anything:** `usdchecker`
      REJECTS `SkelBindingAPI` on a prim not rooted at a `SkelRoot` -- *"as
      required by the UsdSkel schema"* -- **even with no skeleton and only blend
      shapes**. Blender imports that same invalid stage happily, shape key and
      all. So the root becomes a `SkelRoot` whenever anything is bound. Blender
      alone would have shipped a stage Apple's validator rejects; this is the
      second time its lenience has hidden a defect (the first was loose vertices
      in its OBJ importer).
      Prim names take the one substitution USD forces -- an identifier cannot
      hold a hyphen, so `eye-left-closure` becomes `eye_left_closure`.
      **`usdchecker` is now in the automated gate** (`app_blendshapes_usda_valid`,
      guarded on `find_program`), and `run_blender_validation.sh` runs it on both
      stages. The **no-skeleton** case -- which the app cannot produce, because
      it always builds a rig -- is covered by `expressions.usda` in
      `mh_export_fixture`; verified that the harness fails when the SkelRoot
      decision is mutated back.
      Blender: **10/10 exports agree**, and all three formats match **key for
      key on all 34** once the `-`/`_` substitution is applied. Cost: 0.21 s,
      3.2 MB (text; the GLB is 2.3 MB).

- [~] `QMainWindow` + `QDockWidget` — two docks, left/right areas, object names
      set so `saveState` actually restores. Nested and tabbed docking not yet.
- [x] **Task-view registry + modelling sliders** — `core::loadSliderLayout` ports
      `modifiers/*_sliders.json`, the reference's own tab definition: **7 task
      views, 50 sections, 291 sliders**, full parity on order, labels, ranges,
      defaults and camera hints (`tests/golden/slider_layout/`).
      Labels follow `modifierslider.py:46-56` where the file gives none.
      `ui::ModifierPanel` renders them from plain `foundation::TaskViewSpec`, so
      **`mh_ui` still has zero `mh::core` symbols** while driving 291 modifiers.
      App: `--set <modifier>=<value>` (repeatable) for scripted render/export.
      **Order trap:** `applyStack` resets the mesh to its morph base, so posing
      must come *after* morphing or the pose is discarded on every slider move;
      the rig is also re-fitted each time, since a morphed body moves its joints.
      Verified in Blender: default 16.94 dm, Gender=1 17.67 dm, Age=0 6.33 dm
      (a 1-year-old, ~63 cm), and Gender=1 + T-pose keeps the height while the
      span goes 11.35 -> 18.43 dm, so morph and pose compose.
- [x] **Materials dock: skin and pose pickers** — `ui::AssetPanel`, fed plain
      `foundation::AssetGroup`s scanned from `data/litspheres` and `data/poses`,
      so an asset dropped in appears without a code change. `--skin` and
      `--pose` drive the same state headlessly.
      Litspheres verified to render differently: mean luminance **152.3
      (caucasian) / 125.6 (african) / 148.4 (asian)** at identical coverage.
      **The trap:** `loadPoseRig` fits the skeleton to whatever the mesh holds,
      and the mesh is left *posed* after every rebuild — so switching pose to
      pose conjugated into the previous pose's rest frame. Measured **33 cm max
      / 8 cm mean** error over all 19,158 vertices. Always reset to the morph
      base first; pinned by `tests/golden/test_body_pose.cpp`.
- [x] **`.mhm` save, and load/save in the UI** — `core::saveMhm` +
      `mhmFromHuman`, File menu with the platform Open/Save/Save As shortcuts,
      and `--load` / `--save` for scripted use.
      **Byte-parity against the reference's own `Human.save()`**
      (`tests/golden/mhm/reference_save.mhm`, captured by
      `capture_fixture.py mhm_save`), covering uuid, tags, camera, plugin lines
      and `subdivide`.
      **Two traps:** saving from a fresh `MhmFile` silently drops uuid, tags,
      camera and every plugin line the loader kept — open a rigged, clothed
      character, press Save, get back a naked unrigged body. And `applyMhm` does
      not reset, so a second load blends with the first (`human.py:1486`).
      Both fixed and pinned.
- [x] **`.mhm` camera and `subdivide` are now honoured, not just stored.**
      `mhmCameraFrom` / `orbitFromMhmCamera` map the format's *magnification*
      (`lib/camera.py:454-457`: 0.25..15, default 1.0) onto this renderer's
      orbit distance by `zoom = 45 / distance` — **a convention anchored on the
      defaults, not a measurement**, since the two cameras do not share a
      projection. Restored views are clamped to the viewport's own limits:
      MakeHuman's max zoom of 15 maps to distance 3, which is inside the head.
      `--subdivide` and the `.mhm` flag both drive Catmull-Clark: 19,158 → 75,008
      verts, 18,486 → 73,944 faces.
      **A headless save leaves the camera line byte-for-byte as loaded** — the
      format holds doubles and this camera is float, so passing a view through
      unconditionally rewrote `-13.399999999999999` as `-13.399999618530273` on
      every save, for a framing nobody touched.
- [x] **Camera pan** (2026-09-02). The viewport could orbit and zoom but never
      move the model off centre, and the `.mhm` camera line's translation (slots
      2..4) was written as zeros and read as nothing.
      **Middle drag pans, left drag still orbits.** The reference binds pan to
      the arrow keys (`core/mhmain.py:178-181`), but those already orbit here —
      taking them back would remove a working control to match a convention, so
      pan went on the button every DCC uses and nothing was lost.
      Scaled by camera distance, so a drag covers the same fraction of the
      screen at any zoom — the same reason the wheel is multiplicative.
      **The format's units are not ours.** `lib/camera.py:544-546` multiplies
      the stored translation by the human's half-extents, so the file holds a
      FRACTION of the model's size, clamped to [-1, 1] (`camera.py:608-610`).
      `OrbitView::translation` stores it that way and the app converts using the
      mesh bbox; storing decimetres would write a file MakeHuman 1.x reads as a
      pan of many body-widths. Slot 4 (z) is still carried from the loaded file
      rather than zeroed — the viewport pans in x and y only.
      The sign is **negated on purpose**: the reference pans by moving the
      camera's centre, so +x aims right and the model appears to move left,
      while our pan translates the view. Reasoned from `camera.py:544`, **not
      measured against a running MakeHuman 1.x** — the same standing as the zoom
      mapping.
      **Five mutations, and two of them initially survived** — recorded because
      the gap they exposed is this project's recurring one:
      (a) making the renderer ignore `panX`/`panY` entirely left all 493 tests
      green, because the viewport tests checked that dragging changes the
      *camera* and nothing checked that the camera changes the *picture*;
      (b) wiring pan into the model matrix instead of the view passed too, since
      at the default yaw the two axes coincide. Both are now covered by render
      tests, the second with the camera turned 90°.
- [x] **Nested and tabbed docking with drop indicators** — `setDockNestingEnabled`
      plus `AllowNestedDocks | AllowTabbedDocks | GroupedDragging`. Without
      nesting a dock can only sit in one of the four areas, so there is nothing
      to snap into.
- [x] **Workspaces: 4 presets, ⌘1-⌘4, Save As, versioned schema** (`design.md`
      §6.4). Named layouts are JSON at
      `~/Library/Application Support/.../workspaces/<name>.json`: schema version
      plus base64 `saveState`/`saveGeometry`.
      **Two persistence mechanisms on purpose:** QSettings holds *the last
      layout* for session restore; the JSON files hold *named* workspaces.
      Rigging and Export differ from Modelling only by hiding docks whose panels
      do not exist yet — shipped anyway so the switcher, shortcuts and file
      format are exercised by four real entries.
      Verified by measuring the viewport: 2,340,644 -> 2,779,708 -> 3,957,760 px
      as presets hide panels.
- [x] Six-dot panel menu (float/dock/tab/reset/close) — `design.md` §6.3.
      `PanelTitleBar` replaces the whole dock title bar; Qt offers no hook for
      extra title-bar buttons. Areas the dock disallows are **not** offered —
      a menu entry that silently does nothing is worse than none.
      **Duplicate** is not implemented: it needs a panel factory that does not
      exist yet. Everything else in the §6.3 sketch is there.
- [~] Workspaces: save / restore / reset done (QSettings **IniFormat** — the macOS
      native backend ignores `setPath`, so a test could not redirect it away from the
      developer's real preferences). 4 shipped presets and a versioned schema not yet.
- [x] **Assets in place and loaded**: 57 Lucide icons (ISC, normalised to the 1.5 px
      stroke `design.md` §4 specifies) and 42dot Sans variable (OFL-1.1,
      structurally validated). See `resources/README.md`.
- [x] Dark theme from the token table — `ui::theme::Palette` is the 21-token
      table; `styleSheet()` is generated from it and a test asserts **every**
      `#rrggbb` in the sheet is a palette entry, so a hand-typed colour fails
      the build. Contrast ratios are computed from the tokens and checked
      against WCAG, which is how `design.md`'s overstated figures were caught.
- [x] `QRhiWidget` viewport with the documented navigation bindings — Metal, 4x MSAA,
      orbit clamped +/-89 deg, multiplicative dolly clamped 5-300. Verified by
      `makehuman --screenshot`, which exits non-zero on a blank frame.
- [x] **Task registry replacing filename-sort plugin ordering** — `ui::TaskRegistry`.
      The reference derives a plugin's category *and* its position from its file
      name (`core/mhmain.py:562` sorts on it); the registry declares both.
      `MainWindow` builds one dock per registered category instead of hardcoding
      two.
      **Deliberately minimal**: a first draft carried a per-task rank, an icon
      name and a `tasks(category)` accessor, none of which had a single
      production reader. They come back with the first view that needs them.
      `setPanel` returns `[[nodiscard]] bool` and does **not** take ownership on
      failure: a mistyped category used to delete the panel while the caller
      went on connecting signals to it.
- [ ] Symbolic shortcut/mouse persistence (not raw Qt enum ints)
- [~] **Task views inventoried and classified** — `memory/taskviews.md`,
      re-derived by `tools/audit_taskviews.py` in CI. **51, not 50**: an AST
      audit found six views a regex could not see, five of them real tabs
      (Load, Skin/Material, Pose, Skeleton, Expressions).
      7 done · 2 covered by the File menu · 17 to port · 9 blocked on engine
      capability · 16 declined as Python-runtime or dev-only tooling.
- [x] **Multi-mesh rendering** — `SceneResources` holds a `Drawable` per mesh
      (vertex/index buffers, litsphere texture, own `QRhiShaderResourceBindings`);
      pipeline, sampler, white diffuse stand-in and camera UBO stay shared.
      `OffscreenRenderer::render` and `ViewportWidget::setMeshes` take a
      `std::span<const MeshInstance>`; the single-mesh overload delegates.
      **Not yet wired into the app** — `main.cpp` still calls `setMesh` with the
      body alone. The proxy choosers need that wiring plus per-mesh diffuse
      textures; see below.
- [x] **The app wears proxies** — `WornProxy` (fitting data + its own mesh +
      render buffers); `rebuildInto` re-fits every worn proxy against the posed,
      morphed base mesh and hands the viewport `body + proxies` via `setMeshes`.
      **Eyes is the first working chooser**, with `--eyes` beside `--skin`
      /`--pose`. Skin now rebuilds instead of calling `setLitsphere`, because
      the body's material travels with its `MeshInstance`.
      Verified by running the app: `--eyes high-poly` vs `none` differs in 1,329
      pixels; coverage is identical because the eyes sit inside the face
      silhouette, so the check is luminance range (29..212 -> 21..227).
      Pinned by the new `app_screenshot` ctest — `app_smoke` returns at
      `--export` before the asset groups exist and never reaches this wiring.
- [ ] The remaining seven proxy choosers (clothes, hair, teeth, tongue,
      eyebrows, eyelashes, generic proxy). The machinery is in place; each needs
      its asset group and a litsphere/material choice.
- [x] **Proxy `delete_verts` now reach the body — and the OBJ export was
      leaking the helper cages.** `visibleVertexMask` and
      `Mesh::faceMaskForVisibleVertices` existed and were tested, but
      **`visibleVertexMask` had no caller anywhere in `src/`**. Wiring it up
      surfaced a second, larger defect measured on shipped assets:
      `--export x.obj` wrote **18,486** body faces where `--export x.glb` of the
      same character wrote **13,378**. Every format takes its geometry from the
      masked `RenderMesh`; OBJ alone wrote the `Mesh` directly with an empty
      mask, so it was the one export shipping the **5,108** `joint-*`/`helper-*`
      faces — a figure in a solid skirt with a box over its face.
      Both halves are now one function, `mh::core::bodyFaceMask(base, shown,
      worn)` (`src/core/Proxy.cpp:382`): group visibility **AND** what the worn
      proxies delete. It is the single answer to "which body faces exist", used
      by the viewport, by OBJ, and — through `RenderMesh` — by every other
      writer.
      **Subdivision**: `delete_verts` index the base mesh, so the mask is
      computed there and expanded 4:1 (child `f*4+k` comes from parent `f` and
      inherits its group, `Subdivider.cpp:258-277`). The expansion is checked
      against an **independent** derivation — the subdivided mesh's own
      `staticFaceMask`, computed from inherited groups with no knowledge of the
      layout — so agreement is not self-confirming.
      **Mutation-verified four ways**: `&`→`|` fails 3 of the 4 unit tests;
      breaking the 4:1 map fails the subdivision one; dropping the OBJ mask
      argument does not even compile (`-Werror,-Wunused-parameter`); passing an
      empty span leaves `app_smoke` **passing** and fails `app_smoke_obj_faces`
      at 19,506 vs 14,398 — which is exactly why the file is read and not just
      the app's own announcement believed.
      Still a no-op for `delete_verts` specifically: all four shipped
      `.mhclo`/`.proxy` files declare zero, so that half stays covered by
      synthetic proxies only.
- [~] **Export includes worn proxies — `.obj` only so far.**
      `io::writeObjScene` writes several meshes into one OBJ, each its own named
      `g` group, with file-global indices offset per entry; `writeObj` is now a
      wrapper over it, byte-identical for one entry. The app builds its asset
      groups **before** the `--export` branch, which is what let export see what
      the character wears at all.
      Verified: `--eyes high-poly` exports 20,222 verts / 2 groups against
      19,158 / 1 for `--eyes none`, and assimp reads back 2 meshes.
      **Note:** an undressed `--export foo.obj` now writes `g body` where it
      wrote `g mesh`.
- [x] **Multi-mesh glTF** — `io::writeGlbScene`, one mesh + node per entry with
      its own accessor block; `writeGlb` is a wrapper over it and was verified
      **byte-identical** (same SHA-256 for a plain and a rigged T-pose export).
      At most one entry may carry a skin; joint nodes follow the mesh nodes at
      `entries.size() + jointIndex`. Verified with assimp: a skinned body beside
      an unskinned proxy gives bones on mesh 0 only, all resolving to real nodes.
- [x] **Multi-mesh FBX / Collada / STL / 3MF** — `io::SceneEntry` +
      `exportScene(span)`. assimp's `aiScene` holds many meshes natively, so no
      index arithmetic was needed; extracting `fillMesh`/`fillMaterial` left
      `SceneIO.cpp` **smaller than before** (537 lines, was 593).
      **Each mesh gets its own child node.** Not cosmetic: assimp's FBX exporter
      names a mesh after its owning node, so hanging them all off the root made
      both meshes `body` — geometry and materials survived, identity did not,
      and Blender merged them into one object. Only caught by checking with a
      *second* importer.
      Side effect accepted: Collada dedupes a mesh whose name matches its node's
      and emits `body_1`. FBX matters more for DCC round-tripping, and identity
      is still preserved, so tests assert by prefix.
- [x] **Multi-mesh USD** — `io::UsdSceneEntry` + `writeUsdaScene`, one `Mesh`
      prim per entry under one `Xform`, `extent` per prim as USD expects. A
      stage is already a scene graph, so no index arithmetic was needed.
      `writeUsda` is a one-entry wrapper and its output is **byte-identical**.
      Validated two independent ways: **Pixar's `usdchecker` reports Success**
      (checked before the change too, so nothing was introduced), and Blender
      imports `body` (21,833) + `eyes` (1,076) — matching FBX and Collada.
      **Every export format now carries a dressed character**, so the
      "exports the body only" warning was deleted rather than reworded.
- [x] **USD materials** — `UsdPreviewSurface` under one `Looks` scope, bound per
      mesh, with `UsdUVTexture` + `UsdPrimvarReader_float2` for a diffuse map
      and the texture copied beside the stage (same rule as OBJ's `map_Kd`).
      A material-less scene emits **no `Looks` scope at all**, so its output is
      unchanged from before materials existed.
      **Every format now carries per-mesh materials.**
      `usdchecker` caught three things tests could not: `MissingMaterialBindingAPI`
      (a binding must *apply* the API schema), `varname` needing `string` not
      `token`, and — worst — my first fix emitting `prepend apiSchemas` as a
      *property*, which made the stage fail to open at all while every test
      still passed.
- [x] **Proxy materials.** `WornProxy` loads the `.mhmat` its proxy names; the
      body loads `data/skins/default.mhmat` (which is what the reference does,
      `apps/human.py:89`). A dressed export carries `DefaultSkin` on the body
      and `Eye_brown` on the eyes, in OBJ *and* glTF, and the `.mtl`'s texture
      is now **copied** beside the output — the writer used to name a file it
      never wrote. `.fbx`/`.dae`/`.stl`/`.3mf` get the body material too.
      All-or-nothing, because both writers refuse a partly-materialled scene;
      when it falls back it now says so instead of reporting plain success.
- [x] **glTF embeds textures.** Image bytes go into the BIN chunk with an
      `images`/`textures` pair and a `baseColorTexture` (or `normalTexture`)
      reference. Format is read from the **magic bytes**, not the extension, so
      a PNG named `.jpg` cannot produce IMAGE_MIME_TYPE_INVALID. Refused: a
      texture with no UVs to sample it (a hard validator error), an empty or
      unreadable file (`byteLength` 0 is invalid), and anything that is not PNG
      or JPEG. Shared textures embed once.
      **`MaterialDesc` gained `transparent`** — without it a texture's alpha was
      embedded and then ignored, since glTF defaults to `alphaMode: OPAQUE`. The
      shipped eyes are `opacity 1.0` + `transparent True` over an RGBA map, so
      the cornea rendered solid. They now export `alphaMode: BLEND`.
- [x] **TANGENT is exported — and `calcVertexTangents` had no caller at all.**
      Not "never written by the writer": `Mesh::calcVertexTangents()` had **no
      caller anywhere in `src/`**, so the tangent array was always empty. The
      viewport's normal-map branch had no basis to build a TBN from (session 082
      wired the vertex slot and the upload; nothing ever filled it), and no
      export carried one.
      These are the good tangents — Lengyel's method with the reference's three
      bugs fixed (`project_context.md` §8) — which is exactly why a consumer
      should get ours instead of regenerating its own.
      Now computed beside `calcNormals()` in the app (body and every worn
      proxy) and written by the glTF writer as **VEC4**: xyz plus handedness
      w = ±1.
      **The `w` is the half the reference throws away** (`cross(normal,
      tangent)` unsigned), and losing it inverts normal-map lighting on the
      mirrored half of a symmetric body. Measured in our own file: **16 of
      14,517 body vertices and 256 of 1,076 eye vertices carry −1.**
      **The first version of the test could not see that.** Asserting
      `w ∈ {+1, −1}` passes on a writer that emits +1 everywhere — verified by
      replacing the sign with a literal `1.0F`, which left all 21,833 at +1 and
      every assertion green. It asserts the sign VARIES now.
      **Cost**: `calcVertexTangents` is **0.18 ms** on the base mesh, per
      rebuild. Headless export went 0.15 s → 0.15 s, unchanged within noise.
- [x] **assimp's FBX and Collada exporters do not write tangents at all** —
      code to fill `aiMesh::mTangents`/`mBitangents` was written, measured to do
      nothing, and removed rather than left in. Grepping our own output: the FBX
      carries `LayerElementNormal` and `LayerElementUV` and no
      `LayerElementTangent`; the Collada carries `semantic="NORMAL"` and
      `"TEXCOORD"` and no `TEXTANGENT`. glTF gets tangents because that writer
      is ours.
- [ ] **Canonicalise the texture dedup key.** Dedup compares
      `std::filesystem::path` exactly, so the same file reached by two spellings
      (`a/../b.png` vs `b.png`) embeds twice. Unreachable today (one textured
      proxy); `weakly_canonical` on the key when a second one lands.
- [ ] Consider renaming `--skin` to `--litsphere` (keeping `--skin` as an
      alias). It selects a viewport matcap, not a material: `--skin african`
      still exports `DefaultSkin`, which is correct but reads as a bug. The
      reference does not have the flag at all — it derives the litsphere from
      the ethnicity modifiers (`apps/autoskinblender.py:52-60`). **User-facing
      rename, so worth asking first.**
- [x] **Alpha blending — the shader wrote the alpha and the pipeline threw it
      away.** `outColor.a = diffuse.a` has been in `litsphere.frag:120` all
      along, and `QRhiGraphicsPipeline`'s default target blend is **disabled**,
      so it reached the framebuffer and was ignored. Meanwhile the GLB export
      already wrote `alphaMode: BLEND` for the same material — the file and the
      screen disagreed.
      Second pipeline (SrcAlpha / OneMinusSrcAlpha, **depth write off, test
      on**) and a two-pass draw: opaque, then blended. A transparent surface
      drawn before the opaque geometry behind it blends against the clear colour
      instead, which reads as the transparency simply not working.
      **No back-to-front sort within the transparent set.** One shipped material
      is transparent, so the question does not arise; a sort belongs when a
      second one lands rather than as machinery nothing exercises.
      **Honest about the effect: it changes nothing visible today.** The shipped
      `brown.mhmat` is `transparent True` over an RGBA `brown_eye.png` — colour
      type 6, **13,282 of 1,048,576 texels below alpha 255, 13,238 of them fully
      clear** — but the eye mesh's UV island never reaches them, so enabling
      blending moves **0 of 1,048,576 pixels**. The fixture is synthetic for
      that reason.
      The test asserts DIRECTION, not difference: mean green over the model must
      **drop** when a half-alpha map is blended over the dark background.
      Mutation-verified: `blend.enable = false` gives 0.120844 against 0.120755
      and fails.
- [x] **Undo/redo** — `ui::ValueChangeCommand` on a `QUndoStack`, Edit menu with the
      platform ⌘Z/⇧⌘Z. Generic by design: it holds a key, two floats and a
      callback, so undo lives in Apache-2.0 `mh_ui` while the AGPL side says
      what the key means.
      A drag merges into one step via `mergeId`; `editingFinished` closes the
      group. **`QAbstractSlider::actionTriggered` fires BEFORE the value lands**,
      so closing the group there closed it one edit early and the next drag
      merged into the keyboard step — the signal is emitted after
      `valueChanged` now.
      Reset is bracketed by `resetInProgress` into one macro; it used to cost up
      to 291 presses of ⌘Z.
      Opening a document clears the stack: kept, ⌘Z wrote the *previous*
      character's values into the newly loaded one.
- [x] **Undo for pose and skin** — `ui::ChoiceChangeCommand`. Consecutive choices
      in the same group **merge**, deliberately: arrow-keying a closed combo
      emits one change *per keystroke* (measured: three Down presses give three
      `currentIndexChanged` — and `activated` behaves identically, so switching
      signals does not help), so without merging a traversal that ends where it
      started costs three undo steps and three full skeleton reloads.
      A pose that fails to load is now probed *before* the command is pushed, so
      it never becomes an undo entry that does nothing.
- [x] **Undo for workspace presets** (2026-09-02). `applyWorkspacePreset`
      rewrote the layout and pushed nothing, so ⌘1 then ⌘Z left the new layout
      in place and undid whatever slider the user had touched before it — the
      wrong thing, silently.
      `ui::LayoutChangeCommand` holds two `saveState()` blobs and a restore
      callback, knowing nothing about docks or presets. That rests on one fact
      asserted rather than assumed: **`saveState()` carries dock VISIBILITY, not
      just geometry** — if it did not, an undo would restore positions and leave
      docks hidden.
      Captured *after* every refusal path, so a preset resolving to no live dock
      still pushes nothing — the rule the pose commands already follow.
      **`saveWorkspaceAs` is deliberately NOT undoable**, and the todo item's
      framing was wrong to group them: it writes a file and changes no window
      state. "Undo" for it would mean deleting a file the user asked to save.
      Test trap recorded: `isVisible()` is false for every child of a window
      that was never shown, so the visibility assertions use `isHidden()`, which
      is the flag `setVisible` actually writes.
      Three mutations; two caught. The third — dropping a first-call guard in
      `redo()` — changed nothing, because `restoreState` is idempotent, so the
      guard was **deleted** rather than kept as unverifiable cleverness.
- [x] **Workspace presets derived from the registry.** A preset names
      *categories*, not dock object names, and `std::nullopt` means "every
      registered category" — so the first preset (Modelling) shows a category
      added later **by construction**, not because a test noticed.
      A preset that names categories but resolves to no live dock now returns
      false instead of hiding everything and reporting success: renaming a
      registered category used to give a blank window, a "Workspace: Materials"
      status message, and that empty layout saved on quit.
- [x] **Live language switching and working RTL** (2026-09-05).
      Twenty `data/languages/*.json` files have shipped all along and **nothing
      in `src/` read any of them** — no `QTranslator`, no layout direction, no
      way to switch.
      `ui::JsonTranslator` is a `QTranslator` over those files. A QTranslator
      rather than a lookup helper **because Qt already owns live switching**:
      `installTranslator` posts `QEvent::LanguageChange` to every top-level
      widget, and every `tr()` call routes there unchanged. Writing our own
      lookup would have meant touching all 38 call sites and reimplementing the
      event.
      **Context is ignored on purpose.** Qt keys translations by (class,
      source); the shipped files are one flat map. That flatness is what lets
      **data** strings translate too — and measured, that is where the text
      actually is: **135 of 192 slider labels**, 5 of 7 task views and 15 of 49
      sections have German entries, against **5 of our 38 `tr()` strings**
      (`Close`, `Redo`, `Reset`, `Save`, `Undo`). So slider captions, section
      headings, tab names and dock titles all go through
      `QCoreApplication::translate("", …)`.
      `MainWindow::setLanguage` loads the new file **before** removing the old
      one, so a failed switch leaves the running language alone instead of
      dropping the user into raw source strings. RTL comes from
      `__options__.rtl` (Arabic alone of the twenty) and is applied with
      `QApplication::setLayoutDirection` — **and reset when switching away**,
      which is what "working RTL" means rather than just "RTL".
      A Language menu lists all 19 (master is excluded — it is the English
      source list the rest are generated against), plus "English" first as the
      way back from a script you cannot read. `--language <name>` on the CLI.
      Five mutations, all caught: honour Qt's context; skip the clear on a
      failed load; let `__options__` into the string map; never reset the layout
      direction; make `retranslateUi` a no-op.
      **Recorded, not hidden**: our own menu vocabulary (`&File`, `Open…`,
      `Save As…`) is largely absent from the shipped dictionaries, which carry
      the reference's strings. Those menus stay English until someone adds
      entries — the mechanism is done, the vocabulary is data.
- [~] **Accessibility pass** (`design.md` §9) — accessible names on every control
      (a sweep test fails if a new one arrives unnamed; 14 controls covered),
      keyboard operability including **arrow-key orbiting in the viewport**
      (it had focus and no key handler, so orbiting was mouse-only), and focus
      rings that are verified by **rendering the widget with and without
      `State_HasFocus` and requiring the pixels to differ** — not by checking the
      stylesheet contains a string.
      **`QSlider:focus::handle:horizontal` is a trap:** Qt drops the `:focus`
      when it precedes a sub-element, so the ring painted on all 291 handles at
      rest and focus changed nothing. Only `QSlider:focus` works.
      **200% text**: measured, not assumed — the longest shipped caption wants
      263 px in a 380 px dock, so nothing clips. Word wrap is kept for a
      different, measured reason: it drops that caption's *minimum* from 263 to
      72 px, so a user narrowing the dock keeps a usable panel. Benefit band
      ~253-380 px.
      **Reduce motion**: `theme::reduceMotion()` reads AppKit's
      `accessibilityDisplayShouldReduceMotion` in `src/ui/Motion.mm`, the one
      Objective-C++ file. There is **no Qt API** — `QStyleHints`,
      `QAccessibilityHints` (one property, contrast) and `QPlatformTheme` all
      lack it, and `QSettings` cannot read another app's preference domain.
      Read once at construction, deliberately.
      Still open: VoiceOver on a real device.
- [ ] **Unverified:** `reduceMotion()` returning **true** has never been
      exercised — the setting is off on this machine and on CI, so only the
      false branch runs. `dockOptionsFor(true)` is tested, but the AppKit read
      itself is not. Needs one manual toggle in System Settings before shipping.
- [x] **A slider announced its tick, not its value** (2026-09-02). Measured, not
      assumed: the first shipped slider reported `text(QAccessible::Value)` ==
      **"500"** while its readout showed **"0.00"**. Qt's default for a QSlider
      is `QString::number(value())`, and ours run 0..1000 ticks whatever the
      modifier's own range is — so a screen-reader user heard a meaningless
      number that disagreed with the label beside it.
      Fixed with a `QAccessibleWidget` subclass installed via
      `QAccessible::installFactory` (`src/ui/ModifierPanel.cpp`). The factory
      claims only sliders carrying the `mh.sliderMin`/`mh.sliderMax` dynamic
      properties the panel sets, so nothing else in the app is affected.
      Dynamic properties rather than a side table **on purpose**: they die with
      the widget, so there is no pointer-keyed map to leave a stale entry that
      would hand a dangling range to the next QSlider at the same address.
      Two mutations verified (factory returns nullptr; announce the raw tick) —
      both fail 3 assertions.
- [ ] **Known gap, re-measured 2026-09-02:** the readout label beside each
      slider is still offered under its own accessible name, so the number is
      announced twice. An empty `accessibleName` does NOT suppress it — Qt falls
      back to `QLabel::text()`. Suppressing it means reporting the label's
      interface invisible, and **whether macOS then drops it cannot be
      established from the Qt API** — it needs VoiceOver on a real device. Not
      shipped rather than shipped unverifiable.

## Third-party validation (Blender) — wired in 2026-08-29

`tools/run_blender_validation.sh` — exits non-zero on disagreement. 3/3 exports
agree today (geometry, UVs, and 169.5 cm under three unit conventions).

- [x] Extended to **rigged** exports. Blender confirms 163 bones, 1 armature
      and **all 21,833 vertices skinned** — independently of our code and of the
      Python reference. 4/4 exports agree.
- [x] **Extended to a posed mesh** (2026-09-02) -- and the investigation
      reframed the item. `posed.glb` (`--pose tpose`, written by the app, not the
      fixture) now goes through the harness: **11/11 exports agree**.
      **Measured:** the geometry IS baked into the pose (POSITION bounds go from
      +-0.526 to +-0.843 in x) and the exported skeleton follows it (**41 of 163
      joint node matrices differ** from the rest export, by up to 0.62). Blender
      applying the armature moves the mesh by **9e-06** -- a no-op. The file is
      self-consistent: the posed state IS its bind pose, and the posed geometry
      matches the same pose exported to OBJ exactly (1.6863 x 0.3009 x 1.6630 m
      against 16.8628 x 3.0088 x 16.6301 dm).
      New generic check `armature_shift` in `blender_validate.py`, asserted for
      every file. It has teeth: `GltfWriter` derives node transforms and
      inverse-bind matrices from ONE scaled-global array
      (`GltfWriter.cpp:330-374`) so they cannot disagree, and breaking that
      single source (unscaled IBMs against scaled nodes) produces a shift of
      **8.38** and two FAILs. A file that fails it is double-deformed in every
      DCC while our own tests, which never apply an armature, all stay green.
      **The original goal is NOT reachable from these files.** Because a DCC's
      own skinning is a no-op on our exports, Blender cannot independently
      compute LBS from them. That needs an export carrying **rest geometry with
      a posed armature** -- an interchange-semantics decision (do exports bake,
      or ship a live rig?), not a validation gap. **Owner question.**
- [ ] **Unexplained, recorded rather than guessed:** the exported joint nodes
      follow the pose (41 of 163 differ), but the only re-fit in the export path
      is `poseInPlace` (`main.cpp:265`), which fits the skeleton to the mesh
      *before* posing it -- and deleting that re-fit changes the exported file
      not at all. So `skin->globalRest` differs between a rest and a posed run
      for a reason not yet located. **Not a defect**: the output is verified
      correct and self-consistent by Blender. But the mechanism should be
      understood before anyone edits the pose path.

## Research notes (2026-08-29) — owner asked for modern approaches

**Skinning: offer dual-quaternion alongside LBS.** LBS is notorious for the
"candy-wrapper" collapse on twisted joints, because it interpolates positions
linearly and loses volume. DQS removes that and preserves volume, but is *not*
a drop-in: it introduces joint bulging on bends. Disney shipped an enhanced DQS
on *Frozen* specifically to make it production-viable.
Plan: keep LBS as the parity path (it is what the reference does, and what glTF
and every DCC expect), and add DQS as a *display* option in M6, with the
bulging caveat measured rather than assumed.
- [ ] DQS skinning path (M6), with a side-by-side against LBS on a twisted limb

**SMPL / SMPL-X is licence-blocked for us** — see `LICENSING.md` §5.2. The full
parametric model is research-only; the CC-BY subset deliberately omits the shape
blendshapes that make it a generator. Our own 1,280 CC0 targets are the asset
base for M10. The papers are fair to learn from; the models are not.

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
