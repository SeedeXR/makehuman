# Owner directives, 2026-09-05 — plan and status

Six directives given in one message. This file is the map; update it as each
lands. `todo.md` still owns the per-item detail.

## Verified environment (measured 2026-09-05, not assumed)

- **FBX SDK 2020.3.9** — `/Applications/Autodesk/FBX SDK/2020.3.9/`, with
  `include/fbxsdk.h` and `lib/{clang,legacy}`.
- **Maya 2027** — `mayapy` at
  `/Applications/Autodesk/maya2027/Maya.app/Contents/bin/mayapy`.
- **Blender 5.2** headless + blender MCP, already wired
  (`tools/run_blender_validation.sh`, currently 11/11).
- `data/skins/` holds ONE material (`default.mhmat`, names no texture);
  `data/textures/` holds only `texture_notfound.png`.

## The licence boundary on the FBX SDK — READ BEFORE TOUCHING IT

`CLAUDE.md` hard rule 6 forbids the **Autodesk FBX SDK** outright. The owner's
instruction was *"we have autodesk fbx sdk and maya in here **to test** and
ensure all properties of read and write are robust"*.

**Resolution taken**: the SDK and Maya are used **only as external validators**,
exactly as Blender (GPL) already is — separate processes, run by a script, never
linked into any shipped target and never a build dependency. Our FBX writer
stays written **from the published spec**.

That keeps hard rule 6 intact: it forbids a *dependency*, and a validator run
out-of-process is not one. Nothing goes into `LICENSING.md` as a dependency,
because nothing links it.

**If the owner wants the SDK linked into the product instead**, that is a
different decision: it needs `LICENSING.md` changed and hard rule 6 amended,
and it must be asked before any such code is written.

## Stages

| # | Directive | Status |
|---|---|---|
| 1 | Bone naming/order: use the **179-bone superset** (richer) | **done** (0832e3be) |
| 5 | `reduceMotion()` true branch — "give it a go" | **done** (0832e3be+) |
| 3 | Exports ship a **live rig** (rest geometry + posed armature) | **done** — glTF + USD verified; FBX/DAE still bake |
| 2+6 | Skin textures: 8 generated tones, 4 African; saves in .mhm and exports | **done** |
| 4 | **Viewport PBR** (metallic-roughness) | **done** — second shading model, litsphere still default |
| 7 | FBX from spec, validated with FBX SDK + Maya + Blender | pending |
| 8 | **Complete the UI** to match the reference screenshot; all lucide icons | **in progress** — icon audit + retina fix + top toolbar done |
| 9 | **App icon + .app bundle + DMG** (logo supplied 2026-09-05) | **done**, but the bundle is not relocatable yet |

Order is by dependency and risk: 2+6 produces the maps that 4 consumes, so those
are adjacent; 7 is last because it is the largest.

Directive 8 arrived mid-turn on 2026-09-05 with a screenshot of MakeHuman
Community 1.2.1. Measured starting state: **57** vendored lucide icons,
**3** `theme::icon()` call sites in `src/ui/` — the set is there and almost
unused. Detail is in `todo.md` under M8.

## Standing gate for every commit

Build clean under `-Werror`; ctest green in debug, release, ASan and TSan;
mutation-test the new logic; code review and ponytail review; SonarQube gate OK
with 0 open issues; Blender harness green; CI green **before** the next push
(the workflow sets `cancel-in-progress`, so an early push destroys the evidence).


## Directive 10 — design authority for the remaining input UI (2026-09-07)

Verbatim: *"for this choose the best design friendly approach but ensure
eveyrthing is already locally so choose best design friendly approach and
intuitive to user on best design implementation approach"*, in reply to the
statement that the remaining M8 items are mouse-button persistence and the
shortcut rebinding UI.

**Reading:** the design call on these is mine to make, judged on how intuitive
the result is, and built from what is already on this machine — no new
dependency, no new asset pack, nothing to download. Do not come back for
approval on layout or interaction detail; come back only for things that change
a promise to the user (a CLI argument, a saved-file format, a licence).

**The design that follows from it, for the rebinding UI:**
- **ONE dialog for both keyboard and mouse.** The reference splits them across
  `5_settings_shortcuts.py` and `5_settings_mouse.py`; a user asking "how do I
  drive this thing" should not have to know which half their question is in.
  Settings ▸ Shortcuts… covers both.
- **Click a row, press the keys** — the reference's own interaction
  (`5_settings_shortcuts.py:123`), and the one every DCC uses. No modal prompt
  first; the hint goes in the dialog.
- **Conflicts inline, not in an alert.** `shortcuts::apply` and
  `MouseBindings::apply` already refuse a collision and name both sides; the
  dialog shows that on the row rather than interrupting.
- **Reset is per row and for all**, because "put this one back" is a different
  wish from "start over" and the workspace menu already proves the pattern.
- Applied live, written on accept: `shortcuts::save` / `MouseBindings::save`
  already write only what differs from the shipped default.

## Directive 11 — the LOD chain ships as GLB and FBX (2026-09-08)

Verbatim, in reply to the statement that the chain is a format question —
LOD0/1/2 as separate files or as extra entries in one glTF/USD scene:

*"for lods we can them as glb and fbx"*

**Reading:** an LOD chain is written as SEPARATE FILES, in **GLB and FBX**, not
as extra entries inside one scene. Those are the two formats that matter for the
audience the LODs are for, and both already carry a skeleton and blend shapes
through our own writers.

Unblocks `memory/todo.md` M9, "the chain itself". What already exists, so the
chunk is smaller than it looks: `--decimate <ratio>` produces one level with its
rig (2026-09-08) and its blend shapes (2026-09-08), `--export` is repeatable,
and both writers take the whole scene. What is missing is the CHAIN: several
ratios in one run, and a naming rule for the files it writes.

## Directive 12 — the correctives architecture, and the answer to "blocked" (2026-09-09)

The owner replied to "PSD is blocked on a format decision" with the whole
architecture. **The conclusion that matters: "you aren't blocked on content,
you're blocked on having decided the contracts."** Steps 1–3 below are
unblocked now.

Overall goal restated by the owner: *"the key thing is we are able to export
well and work well and use them in gaming and animation like how metahuman does
both for unity and unreal or even web"*.

### 12.1 Naming: legacy by default, modern opt-in, canonical inside

The owner accepted the inversion I proposed: **default stays legacy so no
existing setup breaks**, and the new scheme is opt-in via `--naming=modern`
(or a workspace config key). "Anyone migrating flips one flag and can flip it
back."

What keeps it from rotting into a fork — profiles are a BOUNDARY concern only:

- ONE canonical identity per asset, preset and material slot. Canonical IDs
  never change, whatever the profile.
- **Two name tables, as DATA FILES, not code.** They map legacy names and
  modern names onto canonical IDs. One resolver reads them. No
  `if (profile == legacy)` scattered through the asset loader.
- **Resolution order**: the active profile's table, then fall back to the
  other, **warn once on fallback**. Old- and new-named assets coexist in one
  workspace, which is what makes migration painless.
- **Canonical IDs are what new save files persist.** The profile affects what
  is DISPLAYED and what paths are SCANNED, never what is written. "Otherwise a
  file saved in legacy mode and opened in modern mode becomes a support
  ticket."
- The compiled runtime blob never sees profile names at all.
- The same pattern is available for workspace directory layout, if old
  MakeHuman folder structure is wanted as an option.
- **`--workspace Materials` → the rename happens in step 1**, as a
  modern-profile-only name with legacy as the default. "Costs almost nothing
  now." This closes the open question that has been waiting since 2026-09-07.

### 12.2 Where the static/dynamic line falls

"The single most important structural decision."

**Character-static** (recompute on slider change):
base mesh → weighted sum of body-shape targets → shaped rest mesh → skeleton
fit from the shaped mesh → skin weights, normalised → proxy bindings resolved.

**Per-frame**:
joint transforms → pose signal extraction → RBF evaluation → sparse delta
accumulation into rest-space positions → skinning → proxy propagation.

Two consequences committed to:

1. **Correctives apply PRE-SKIN, in rest space.** They are then carried by the
   skinning transform, which is what makes them compose with body-shape targets
   rather than fight them. A corrective authored on one body type degrades
   gracefully on another instead of exploding.
2. **Proxies and clothing bind to the base topology as BARYCENTRIC OFFSETS**,
   so they inherit shape, correctives and pose for free. **Clothing does not
   get its own corrective system.**

### 12.3 One pose-signal evaluator, several consumers

Designed deliberately now "because it changes what M9's remaining content work
looks like".

Extract a normalised signal per driving joint with **swing and twist separated
— swing-twist decomposition, NOT Euler**. Feed one RBF evaluator, which outputs
a weight vector. Several systems then consume the SAME weights:

- geometry correctives (sparse vertex deltas)
- **wrinkle map blending** — "the texture-space sibling of PSD", currently on
  the M9 authored-content list
- future masks: muscle flex, tension-driven shading

So wrinkle maps are a different CONSUMER of the same driver, not a second
parallel system with its own keying convention. **That collapses two M9 content
items into one pipeline and one set of authoring tools.**

**Eye and teeth rigging are NOT this.** They are skeleton and constraint work,
and expressing them as correctives is "a trap". They stay in the rig layer.

### 12.4 Format stack — three layers, one direction of flow

1. **Authoring**: a manifest (TOML or JSON) plus the existing sparse `.target`
   delta payloads. The manifest holds driving joints, which component
   (swing/twist), example pose values, kernel and radius, composition rules and
   payload paths. Diffable, hand-editable, git-reviewable.
2. **Compile**: solve the RBF interpolation matrix OFFLINE and bake it. Per
   frame there is then only kernel evaluation and a small matvec, no solve.
   Output is an mmap-able blob: header with format version and content hash, a
   joint name table, the solved weight matrix, and sparse delta arrays aligned
   for SIMD.
3. **Runtime**: the blob only. It is a DISPOSABLE CACHE, invalidated on
   manifest hash mismatch.

Version the blob format cheaply and aggressively; version the manifest format
conservatively, with a written spec.

**Two version numbers in every export: application version AND content-format
version.** "You need the second one the moment correctives ship." — this also
answers the open M10 item about exports not being traceable to a build.

Keep the manifest shape mappable onto **UE Pose Assets and PoseDriver** if
MetaHuman-adjacent output is the target: "saves an ugly translation layer
later".

### 12.5 Hot path

- Delta accumulation is a **scatter-add**, which resists SIMD. Accumulate into
  a dense `float3` scratch buffer, keep a **dirty-index list** from the union of
  active correctives, and reset only those.
- Skinning wants **SoA** layout and is trivially parallel.
- **Determinism**: parallel scatter-add reorders float additions, so the same
  input can give different output across thread counts. Fix the accumulation
  order per vertex, or use a deterministic reduction. "You will otherwise chase
  phantom test failures for a week."

### 12.6 Order of work — the owner's numbering

1. **Freeze contracts.** Canonical ID registry, naming tables, base topology
   hash, version injection into FBX/glTF/USD/OBJ. "Small, and everything else
   depends on it."
2. **Fix skinning.** Optimised centres of rotation, or dual quaternion at
   minimum. "Kills a large share of the artifacts you'd otherwise author
   correctives to patch. Zero art cost, and it changes how much authoring
   tooling is worth building."
3. **PSD runtime plus synthetic oracle.** An analytic corrective function as
   ground truth, verified at AND between example poses. Still no art needed.
4. **Authoring format, compiler, Blender round-trip.** "Now the tools have a
   tested runtime to target."
5. **Content**: groom, PBR skin, wrinkle maps through the shared driver,
   eye/teeth rig.

### 12.7 Testing spine

- Synthetic **analytic oracle** for interpolation correctness.
- **Baked numeric caches from Blender** (flat binary or `.npy` — no Alembic
  dependency) for realism regression.
- **Topology hash guard**, so a base mesh edit fails loudly instead of silently
  corrupting every delta.
- **Determinism test across thread counts.**
- **Round-trip test**: save in the legacy profile, load in modern, compare
  canonical IDs.

