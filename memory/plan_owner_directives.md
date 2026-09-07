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
