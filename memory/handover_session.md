# Session Handover Log

Newest entry first. Every entry carries a `YYYY-MM-DD HH:MM:SS` timestamp.

---

## 2026-08-29 — Session 002 · M1 build system + M2 mesh core

**Started:** 2026-08-29 03:58:00
**Ended:** 2026-08-29 04:09:41
**Agent:** Claude Opus 5 (1M context)
**Branch:** master

### Objective
Stand up the C++ build system and deliver the first real, tested, benchmarked
module of the port.

### Delivered

**Build system (M1)**
- `CMakeLists.txt` + `CMakePresets.json`: `macos-arm64-{debug,release,asan}`
- **C++23**, not C++20 — `std::expected` is C++23. Verified available in Apple
  clang 21's libc++ before adopting. All docs claiming C++20 were corrected.
- Warning set: `-Wall -Wextra -Wpedantic -Wshadow -Wconversion -Wsign-conversion
  -Wdouble-promotion …` with **`-Werror` on**. Builds clean.
- Catch2 v3.7.1 via FetchContent, **pinned tag**, include tree marked `SYSTEM`
  so third-party warnings never surface without weakening ours.

**`mh::core` (M2, partial)**
- `Types.h` — `Vec2`/`Vec3`, cross/dot, the decimetre constant.
- `Mesh` — SoA, dual index space (`fvert`/`fuvs` independent, per module3d.py:627/:629),
  degenerate-quad triangles, face groups, adjacency, area-weighted normals,
  bounding box, `heightCm`.
- `ObjReader` — `std::expected`-based, bounds-checked. Handles `v/vt/f/g/o`,
  negative indices, leading-dot floats. Rejects loose vertices, out-of-range
  indices, n-gons, and empty meshes rather than reading out of bounds.

### Verification (all observed this session)
- **27/27 tests pass**, debug and release.
- **27/27 pass under ASan + UBSan.**
- Clean build under `-Werror`.

### Measured — release, vs. the Python baseline

| Operation | C++ | Python | Speedup |
|---|---|---|---|
| load base.obj (parse + adjacency + normals) | **5.24 ms** | 211.80 ms | **40.4x** |
| calcNormals full mesh | 0.07 ms | 5.18 ms | 74.1x |
| calcFaceNormals | 0.03 ms | 0.68 ms | 25.2x |
| calcVertexNormals | 0.04 ms | 1.69 ms | 39.8x |
| buildAdjacency | 0.14 ms | (inside the 211.8 ms) | — |

The ≤20 ms base-mesh-load success metric in `project_context.md` §6 is **met**
(5.24 ms warm; ~10.7 ms cold file cache).

### Corrections made this session
1. **`std::expected` is C++23, not C++20.** The build failed on it. Verified the
   standard level with a scratch compile before bumping, then corrected every
   doc that said C++20.
2. **I asserted 172 face groups in a parity test from a subagent's number without
   verifying it. The test failed at 139.** Root cause: `base.obj` has 172 `g`
   *statements* but only 139 *distinct* names (`helper-*-eyelashes-*` recur 9x
   each). The reference keys a dict by name and only creates a group for an
   unseen name (`wavefront.py:120-123`), so it produces 139. Confirmed by running
   the reference loader directly: 139 groups, 125 `joint-`, 13 `helper-`. The
   implementation was right; my assertion was wrong. This is exactly the failure
   `agent_profile.md` §3.3 warns about — a subagent figure used without a
   spot-check. The corrected test now asserts all three numbers.

### Ponytail review — applied
- Dropped `Eigen3::Eigen` from `mh_core`: linked but never included. Eigen is
  added when `mh_rig` needs quaternions and decomposition.
- Removed `MH_BUILD_APP` / `MH_USE_ASSIMP` options that gated nothing.
- Removed `Vec4` and `RGBA8` — declared, zero uses.
- Kept the POD `Vec3` over `Eigen::Vector3f`: SoA storage wants a
  trivially-copyable type, and the whole thing is 20 lines.

### Deferred, with reason
Empty CMake targets for `mh-foundation`/`mh-rig`/`mh-io`/`mh-render`/`mh-ui`/`mh-app`
were **not** created. Scaffolding targets with no sources is exactly the
speculative structure `philosophy.md` §2 rejects; each is added with its first
real source file.

### Next session starts here
1. Run `memory/session_start.md` in full.
2. `.clang-format` / `.clang-tidy`, then the CI workflow.
3. `tools/capture_fixture.py` — golden fixtures gate every subsequent port step.
4. Then M2 remainder: correct tangents, unweld/index buffer, Catmull-Clark.

---

## 2026-08-29 — Session 001 · Grounding, restructure, memory bootstrap

**Started:** 2026-08-29 03:30:00
**Ended:** 2026-08-29 03:57:55
**Agent:** Claude Opus 5 (1M context)
**Branch:** master

### Objective
Understand the MakeHuman codebase in depth, establish repository structure and the
`memory/` system, and set up the foundation for a Python → C++/Qt6 port.

### Completed

| Time | Work |
|---|---|
| 03:30 | Repo recon: 209 Python files, ~60k LOC, 212 MB |
| 03:35 | Verified toolchain: Qt 6.11.0, CMake 4.3.2, Ninja 1.13.2, clang 21, assimp 6.0.4, Eigen 5.0.1, TBB — all present |
| 03:36 | graphify AST extraction: 5,333 nodes / 10,108 edges |
| 03:40 | Knowledge graph complete: **5,392 nodes · 9,319 edges · 358 communities** → `graphify-out/` |
| 03:41 | **Repo restructured**: `makehuman/` → `legacy-python/`, `makehuman/data/` → `data/` (+ symlink), created `src/ include/ tests/ benchmarks/ tools/ resources/ cmake/ packaging/ docs/ third_party/ memory/` |
| 03:39 | Reference venv built: numpy 2.5.1 + PyQt5 + PyOpenGL on Python 3.14.6 |
| 03:50 | **Performance baseline measured** → `benchmarks/baseline_python.json` |
| 04:00 | 7 parallel subsystem analyses completed and spot-verified |
| 04:15 | `memory/` written — 12 documents |

### Key findings (all verified against source)

1. **Base mesh**: 19,158 verts / 18,486 **quad** faces / 21,334 UVs. Subdivided: 75,008 / 73,944.
2. **No VBOs anywhere.** `glVertexPointer(3, GL_FLOAT, 0, obj.verts)` — `legacy-python/lib/glmodule.py:479`. Fixed-function, client-side arrays, re-fed every frame. This is hotspot #1 and the reason a Metal/RHI port is transformative.
3. **Zero import capability.** No importer machinery of any kind (grep-verified). Only mesh reader is `wavefront.loadObjFile`, which ignores `vn` and `usemtl`. Import is greenfield.
4. **Zero docking infrastructure.** No `QDockWidget`/`QSplitter`/`saveState` anywhere. The dockable UI is 100% new work.
5. **FBX exporter**: hand-rolled, FBX **7300 (2013)**, no animation, no blendshapes, **verified 10× unit bug** at every scale but decimetre (`plugins/9_export_fbx/__init__.py:112` + `fbx_binary.py:736`), forged Creator string, 12+ circular imports. Treat as a format reference only, never port.
6. **Licence position resolved.** Code AGPL-3.0, assets CC0. pyFBX is GPL-2.0-**or-later** → upgradeable to GPLv3 → **compatible with AGPLv3**. An earlier automated pass flagged a conflict; that flag was wrong and has been retracted.
7. **Upstream licence contradiction found**: `algos3d.defaultTargetLicense()` (`core/algos3d.py:507-509`) claims targets are AGPL3, while `LICENSE.md` §C **and** every `.target` file header say CC0. Authoritative reading: **CC0**. Also: asset licence headers are **not machine-readable** — `updateFromComment` only accepts lowercase keys, so every shipped asset loads with the default LicenseInfo.
8. **Three tangent bugs** in `core/module3d.py` (:411 chained assignment, :429 missing `axis=`, :1212 operator precedence). Tangents in the reference are provably wrong — must NOT be parity-matched.
9. **Modifier weighting rule** established exactly: `weight = sliderValue × Π factors`, factors derived from filename tokens against a 9-category table.
10. **The mesh drives the rig**, not vice versa — joints are means of vertex clouds, so changing a slider rebuilds bone rest matrices.

### Measured baseline (macOS 26.6.2, arm64, Python 3.14.6, numpy 2.5.1)

| Operation | Median |
|---|---|
| Load base.obj | 211.8 ms |
| Catmull-Clark build | 202.3 ms |
| Load 200 targets (ASCII) | 104.4 ms (~670 ms for all 1,280) |
| Subdiv calcNormals | **20.6 ms** — exceeds a 16.6 ms frame budget on its own |
| Subdiv update_coords | 7.6 ms |
| calcNormals (base) | 5.2 ms |
| Apply 200 targets | 4.8 ms |
| updateIndexBuffer | 3.4 ms |
| Apply 1 target | 0.04 ms |

### Corrections made this session
- The graphify doc-extraction pass flagged pyFBX GPLv2 as possibly incompatible with AGPLv3. **Wrong** — the header reads "either version 2 … or (at your option) any later version". Verified at `legacy-python/licenses/pyFbx-license.txt:4-6` and retracted.
- The benchmark harness initially failed on subdivision because `Object3D.object` is a **weakref** property (`core/module3d.py:459-464`) and the stub was collected immediately. Fixed by holding a strong reference.

### Files changed
- Restructured: `makehuman/` → `legacy-python/`, `makehuman/data/` → `data/`
- Added: `memory/` (12 docs), `benchmarks/baseline_python_core.py`, `benchmarks/baseline_python.json`, `graphify-out/`, `CLAUDE.md`, `AGENT.md`, `LICENSING.md`, `.gitignore` updates
- Created empty: `src/ include/ tests/ tools/ resources/ cmake/ packaging/ docs/ third_party/`

### Blockers / open questions for the user
1. **Typeface** — instruction was "red 42 dot sans". Assumed **42dot Sans** (SIL OFL 1.1). Needs confirmation.
2. **"Open rig"** — ambiguous. Could mean the existing open `.mhskel` format, OpenSim rigs (referenced at `legacy-python/apps/compat.py:181-188` as a downloadable asset), or a specific third-party project. **Not guessing.** Does not block M1–M8.
3. **Commercial-derivative expectation** — AGPL copyleft means a closed-source fork is not possible. Output and assets are fully free (CC0). Mitigation is the Apache-2.0 clean-room module boundary (`architecture.md` §II.1). Flagged for awareness; no action needed to proceed.

### Next session starts here
1. Run `memory/session_start.md` in full.
2. Begin **M1**: `CMakeLists.txt` + `CMakePresets.json` + module targets + Catch2 + CI.
3. Then `tools/capture_fixture.py` — golden fixtures gate every subsequent port step.
