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
| covered | 2 | done differently on purpose |
| todo | 17 | to port, nothing blocking |
| blocked | 9 | engine capability missing first |
| declined | 16 | Python-runtime or dev-only tooling |

### done (7)
The views `guimodifier.loadModifierTaskViews` builds from the `*_sliders.json`
files — Face, Torso, Arms and Legs, Gender, Macro modelling, Body shapes,
Measure. One view per top-level key, `apps/gui/guimodifier.py:226-232`.

### covered (2)
`LoadTaskView`, `SaveTaskView`. The reference makes these tabs; we make them
File menu actions (`src/ui/MainWindow.cpp:138-143`), which is the platform
idiom. Not a gap.

`ExportTaskView` was in this bucket and should not have been: there is no export
action in the UI at all (`grep 'file.export' src/` finds nothing). The writers
exist — `io/ObjWriter.h`, `io/GltfWriter.h`, `io/UsdWriter.h` — but nothing
reaches them, so it is a real gap and now sits in `todo`. Calling it "covered"
was the worst kind of error in a roadmap: it labelled missing work as done.

### todo (17)
`ExportTaskView`, `AnimationLibrary`, `MaterialTaskView`,
`PoseLibraryTaskView`, `SkeletonLibrary`, `ExpressionTaskView`,
`OpenGLTaskView`, `ViewerTaskView`, `BackgroundChooser`,
`CustomTargetsTaskView`, `RandomTaskView`, `MaterialEditorTaskView`,
`ExpressionMixerTaskView`, `SettingsTaskView`, `MouseActionsTaskView`,
`ShortcutsTaskView`, `HelpTaskView`.

The first four are the rigging/posing tabs the regex hid. They are *todo*, not
blocked: `rig/Skeleton.h`, `rig/PoseUnits.h`, `rig/Skinning.h` and
`io/BvhReader.h` all exist.

`OpenGLTaskView` is the real **Render** tab — its label is literally `'Render'`
(`plugins/4_rendering_opengl/__init__.py:53`) and it holds the resolution box,
AA toggle and Render button. It is not blocked on a scene model; it deliberately
does not touch the scene shader (`:59`). `ViewerTaskView` is where its output
lands (`plugins/4_rendering_opengl/mh2opengl.py:122-123` sets the image and
switches to it), so declining it would break the render feature.

### blocked (9)
Eight proxy choosers — `ClothesTaskView`, `EyesTaskView`, `EyebrowsTaskView`,
`EyelashesTaskView`, `HairTaskView`, `TeethTaskView`, `TongueTaskView`,
`ProxyTaskView` — plus `SceneLibraryTaskView`.

All eight choosers are blocked on the same thing: **the viewport draws exactly
one mesh.** Multi-mesh rendering is the single largest unblocker on the roadmap:
it clears eight of these nine in one change.

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
