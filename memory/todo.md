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
- [ ] **SonarQube is blocked on credentials.** `sonar-scanner` 8.1.0 is
      installed but fails with `You must define ... sonar.organization` and
      needs `SONAR_TOKEN`. Ask the owner for a SonarCloud token + organisation,
      or a self-hosted `SONAR_HOST_URL`.

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
- [ ] Skin normals/tangents (the w=0 direction path) — needs the renderer to
      have something to shade; positions are the parity target today.
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
- [ ] Qt RHI **swapchain** and MSAA — the interactive widget, not offscreen
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
- [ ] PBR metallic-roughness path + Blinn-Phong→PBR conversion
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

- [ ] `mh::io::Scene` intermediate representation
- [ ] One `UnitSystem`/`Transform` consumed by **every** reader and writer
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
      Shininess is deliberately **not** asserted: FBX returned a default and
      Collada a 0..128-style exponent, so one expected value would be wrong
      somewhere.
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
- [ ] glTF: embedded textures, Draco (blendshapes are done — session 029)
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
- [ ] **USDZ** — a zip of the `.usda` plus textures. Packaging, not geometry;
      needs a zip writer.
- [ ] **UsdSkel** — skeleton and skinning in USD. A bigger schema than the mesh;
      glTF and FBX already carry the rig.
- [x] **Export OBJ** (`mh::io`, Apache-2.0) — face lines byte-identical to the
      reference across all 18,486 faces; unit conversion; feet-on-ground computed
      from the mesh minimum rather than the reference's Y-only joint offset;
      face masking; `.mtl` sidecar
- [ ] Export STL, DAE, BVH
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
- [ ] Sparse morph accessors — a target touches ~2k of 19,158 vertices, so dense
      costs ~262 KB each. Fine for a handful, 335 MB for all 1,280.
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
- [ ] Round-trip + malformed-input tests for every format

## M8 — Application shell (`mh-app`, `mh-ui`)

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
- [ ] Camera *pan* has no equivalent here; the loaded translation is carried
      forward untouched rather than flattened to the origin.
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
- [ ] **Proxy `delete_verts` are not applied to the body.** `visibleVertexMask`
      and `Mesh::faceMaskForVisibleVertices` exist and are tested, but nothing
      calls them yet. A no-op today — all four shipped `.mhclo`/`.proxy` files
      declare zero `delete_verts` (verified) — and a real bug the first time a
      clothing asset expects the body hidden underneath it.
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
- [ ] **Normal maps need TANGENT.** A `normalTexture` without a `TANGENT`
      attribute makes validators warn `MESH_PRIMITIVE_GENERATED_TANGENT_SPACE`.
      `RenderView::vtang` exists but is never written. Not reachable from
      shipped data — no `.mhmat` sets a normal map — but the path is live.
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
- [ ] Per-mesh diffuse texture + alpha blending — hair and eyelash proxies need
      it. Today the shader's alpha comes from a shared 1x1 white stand-in, so
      it is always 1.0; it also needs a second pipeline (blend state) and
      back-to-front ordering, which means `setGraphicsPipeline` moves into the
      per-mesh loop.
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
- [ ] Undo for workspace changes — presets and Save As bypass the stack.
- [x] **Workspace presets derived from the registry.** A preset names
      *categories*, not dock object names, and `std::nullopt` means "every
      registered category" — so the first preset (Modelling) shows a category
      added later **by construction**, not because a test noticed.
      A preset that names categories but resolves to no live dock now returns
      false instead of hiding everything and reporting success: renaming a
      registered category used to give a blank window, a "Workspace: Materials"
      status message, and that empty layout saved on quit.
- [ ] Live language switching; **working** RTL
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
- [ ] **Known gap:** the readout label beside each slider repeats the slider's
      value, so VoiceOver announces it twice. An empty `accessibleName` does NOT
      suppress it — Qt falls back to `QLabel::text()` — so this needs a custom
      `QAccessibleInterface`.

## Third-party validation (Blender) — wired in 2026-08-29

`tools/run_blender_validation.sh` — exits non-zero on disagreement. 3/3 exports
agree today (geometry, UVs, and 169.5 cm under three unit conventions).

- [x] Extended to **rigged** exports. Blender confirms 163 bones, 1 armature
      and **all 21,833 vertices skinned** — independently of our code and of the
      Python reference. 4/4 exports agree.
- [ ] Extend it to a **posed** mesh, so LBS output is checked by a third party
      rather than only against the reference.

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
