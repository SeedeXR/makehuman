# Architecture

**Version:** 1.0 · **Created:** 2026-08-29 · **Status:** design, pre-implementation

> Every "As-is" fact below was verified in-session against the cited `file:line`
> in `legacy/python/`. Every "To-be" statement is a design decision, not an
> observation, and is marked as such.

---

## Part I — As-is: the Python reference

### I.1 Layer map

```
makehuman.py            bootstrap: chdir, sys.path, logging, stream redirect
   └── core/mhmain.py   MHApplication (QApplication + gui3d.Application + mh.Application)
        ├── lib/qtui.py     Frame(QMainWindow), Canvas(QOpenGLWidget), Application
        ├── lib/qtgui.py    ~40 widget wrappers (QWidget + mixin)
        ├── core/gui3d.py   View / TaskView / Category / Application  (scene-graph + nav)
        ├── core/events3d.py string-keyed event dispatch
        ├── apps/human.py   Human (guicommon.Object + animation.AnimatedMesh)
        │    ├── apps/humanmodifier.py   Modifier hierarchy, target weighting
        │    ├── core/algos3d.py         Target (morph) load + apply
        │    ├── core/module3d.py        Object3D — THE mesh container
        │    ├── shared/proxy.py         Proxy fitting (.mhclo)
        │    ├── shared/skeleton.py      Skeleton / Bone
        │    └── shared/animation.py     AnimationTrack / Pose / skinMesh (CPU LBS)
        ├── lib/glmodule.py  fixed-function GL draw + picking + FBO baking
        ├── lib/shader.py    GLSL 1.20 program cache, #define variants
        ├── shared/material.py  .mhmat parse/write, shader config
        └── plugins/*.py     50 TaskViews, 8 exporters — loaded by lexicographic filename
```

### I.2 The mesh — `core/module3d.py:110` `Object3D`

Indexed mesh, **uniform primitive size**, `vertsPerPrimitive = 4` by default
(`module3d.py:111`). GL primitive chosen by `g_primitiveMap[vertsPerPrimitive-1]`
(`lib/glmodule.py:66`) → **`GL_QUADS`**.

Triangles are stored as **degenerate quads**: the OBJ loader duplicates index 0
(`shared/wavefront.py:105-106`), and `setFaces` detects it via
`verts[0][0] == verts[0][3]` → `vertsPerFaceForExport = 3` (`module3d.py:634-639`).

**Dual index space.** A face corner references `(fvert[f][i], fuvs[f][i])` —
positions and UVs have *independent* index spaces (`module3d.py:627`, `:629`).
`updateIndexBufferVerts` (`:815-840`) unwelds by packing `(fvert<<32)|fuvs` into
uint64, `np.unique`, and produces the render-side arrays `r_coord`, `r_vnorm`,
`r_texco`, `r_vtang`, `r_color` plus `vmap`/`tmap` gather tables.

Per-vertex adjacency: `vface` (uint32, `nv × MAX_FACES`) + `nfaces` (uint8 valid
count) — `module3d.py:529-530`. `MAX_FACES` starts at 8 and is shrunk to the true
max valence (`:760-770`); **measured value for the base mesh is 5**.

Dirty tracking already exists and is per-vertex accurate: `ucoor/unorm/utang/
ucolr/utexc` are tri-state (False / True / per-vertex bool array), set by
`markCoords` (`:557-589`) and consumed by `sync_*` (`:872-938`). **This is the
hook a GPU port needs for partial buffer updates.**

Measured (2026-08-29): base mesh **19,158 verts / 18,486 quads / 21,334 UVs**;
subdivided **75,008 verts / 73,944 faces**.

### I.3 Morph targets — `core/algos3d.py:67`

Sparse: `verts` (indices) + `data` (Nx3 offsets), dtype
`[('index','u4'),('vector','(3,)f4')]` (`algos3d.py:72`). Apply is **additive**:

```
coord[verts] += data * (scale * morphFactor)      # algos3d.py:268, :284
```

There is no "current value" on the mesh — correctness depends on the caller either
resetting to `orig_coord` and replaying the whole stack (`apps/human.py:1159-1165`)
or applying the delta `new - old` (`apps/humanmodifier.py:277`).

Compiled form: a `ZIP_DEFLATED` archive of `.npy` entries, quantised to
`uint16` indices + `int16(round(offset*1e3))` (`algos3d.py:220-221`), decoded
`* 1e-3` (`:168`). **Vertex indices are capped at 65535** — fine at 19,158, but a
hard ceiling to remove in the port.

1,280 `.target` files on disk; mean 983 affected verts, max 5,480.

### I.4 Modifiers — `apps/humanmodifier.py`

```
Modifier (:153)
├── SimpleModifier (:345)
└── ManagedTargetModifier (:380)
    ├── UniversalModifier (:488)  →  WarpModifier (apps/warpmodifier.py:84, DEAD)
    └── MacroModifier (:547)      →  EthnicModifier (:615)
```

**The weighting rule** (`humanmodifier.py:644-652`, verified):

```
weight(target) = sliderValue × Π factors[f]  for f in target.factorDependencies
```

Factor names come from the target's *filename tokens*. `lib/targets.py:203-215`
splits every path/filename on `-`, `_`, and `.`; a token in the reserved value
table (`_cat_data`, `targets.py:50-61` — 9 categories, **27** values;
an earlier note here said 26, which was wrong — `age` has four values) becomes a macro
dependency, anything else becomes part of the group key.

Macro scalars → per-value factors, e.g. `_setAgeVals` (`apps/human.py:574-600`):
```
age < 0.5:  babyVal  = max(0, 1 - age*5.333)
            youngVal = max(0, (age-0.1875)*3.2)
            childVal = max(0, min(1, 5.333*age) - youngVal);  oldVal = 0
age >= 0.5: oldVal   = max(0, age*2 - 1);  youngVal = 1 - oldVal;  child=baby=0
```
This is separable multilinear interpolation over a 1-of-N simplex per category.

Zero weights are **deleted** from the stack (`apps/human.py:920-921`).

### I.5 Skeleton and skinning

`Skeleton` / `Bone` — `shared/skeleton.py:64`, `:698`. **163 bones, 326 joints,
163 planes** in `data/rigs/default.mhskel`. Joint position = mean of a named
vertex cloud on the base mesh (`skeleton.py:419-436`) — the rig follows the morph.

Matrix conventions (verified `core/transformations.py:97-100`, `skeleton.py:1199`):
row-major storage, **column vectors** (`v' = M·v`), translation in `M[:3,3]`,
**Y-up, model faces +Z**, right-handed. Quaternions are **`[w,x,y,z]`**
(`transformations.py:112`) — Eigen's `.coeffs()` is `[x,y,z,w]`, a classic trap.

Skinning matrix: `matPoseVerts = matPoseGlobal · inv(matRestGlobal)`
(`skeleton.py:909`). **CPU linear-blend skinning**, vectorised via `einsum`
(`shared/animation.py:1205`). No dual quaternion, no GPU skinning — verified: no
bone/skin uniforms exist in any of the 12 shader files.

Weights (`data/rigs/default_weights.mhw`): 139 weighted groups for 163 bones;
measured max **12** influences/vertex, mean 2.98. Runtime truncates to **6**
(`animation.py:1067`); morph application truncates to **4** (`algos3d.py:274-281`).

**No IK. No retargeting.** `Bone.poleTargetCorrect` (`skeleton.py:1108`) has zero
callers; `Skeleton.transformed` raises `NotImplementedError` (`:490`). What exists
is name/reference-based channel remapping (`bvh.py:191-238`, whose own comment at
`:200-201` says multi-reference "does not work").

Expressions: 60 face pose units as BVH frames (`data/poseunits/face-poseunits.bvh`,
162 joints × 60 frames), blended by slerp-toward-rest then **multiplicative,
order-dependent composition** (`animation.py:399-413`). **Rotation only —
translations are discarded** (`:415`), so jaw slide / lip pursing cannot be
expressed through this path.

### I.6 Rendering — `lib/glmodule.py`

**No version or profile is requested** (`lib/qtui.py:182-190`) — Qt supplies the
platform default. MSAA explicitly **disabled** (`setSamples(0)`, `:188`).

**Fixed-function pipeline with an optional shader overlay.** Verified absent
(exhaustive grep, zero hits): `glGenBuffers`, `glBindBuffer`, `glBufferData`,
`glGenVertexArrays`, `glNewList`, `glBegin`. Every draw re-specifies raw numpy
pointers:

```
glVertexPointer(3, GL_FLOAT, 0, obj.verts)     # glmodule.py:479
glNormalPointer(GL_FLOAT, 0, obj.norms)        # :481
glTexCoordPointer(2, GL_FLOAT, 0, obj.UVs)     # :477
```

Per frame, per object: full matrix-stack push, a texture bind/unbind loop over
*every* texture unit (`:452-466`), five `glMaterialfv` with freshly allocated
numpy arrays (`:505-523`), one `glDrawElements` — **plus an extra per-face-group
pass looping all 172 groups** (`:598-607`).

**Picking is a second full-scene render** into a colour-ID buffer followed by a
`glReadPixels` of the entire window (`:133-174`), invalidated after *every* frame
(`:1063`).

Shaders: 6 programs in `data/shaders/glsl/`, GLSL **`#version 120`** (two files
declare no version at all). All depend on removed built-ins — `ftransform()`,
`gl_NormalMatrix`, `gl_LightSource[]`, `gl_FrontMaterial`, `texture1D`. The
default skin is a **litsphere/matcap** (`litsphere_fragment_shader.txt:82`),
which works precisely because the light is fixed in eye space while the *model*
rotates (`camera.py:501-525` vs `:689-723`).

Materials: Blinn-Phong, **7 texture channels** (diffuse, bump, normal,
displacement, specular, transparency, AO — `material.py:176`). **No PBR anywhere** —
no metallic, no roughness, no ORM, no IOR.

### I.7 Interchange — `plugins/9_export_*`

**Export only. There is no import layer at all** — verified by grep: zero hits for
`addImporter`, `class *Importer`, `ImportTaskView`, `gltf`, `usd`, `draco`. The
only mesh reader is `shared/wavefront.py:47` `loadObjFile`, which ignores `vn`
and `usemtl` entirely (`:50-52`, `:125-126`).

| Format | Skel | Weights | Material | Anim | Blendshapes |
|---|---|---|---|---|---|
| OBJ | ✗ | ✗ | ✓ .mtl | ✗ | ✗ |
| Collada | ✓ | ✓ | ✓ phong | broken | dead (`NameError`, `dae_controller.py:208`) |
| FBX | ✓ | ✓ | ✓ phong | **disabled** | dead (arity bugs) |
| Ogre | ✓ | ✓ | ✓ | TODO | ✗ |
| STL | ✗ | ✗ | ✗ | ✗ | ✗ |
| BVH | ✓ | ✗ | ✗ | **disabled** | ✗ |

The FBX writer is hand-rolled, **FBX 7300 (= 7.3.0, the 2013 format)**
(`fbx_utils_bin.py:32`), with a binary backend vendored from Blender 2.79
(GPL-2.0-or-later). It forges the `Creator` string to
`"FBX SDK/FBX Plugins version 2013.3"` because "The FBX converter refuses to load
the character unless this is the creator" (`fbx_binary.py:676-678`), and writes a
fixed fake `FileId`/`CreationTime` (`encode_bin.py:285-314`).

**Verified 10× unit bug**: `cfg.scale *= 10` (`9_export_fbx/__init__.py:112`)
combined with a hardcoded `scale_factor = 10.0` (`fbx_binary.py:736`, with the
correct `10.0/config.scale` commented out at `:735`). Only "decimeter" exports
correctly; metre/cm/inch are wrong by 10×.

**Units:** internal unit = **1 decimetre** (`apps/human.py:694-699`:
`heightCm = 10 × bboxY`). Export scale table (`apps/gui/guiexport.py:124-129`):
dm 1.0, m 0.1, inch 1/0.254, cm 10.0.

### I.8 UI shell

Two levels of Qt tabs: Category = `QTabWidget` tab, Task = `QTabBar` tab inside it
(`lib/qtgui.py:155-196`). **51 task views** — 44 standalone + 7 built at run
time; the "50" this file used to carry was wrong. Inventory, per-view plan and
the arithmetic in `memory/taskviews.md`, re-derived by
`tools/audit_taskviews.py` in CI. Each `TaskView.__init__`
allocates its own left/right scroll-area panel (`core/gui3d.py:259`); switching
tasks is a `QStackedLayout` page swap (`lib/qtui.py:552-554`).

**Verified: zero `QDockWidget`, `QSplitter`, `QMdiArea`, `saveState`, or
`restoreState` in the entire codebase.** Docking, snapping, and workspaces are
100% greenfield.

Events: string-keyed duck typing — `callEvent` does `getattr(self, name)()` and
**swallows exceptions** (`core/events3d.py:200-221`), `attachEvent` is `setattr`,
and the `@x.mhEvent` decorator monkeypatches instances (`:234-235`). No enum, no
registry. Keyboard never goes through it — all shortcuts are `QAction` +
`QKeySequence` (`core/mhmain.py:156-193`).

Plugins: discovered by directory scan, ordered by **plain lexicographic filename
sort** (`core/mhmain.py:562`) — that is the entire meaning of the `0_`…`9_`
prefixes. Loaded with `importlib` + `exec_module`; the API is exactly two
module-level functions, `load(app)` and `unload(app)`.

Theming: a custom line-based `.mht` (viewport colours only) plus a 681-line
`.qss`. Dark theme is `makehuman.qss`: base `#323232`, text `#ffffff`, accent
gradient `#ffa02f → #e96226`. **QSS `url()` paths are relative to the process
CWD** and only work because `makehuman.py:143` does `os.chdir`.

### I.9 Verified hotspots (ranked)

| # | Hotspot | Evidence |
|---|---|---|
| H1 | Full vertex+index arrays re-fed to the driver **every frame, every object** | `glmodule.py:475-488`, no VBOs anywhere |
| H2 | Picking = 2nd full scene render + 172 draw calls + full-window `glReadPixels`, invalidated every frame | `glmodule.py:133-174`, `:1063` |
| H3 | `os.path.getmtime` per texture, per object, **per frame** | `texture.py:232`, `shader.py:244-263` |
| H4 | Human bounding box recomputed **every frame** (gathers a 74k×3 array) | `glmodule.py:1016`, `module3d.py:1215-1234` ("TODO maybe cache") |
| H5 | `_update_faces` — nested Python loops, `O(V·valence·vpp·MAX_FACES)` | `module3d.py:697-770`; **this is why `.npz` caching exists** |
| H6 | `applyAllTargets` = full reset + replay of the entire stack + full normals, on **every slider release** | `apps/human.py:1147-1209` |
| H7 | CPU skinning + full `calcNormals` on **every pose change** | `animation.py:1086-1094` ("way too slow for realtime animation") |
| H8 | Per-frame Python object churn: `sorted(G.world)`, 3-5 `np.matrix` per object, 5 numpy allocs for materials, a new `shaderParameters` dict | `glmodule.py:1019`, `camera.py:509-524`, `glmodule.py:505-517`, `material.py:1066-1082` |

Measured consequence: subdiv `calcNormals` alone is **20.57 ms** — a 16.6 ms frame
budget is already blown before any drawing happens.

---

## Part II — To-be: the C++/Qt6 architecture

> Design decisions. Nothing below is an observation of existing code.

### II.1 Module map and the licence boundary

```
                    ┌──────────────────────────────────────┐
   Apache-2.0       │  mh-foundation   math, containers,   │  reusable
   (clean-room,     │                  logging, jobs       │  commercially
    no MakeHuman    │  mh-rhi          render abstraction  │  by anyone
    derivation)     │  mh-scene        generic scene graph │
                    │  mh-io-*         glTF/GLB, USD, FBX, │
                    │                  OBJ, STL writers    │
                    │                  (from published     │
                    │                   specs only)        │
                    │  mh-ui-dock      docking, workspaces │
                    └──────────────────────────────────────┘
                                      ▲  depends on
                    ┌─────────────────┴────────────────────┐
   AGPL-3.0         │  mh-core     mesh, targets, modifiers│  derivative of
   (derived from    │  mh-rig      skeleton, skinning, pose│  MakeHuman —
    MakeHuman)      │  mh-asset    .mhm/.mhclo/.mhmat/     │  stays copyleft
                    │              .mhskel/.target/.mhw    │
                    │  mh-app      Qt6 application shell   │
                    └──────────────────────────────────────┘
```

**The rule that makes this real:** an Apache-2.0 module may never be produced by
translating an AGPL source file. It is written from a published specification or
from scratch. Dependency arrows point **upward only** — AGPL may depend on
Apache-2.0, never the reverse. Any doubt → the code is AGPL.

Rationale: this is how downstream users get genuine commercial reuse of the parts
that are legitimately ours to give, without misrepresenting the copyleft that
covers the parts we inherited. See `project_context.md` §4.2.

### II.2 Directory layout

```
/                       repo root
├── CMakeLists.txt      top-level; options, toolchain, targets
├── CMakePresets.json   macos-arm64-{debug,release,asan}
├── CLAUDE.md           agent instructions (loaded every session)
├── AGENT.md            agent profile pointer
├── LICENSING.md        full dependency + asset licence inventory, SBOM
├── memory/             ← this folder: the project's long-term memory
├── include/makehuman/  public headers, one dir per module
│   ├── core/  io/  rig/  render/  ui/
├── src/
│   ├── core/           mesh, target, modifier, proxy, human            [AGPL]
│   ├── rig/            skeleton, skinning, pose, retarget, facs        [AGPL]
│   ├── io/             importers + exporters, format registry          [Apache-2.0]
│   ├── render/         RHI renderer, materials, picking, baking        [Apache-2.0]
│   ├── ui/             docking, panels, widgets, theme, workspaces     [Apache-2.0]
│   └── app/            main(), application shell, task registry        [AGPL]
├── data/               CC0 assets (moved from legacy makehuman/data)
├── resources/
│   ├── icons/          Lucide SVGs
│   ├── fonts/          42dot Sans
│   ├── themes/         dark.qss + tokens
│   └── shaders/        .qsb baked shaders + sources
├── tests/{unit,integration,regression,golden,smoke}
├── benchmarks/         baseline_python_core.py + C++ benchmarks
├── tools/              asset compiler, shader baker, codegen
├── third_party/        vendored deps (FetchContent-preferred)
├── cmake/              modules, toolchains
├── packaging/macos/    Info.plist, bundle, dmg, notarization
├── docs/               authored + generated documentation
├── legacy/python/      the Python reference — oracle only, never shipped
└── graphify-out/       knowledge graph (5,392 nodes / 9,319 edges)
```

### II.3 Core data model (C++)

Derived from the verified reference structures (§I.2–I.5). Struct-of-arrays,
contiguous, no virtuals in the hot path.

```cpp
namespace mh::core {

// Triangles only on the GPU side; quads preserved on the authoring side so
// Catmull-Clark and the .obj/.mhclo formats round-trip losslessly.
struct Mesh {
    std::string name;
    uint8_t vertsPerPrimitive = 4;          // module3d.py:111
    uint8_t vertsPerFaceForExport = 4;      // 3 when quads are degenerate
    uint8_t maxValence = 5;                 // measured on the base mesh

    // per-vertex (SoA)
    std::vector<Vec3>  coord, origCoord, vnorm;
    std::vector<Vec4>  vtang;
    std::vector<RGBA8> color;
    std::vector<uint32_t> vface;            // flat [nv * maxValence]
    std::vector<uint8_t>  nfaces;

    // per-UV — INDEPENDENT index space (module3d.py:629)
    std::vector<Vec2> texco;
    bool hasUV = false;

    // per-face, stride = vertsPerPrimitive
    std::vector<uint32_t> fvert, fuvs;
    std::vector<Vec3>     fnorm;            // unnormalised cross product
    std::vector<uint16_t> group;
    std::vector<uint8_t>  faceMask;

    std::vector<FaceGroup> faceGroups;      // 172 on the base mesh
    DirtyRanges dirty;                      // replaces ucoor/unorm/... 
};

// Sparse morph. Index width widened to uint32 — the reference's uint16
// (algos3d.py:220) caps at 65535 and is a needless ceiling.
struct Target {
    std::string path;
    std::vector<uint32_t> verts;
    std::vector<Vec3>     data;
};

// weight = sliderValue * PRODUCT(factors)   — humanmodifier.py:644-652
struct WeightedTarget { const Target* target; SmallVec<FactorId,6> factors; };
} // namespace mh::core
```

### II.4 Render architecture — Qt RHI

**Decision: Qt RHI, Metal backend on macOS.** Rationale: OpenGL is deprecated on
macOS and capped at 4.1; the reference needs nothing above GL 2.1 (§I.6), so the
feature surface to reimplement is small. Qt RHI ships with Qt 6, integrates with
`QRhiWidget`/`QQuickWindow`, and bakes shaders once via `qsb` for Metal + Vulkan +
D3D12, keeping the door open for Windows/Linux later.

Fixes, mapped to the hotspots:

| Hotspot | Fix |
|---|---|
| H1 | Persistent interleaved vertex buffers; partial updates driven by the existing per-vertex dirty ranges (`module3d.py:557-589`) |
| H2 | ID-buffer render to a small R32Uint target + async single-pixel readback; or CPU BVH ray-pick. Never a full-window sync readback |
| H3 | Explicit texture invalidation. No `stat` in the frame loop |
| H4 | Cache the bounding box; invalidate on human/proxy change |
| H7 | GPU skinning: matrix palette in a UBO/SSBO, LBS in the vertex shader |
| H8 | Precomputed per-object uniform blocks; sort the draw list only when the world changes |

Quads → triangles at buffer-build time (Metal has no `GL_QUADS`). Wireframe has
no `glPolygonMode` equivalent — use a barycentric fragment shader or a line index
buffer. Depth range: GL is `z ∈ [-1,1]`, **Metal is `z ∈ [0,1]`** — `lib/matrix.py:60-92`
must be reworked, not transliterated.

Material model moves to **PBR metallic-roughness**, with a documented
Blinn-Phong → PBR conversion for legacy `.mhmat`, and `.mhmat` extended with
optional PBR keys. The litsphere/matcap path is preserved bit-faithfully
(including the `0.495` scale and the `2.0 - mean(shading)` term) because it is the
default skin shader and every existing screenshot depends on it.

### II.5 Interchange architecture

The reference has no scene representation to import *into* (§I.7). We introduce one:

```
                 ┌──────────────────────┐
 FBX ─┐          │   mh::io::Scene      │          ┌─ glTF/GLB
 GLB ─┤          │  nodes, meshes,      │          ├─ FBX
 OBJ ─┼─ Import ─▶  skins, materials,   ─ Export ──┼─ USD
 USD ─┤          │  animations,         │          ├─ OBJ
 DAE ─┘          │  morph targets       │          └─ STL / DAE / BVH
                 └──────────────────────┘
                           ▲ ▼
                    mh::core::Human  (bidirectional adapter)
```

One `UnitSystem`/`Transform` object consumed by **every** reader and writer —
this is the single highest-value fix, because the reference duplicates
orientation and scale logic per exporter and gets it wrong in FBX (§I.7). The
repeated `# TODO integrate meshOrientation and localBoneAxis properties in this
config` (`core/export.py:92` and two other sites) is exactly this.

Library decision — **assimp 6.0.4 is already installed** (verified via
`brew list`) and is BSD-3-Clause, so it is licence-compatible. Planned use:
assimp for broad *import* coverage; **purpose-written exporters** for glTF/GLB
(the spec is small and quality matters), and a from-spec FBX writer targeting
**7.4/7.5**, not a translation of the GPL Blender code. The Autodesk FBX SDK is
**forbidden** — its licence is incompatible with AGPL redistribution.

Non-negotiable conversions the reference lacks entirely:
- Skin weights → sorted, normalised, clamped to 4 influences (glTF requires it).
- Morph targets → real blendshape export (the reference's is dead code everywhere).
- Blinn-Phong → metallic-roughness.
- Texture packing (ORM), embedding into GLB buffers, KTX2/Basis, optional Draco.

### II.6 UI architecture

Greenfield — the reference has nothing to port (§I.8).

- **Shell**: `QMainWindow` + `QDockWidget` with nested docking, tabbed docks, and
  floating windows. `saveState`/`restoreState` for workspaces.
- **Workspaces**: named layouts, saveable, resettable, shipped presets
  (Modelling / Rigging / Materials / Export). Serialised as JSON + the Qt state
  blob, versioned so a layout from an older build degrades gracefully.
- **Panel chrome**: every dock carries the six-dot grip in its title bar opening a
  menu — Float, Dock Left/Right/Top/Bottom, Tab with…, Close, Reset.
- **Task registry** replaces the `0_`…`9_` filename-sort plugin ordering with an
  explicit registration API carrying a stable id, a category, and a sort key.
- **Events**: Qt signals/slots. The string-keyed `callEvent` model and its
  exception swallowing (`events3d.py:212-214`) are not ported.
- **Viewport**: `QRhiWidget` subclass. MSAA **on** (the reference disables it,
  `qtui.py:188`).
- Settings: `QSettings`-backed, **symbolic** shortcut and mouse-binding storage —
  the reference persists raw Qt enum ints (`mhmain.py:1021-1027`) whose numeric
  values are not stable across Qt5→Qt6.

Visual language, icons, typography, and tokens are specified in `design.md`.

### II.7 Threading

- **Main thread**: Qt event loop, UI, RHI submission.
- **Job system**: a work-stealing pool (TBB 2023.1.0 is installed, Apache-2.0) for
  target application, proxy fitting, normal recomputation, mesh subdivision.
- **I/O thread**: asset load/save, never blocking the UI.
- Rule: the render thread never allocates; buffers are written from the job pool
  into persistently mapped staging memory.

### II.8 Asset pipeline

`tools/mhassetc` compiles the CC0 data into a single mmappable blob:
- 1,280 targets → one file, uint32 indices, float32 or int16-quantised offsets,
  with an offset table. Replaces the 670 ms ASCII parse with an mmap.
- Base mesh → prebuilt adjacency (`vface`/`nfaces`), skipping `_update_faces`
  entirely (H5) — the reference already does this via `.npz` (`files3d.py:140-143`).
- Shaders → `.qsb` via Qt's `qsb` tool at build time.

### II.9 Character generation (Objective O6)

Deferred to a later milestone; recorded here so the architecture leaves room.

The modifier space is a bounded, well-defined vector: ~180 shipped modifiers plus
11 macro scalars, all in `[-1,1]` or `[0,1]`. That is a tractable latent space.
Planned direction: fit a generative model over that parameter vector using CC0
in-repo data plus **licence-verified** public anthropometric datasets. Every
dataset must be recorded in `LICENSING.md` before use. **No MetaHuman-derived
data, ever** (`project_context.md` §4.3).

The C++ core must therefore expose a headless, deterministic
`parameters → mesh` API with no Qt dependency — which is why `mh-core` does not
link Qt.

---

## Open decisions

| # | Decision | Status |
|---|---|---|
| D1 | Qt RHI vs. direct Metal | **Decided: Qt RHI.** Revisit only if RHI blocks compute-based morphing |
| D2 | assimp for import vs. all-bespoke | **Decided: assimp for import, bespoke for glTF/FBX export** |
| D3 | Where `data/` lives at runtime on macOS | Open — bundle `Resources/` vs `~/Library/Application Support` |
| D4 | Widgets vs. QML for the panel content | Open — leaning Widgets for density and native feel |
| D5 | Retain `.mhscene` (a Python pickle — an RCE hole, `scene.py:219`) | **Decided: replace with JSON.** It is 4 fields and a light list |
| D6 | Font licence check for "42dot Sans" | Open — user said "red 42 dot sans"; needs confirmation |
