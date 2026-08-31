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
- [ ] **Visual check on the ball crease.** The ramp is a principled choice, not
      a match to any reference — MakeHuman has no ball bone to compare against.
      The weights are provably conservative and spatially coherent, but whether
      the crease *looks* right under a roll is unjudged. Blender is available.
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
- [ ] MakeHuman(163) -> Mixamo(65) retarget map. Lossy by construction; needs
      an explicit table, not a name-matching heuristic.
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
- [ ] `matPoseVerts = matPoseGlobal · inv(matRestGlobal)` (posing; needs M5 pose data)
- [ ] Joint positions from vertex clouds (rig follows the mesh)
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
- [ ] Influence clamping surfaced in the glTF exporter (JOINTS_0/WEIGHTS_0)
- [x] Euler conventions — **all 24**, plus quaternion matrix/from-matrix/
      multiply/slerp and arbitrary-axis rotation. 120 captured cases
      (24 conventions x 5 angle sets), worst matrix delta at float32 epsilon.
      `[w,x,y,z]` scalar-FIRST throughout; Eigen's `.coeffs()` is `[x,y,z,w]`,
      so any future Eigen interop must reorder.
      Licensing: `transformations.py` is **BSD-3-Clause** (C. Gohlke) with
      MakeHuman's AGPL boilerplate wrongly stamped into its docstring — see
      `LICENSING.md` §4.1. The port is BSD and lives in `mh_foundation`.
- [ ] Wire the Euler/quaternion helpers into pose loading (`.mhpose`, BVH)
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
- [ ] Body pose units (`body-poseunits.json`) — a different format: bone ->
      quaternion directly, not BVH frames
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
- [ ] Pose library UI — `--pose` is CLI only; nothing in the window selects one.
- [ ] `mixPoses` for face/foot layering
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
- [ ] BVH **export**
- [ ] Pose units: wire `face-poseunits.json` (60 units) onto the BVH frames
- [ ] **Parity fixtures**: rest matrices, skinned positions, blended expression

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
- [ ] Multi-mesh for `.usda`, `.fbx` and the assimp `.dae`/`.stl`/`.3mf` paths.
      Still single-mesh, and they say on stderr what they omit. `exportScene`
      takes one `RenderView`.
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
