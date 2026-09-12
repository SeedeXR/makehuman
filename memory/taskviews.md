# Task views: inventory and port plan

**Measured 2026-08-30** by `tools/audit_taskviews.py`, which re-derives every
number here from `legacy/python` and fails CI if this file drifts from it.

<!-- audit:explicit=44 -->
<!-- audit:dynamic=7 -->
<!-- audit:total=51 -->

**51 task views** — 44 standalone tabs + 7 built at run time.

## The number was wrong twice

`architecture.md` said **50**, uncited. A first audit using a
`^class X(...TaskView)` regex said **48**. Both wrong, and the regex failed in
the direction that costs most: it matches only a single base class, so it could
not see

```python
class LoadTaskView(gui3d.TaskView, filecache.MetadataCacher):
```

Six views were invisible to it — **Load** (`apps/gui/guiload.py:73`),
**Skin/Material** (`plugins/3_libraries_material_chooser.py:69`), **Pose**
(`plugins/3_libraries_pose.py:73`), **Skeleton**
(`plugins/3_libraries_skeleton/skeletonlibrary.py:83`), **Expressions**
(`plugins/2_posing_expression.py:69`) and the `ProxyChooserTaskView` base
(`apps/gui/proxychooser.py:90`). Five of those are real user-facing tabs, and
the whole Pose/Animate category was missing from the roadmap. The auditor now
walks the AST and resolves bases transitively, so a mixin added tomorrow cannot
hide a view again.

Three distinctions the count turns on, all now enforced structurally:

| Not counted | Why | Evidence |
|---|---|---|
| `ModifierTaskView`, `ProxyChooserTaskView`, `RenderTaskView` | abstract bases, never constructed | only ever subclassed or `__init__`-chained |
| `TextureProjectionView` | constructed, never registered — not a reachable tab | `plugins/0_modeling_background.py:630`, `addTask` commented out on `:632` |
| `MeasureTaskView` | realised *only* as the dynamic "Measure" view; counting class + view double-counts | `taskviewClass=MeasureTaskView` at `plugins/0_modeling_a_measurement.py:376`; `measurement_sliders.json` has one key |

Cross-checked independently: **45** uncommented `addTask(` call sites, one of
which is the loop at `apps/gui/guimodifier.py:232` that builds the dynamic
views → 44 standalone. The auditor recomputes this cross-check every run.

The folklore "50" is not the release-build figure either: `core/mhmain.py:211-221`
excludes nine plugins by default (10 views), and `SkeletonDebugLibrary` is
dev-gated, so a default release build shows **40**.

## Where the 51 stand

| Bucket | N | Meaning |
|---|---|---|
| done | 7 | the dynamic modifier views — shipped |
| covered | 20 | the capability reaches the user, just not as a TAB |
| todo | 5 | to port, nothing blocking |
| blocked | 3 | needs content or an engine capability first |
| declined | 16 | Python-runtime or dev-only tooling |

## The buckets were wrong about FOURTEEN views, in both directions

**Corrected 2026-09-12.** The auditor has always checked the reference side —
how many views exist upstream, and that every one is classified. Nothing ever
checked the other half of each claim: whether OUR bucket for a view was still
true. Both halves rotted, in opposite directions:

  * **Nine sat in `todo`** — "to port, nothing blocking" — after they shipped.
    `ExportTaskView` was the worst, filed under the comment *"the writers exist
    but nothing in the UI reaches them"* long after `file.export` shipped with a
    handler, five formats and three tests.
  * **Six sat in `blocked`** under *"the viewport draws exactly one mesh"* after
    that stopped being true. Five of those six shipped on 2026-09-11 and -12.

`EVIDENCE` in `tools/audit_taskviews.py` now names, for each such view, a
literal that appears in `src/` if and only if the capability is wired, and the
gate runs BOTH ways: `covered`/`done` requires the evidence present,
`todo`/`blocked` requires it absent.

**And then the patch had a hole of its own.** The `NO_EVIDENCE` map that closed
the first one held free TEXT — a sentence saying why a view could not be
checked — and two of its eleven sentences were false. `MouseActionsTaskView`
said "no UI surface yet" while `ShortcutsDialog` had been rebinding
`mouse.orbit` and `mouse.pan` since it was written; its own header says it
merges the reference's `5_settings_mouse.py` and `5_settings_shortcuts.py` on
purpose, so a user need not know whether "how do I pan" is a key question or a
mouse one. `ViewerTaskView` said "deliberately not built" while
`mh::ui::ImageViewer` had been showing every render since M8 — what was
deliberately not built is only its Refresh button, because nothing writes a
path to re-read.

That map is now `ABSENT`, and each entry names the literal that WOULD be in
`src/` if the view had shipped. The reason is prose; the literal is the check,
and it runs in the same direction as `EVIDENCE` in reverse. The rule found both
false claims on its first run, by name.

The bucket TABLE in this file is checked too, for the same reason: it is five
numbers nobody read, and both views moved bucket in the chunk that added the
rule.

**The first version of that gate had a hole, and the very next chunk fell into
it.** `CustomTargetsTaskView` shipped on 2026-09-12 and this file stayed stale,
because a view with no `EVIDENCE` entry is simply not checked — silence was
indistinguishable from "nobody has looked". Every non-declined view must now
appear in either `EVIDENCE` or `NO_EVIDENCE`, the latter carrying the REASON it
cannot be checked. Being unchecked is now a deliberate, written-down act.

### done (7)
The views `guimodifier.loadModifierTaskViews` builds from the `*_sliders.json`
files — Face, Torso, Arms and Legs, Gender, Macro modelling, Body shapes,
Measure. One view per top-level key, `apps/gui/guimodifier.py:226-232`.

### covered (17)
Not a gap: this port is dockable, so what the reference makes a tab arrives as
a menu action or as a group in the Assets panel.

`LoadTaskView`, `SaveTaskView`, `ExportTaskView`, `OpenGLTaskView` (the Render
tab — ours is `file.render`; its resolution and AA options are CLI flags),
`RandomTaskView`, `SettingsTaskView`, `ShortcutsTaskView` — menu actions.

`MaterialTaskView`, `PoseLibraryTaskView`, `SkeletonLibrary` — Assets-panel
groups (Skin material, Pose, Skeleton).

`TeethTaskView`, `TongueTaskView`, `HairTaskView`, `ClothesTaskView`,
`EyelashesTaskView`, `EyesTaskView` — the six proxy choosers, all shipped as
Assets-panel groups over helper-cage assets.

`CustomTargetsTaskView` — `--custom-targets <dir>`, one `custom/<stem>` slider
per `.target` file a user supplies.

### todo (8)
`AnimationLibrary`, `ExpressionTaskView`, `ViewerTaskView`, `BackgroundChooser`,
`MaterialEditorTaskView`, `ExpressionMixerTaskView`, `MouseActionsTaskView`,
`HelpTaskView`.

`AnimationLibrary` gates only on a skeleton and an active animation
(`3_libraries_animation.py:150,157`) — `rig/` and `io/BvhReader.h` have both.
`ExpressionTaskView` chooses `.mhpose` files and this port ships **zero**
(measured under `data/`), so expressions arrive as `--facs` action units
instead; the chooser needs content before it needs code.

### blocked (3)
`EyebrowsTaskView` has no helper cage in the base mesh. `ProxyTaskView` would
choose between alternate BODY topologies, and the only proxymesh-shaped assets
shipped are `data/3dobjs/base.mhclo` (`basemesh alpha_7`, 434 verts — the OLD
topology's map) and `a7_converter.proxy` (the alpha_7 → hm08 converter, 7102
verts); neither is wearable. Both are blocked on CONTENT, not on the engine.
`SceneLibraryTaskView` is blocked on a lighting model: a scene is lights plus
environment (`shared/scene.py:190-192`).

~~All eight choosers are blocked on the same thing: **the viewport draws exactly
one mesh.**~~ **CORRECTED 2026-09-05 (session 135).** Multi-mesh rendering is
**done** — `ViewportWidget::setMeshes`, `render::MeshInstance`, and
`tests/render/test_proxy_render.cpp` draws a body and a worn proxy together.
`Eyes` is a live chooser over `data/eyes/*.mhclo` and works.

**But they are still not buildable, for a different reason: the assets do not
ship.** Measured, per directory:

| Directory | `.mhclo` files |
|---|---|
| `data/eyes/` | **2** |
| `data/clothes/`, `data/hair/`, `data/teeth/`, `data/tongue/`, `data/eyebrows/`, `data/eyelashes/`, `data/proxymeshes/` | **0** |

`data/hair/`, `data/eyebrows/` and `data/proxymeshes/` contain exactly one file
each — `clear.thumb`, a placeholder. Upstream MakeHuman ships these as separate
downloadable asset packs, not in the base data.

So the seven remaining choosers are blocked on **content, not code**: building
them now would ship seven empty dropdowns, which is the "painted no-op" this
project has refused elsewhere. What they need is either an asset pack decision
from the owner or procedurally generated proxies of our own — the same route
taken for the skin textures.

`SceneLibraryTaskView` is blocked on something different and should not be
lumped in with them — a `Scene` is `self.lights = []` plus an `Environment`
(`shared/scene.py:190-192`), so it needs a lighting model, not more meshes.

`AnimationLibrary` was in this bucket and has moved to `todo`: its only gates
are a skeleton and an active animation
(`plugins/3_libraries_animation.py:150,157`), and `rig/Skeleton.h`,
`rig/Skinning.h` and `io/BvhReader.h` all exist.

### declined (16)
`ShellTaskView`, `ScriptingView`, `ScriptingExecuteTab`, `SocketTaskView`,
`PluginsTaskView`, `UserPluginsTaskView`, `LoggingTaskView`,
`ProfilingTaskView`, `DataTaskView`, `ExampleTaskView`, `TargetsTaskView`,
`SaveTargetsTaskView`, `SceneEditorTaskView`, `AssetDownloadTaskView`,
`MassProduceTaskView`, `SkeletonDebugLibrary`.

Python-runtime tooling (shell, scripting, plugin management, socket server) has
no meaning in a build that ships no Python. `SkeletonDebugLibrary` is dev-only:
registered only `if not mh.isRelease()`
(`plugins/3_libraries_skeleton/__init__.py:62-68`).

A decision, not an omission — revisit any of these by moving it to `todo` in
`BUCKETS` and re-running the auditor.
