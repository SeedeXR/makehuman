# Design System

**Version:** 1.0 · **Created:** 2026-08-29 · **Status:** specification

Single source of truth for UI, UX, and interaction. Binding for implementation.

## Visual reference

**→ [MakeHuman Interface Kit](https://claude.ai/code/artifact/fddacb8c-5f35-4619-81cc-6e0f45d1a070)**

A rendered specimen of everything below: the default workspace with a working
six-dot panel menu, the colour tokens as real swatches with their measured
contrast ratios, the type scale set in 42dot Sans, the controls, the Lucide icon
set, and the verification evidence. It is built *in* these tokens rather than
describing them, so a drift between spec and specimen is visible immediately.

Rebuild it from `memory/design.md` whenever the tokens change — the page and this
document must not disagree.

> **Note on the existing UI:** none of this is a port. The reference has zero
> docking infrastructure — verified: no `QDockWidget`, `QSplitter`, `QMdiArea`,
> `saveState`, or `restoreState` anywhere in `legacy/python/`. Its shell is two
> levels of Qt tabs with 50 `TaskView` classes each owning a pair of scroll-area
> panels (`legacy/python/core/gui3d.py:259`). We are building the UI fresh.

---

## 1. Design principles

1. **The model is the hero.** Chrome recedes; the viewport dominates. Dark UI so
   the eye adapts to the render, not the interface — the Lightroom rationale.
2. **Visible system status.** Every long operation shows determinate progress.
   Nothing blocks silently.
3. **Minimal cognitive load.** One primary action per panel. Progressive
   disclosure: advanced controls collapse by default.
4. **Spatial memory.** Panels stay where the user put them. Workspaces persist
   across sessions and are explicitly resettable.
5. **Direct manipulation first.** Drag in the viewport before typing in a field.
6. **Consistency over novelty.** One slider, one file chooser, one asset browser —
   reused everywhere. The reference has three different chooser idioms; we have one.
7. **Reversibility.** Everything undoable. Destructive actions confirm once, then
   are undoable anyway.

---

## 2. Typography

**Primary typeface: 42dot Sans** — an open-source sans by 42dot, available on
Google Fonts under the **SIL Open Font License 1.1** (redistributable, embeddable,
commercially usable — compatible with AGPL distribution).

> ⚠️ **Needs confirmation.** The instruction received was *"red 42 dot sans"*.
> `42dot Sans` is the clear match for "42 dot sans"; the leading "red" is
> unexplained — it may be a typo, or may indicate a different family (e.g. a
> "Red Hat" family). **Confirm before shipping**; the token indirection below means
> changing it is a one-line edit. The licence must be re-verified for whatever
> family is finally chosen and recorded in `/LICENSING.md`.

```css
--font-sans: "42dot Sans", -apple-system, "SF Pro Text", "Helvetica Neue", sans-serif;
--font-mono: "SF Mono", "JetBrains Mono", Menlo, monospace;   /* numeric fields, logs, code */
```

Fonts are bundled in `resources/fonts/` and registered via `QFontDatabase::addApplicationFont`
so rendering is identical regardless of what the user has installed.

### Type scale (macOS points; scales with the OS text-size setting)

| Token | Size / line | Weight | Use |
|---|---|---|---|
| `--text-display` | 24 / 32 | 600 | Splash, empty states |
| `--text-title` | 17 / 24 | 600 | Dialog titles |
| `--text-panel` | 13 / 18 | 600 | Dock title bars, section headers |
| `--text-body` | 13 / 18 | 400 | Default UI text |
| `--text-label` | 12 / 16 | 400 | Slider labels, form labels |
| `--text-caption` | 11 / 14 | 400 | Hints, secondary metadata |
| `--text-numeric` | 12 / 16 | 500 mono | Spin boxes, measurements, coordinates |

Never below 11 pt. Never more than three sizes visible in one panel.

---

## 3. Colour

Dark-first, single theme at launch. Neutral greys, one warm accent — inherited in
spirit from the reference's `#ffa02f → #e96226` accent
(`legacy/python/data/themes/makehuman.qss:40-49`) so existing users feel at home.

### Neutrals

| Token | Hex | Use |
|---|---|---|
| `--bg-viewport` | `#1a1a1c` | 3D viewport background (top of gradient) |
| `--bg-viewport-far` | `#0f0f11` | viewport gradient bottom |
| `--bg-base` | `#212124` | window background |
| `--bg-panel` | `#2a2a2e` | dock panel body |
| `--bg-elevated` | `#323238` | popovers, menus, dialogs |
| `--bg-input` | `#1c1c1f` | text fields, spin boxes |
| `--bg-hover` | `#3a3a41` | hover fill |
| `--bg-active` | `#45454e` | pressed / selected fill |
| `--border-subtle` | `#35353b` | panel separators |
| `--border-strong` | `#4a4a52` | input borders, focus outline base |

### Text

| Token | Hex | Contrast on `--bg-panel` |
|---|---|---|
| `--text-primary` | `#ececee` | 13.1:1 — AAA |
| `--text-secondary` | `#a8a8b0` | 6.2:1 — AA |
| `--text-tertiary` | `#76767e` | 3.4:1 — large text / non-essential only |
| `--text-disabled` | `#5a5a62` | decorative only, never sole information |

### Accent and semantic

| Token | Hex | Use |
|---|---|---|
| `--accent` | `#f58220` | primary action, active tab, slider fill — the MakeHuman brand orange, taken from `legacy/python/icons/makehuman.svg` |
| `--accent-hover` | `#ff9642` | |
| `--accent-press` | `#d96a10` | |
| `--accent-muted` | `#f5822033` | selection wash, 20% alpha |
| `--success` | `#4ea87a` | |
| `--warning` | `#e0a33e` | |
| `--danger` | `#e05c5c` | destructive actions |
| `--info` | `#5b9dd9` | |

**Accessibility rules:** all body text meets WCAG AA (4.5:1); primary text meets
AAA. Colour is never the only signal — state also carries an icon, a label, or a
shape. Focus rings are 2 px `--accent` at 3:1 against any adjacent fill.

A light theme is a post-1.0 item; every colour is a token specifically so it
becomes a token-table swap, not a refactor.

---

## 4. Iconography — Lucide

**Lucide** (ISC licence — permissive, AGPL-compatible; record in `/LICENSING.md`).
SVG, stroke-based, 24×24 grid, **1.5 px stroke**, rendered via `QSvgRenderer` and
recoloured at runtime with `currentColor` semantics so one asset serves every state.

Sizes: **16 px** inline/toolbar-dense · **20 px** default toolbar · **24 px** primary actions.

### Icon map (replaces the reference's 28 bespoke icons in `data/icons/`)

| Action | Lucide | | Action | Lucide |
|---|---|---|---|---|
| Load | `folder-open` | | Front view | `user` |
| Save | `save` | | Back view | `rotate-3d` |
| Export | `upload` | | Left / Right | `chevron-left` / `chevron-right` |
| Import | `download` | | Top / Bottom | `chevron-up` / `chevron-down` |
| Undo / Redo | `undo-2` / `redo-2` | | Reset camera | `focus` |
| Reset | `rotate-ccw` | | Grid | `grid-3x3` |
| Smooth (subdivide) | `spline` | | Wireframe | `box` |
| Symmetry | `flip-horizontal-2` | | Pose | `person-standing` |
| Grab screen | `camera` | | Help | `circle-help` |
| Settings | `settings` | | Search | `search` |
| Modelling | `sliders-horizontal` | | Materials | `palette` |
| Geometries | `shirt` | | Rigging | `bone` |
| Render | `image` | | Community | `globe` |
| **Panel menu** | `grip-horizontal` | | Close panel | `x` |

Custom icons are only for concepts Lucide lacks (e.g. a morph-target glyph); they
are drawn on the same 24×24 grid at 1.5 px stroke and live in `resources/icons/custom/`.

---

## 5. Spacing, radius, elevation

4 px base grid. `--space-1: 4px` … `--space-6: 32px` (4/8/12/16/24/32).

| Token | Value | Use |
|---|---|---|
| `--radius-sm` | 3px | inputs, small buttons |
| `--radius-md` | 5px | panels, cards |
| `--radius-lg` | 8px | dialogs, popovers |
| `--dock-title-h` | 28px | dock title bar height |
| `--toolbar-h` | 40px | main toolbar |
| `--row-h` | 24px | list/slider row |
| `--touch-min` | 24px | minimum hit target (pointer-based desktop) |

Elevation is border + shadow, never a colour shift alone:
`--elev-1` panels `0 1px 2px #0006`; `--elev-2` popovers `0 4px 12px #0009`;
`--elev-3` dialogs/floating docks `0 8px 32px #000c`.

---

## 6. Layout and docking

### 6.1 Default workspace ("Modelling")

```
┌──────────────────────────────────────────────────────────────────────────┐
│ ⬤⬤⬤   MakeHuman — untitled.mhm                                          │  title bar
├──────────────────────────────────────────────────────────────────────────┤
│ [open][save][export] │ [undo][redo][reset] │ [smooth][wire][pose][grid]  │  toolbar 40px
│                      │ [symmetry] │ [front][side][top][reset-cam]        │
├───────────────┬──────────────────────────────────────┬───────────────────┤
│ ⠿ Modifiers ✕ │                                      │ ⠿ Properties    ✕ │
│ ┌───────────┐ │                                      │ ┌───────────────┐ │
│ │ ▾ Macro   │ │                                      │ │ Name          │ │
│ │  Gender ──●│ │                                      │ │ Tags          │ │
│ │  Age   ──● │ │            VIEWPORT                  │ │ Measurements  │ │
│ │  Muscle ─● │ │                                      │ └───────────────┘ │
│ │ ▸ Face    │ │                                      ├───────────────────┤
│ │ ▸ Torso   │ │                                      │ ⠿ Assets        ✕ │
│ │ ▸ Arms    │ │                                      │ ┌───┬───┬───┐     │
│ └───────────┘ │                                      │ │ ▣ │ ▣ │ ▣ │     │
│               │                                      │ └───┴───┴───┘     │
├───────────────┴──────────────────────────────────────┴───────────────────┤
│ ● Ready · 19,158 verts · 18,486 faces · 60 fps            [progress ▭▭▭ ] │  status 22px
└──────────────────────────────────────────────────────────────────────────┘
```

`⠿` is the six-dot grip (Lucide `grip-horizontal`) — §6.3.

### 6.2 Docking behaviour

- `QMainWindow` + `QDockWidget`, nested docking and tabbed docks enabled.
- **Snapping**: dragging a dock shows a translucent `--accent-muted` drop
  indicator; edges snap within 12 px. Drop targets: the four window edges, any
  existing dock edge, and the centre of a dock (→ tabify).
- **Floating**: a dock dragged out becomes a `--elev-3` utility window that stays
  above the main window and remembers its geometry.
- **Resize**: splitter handles are 4 px, hit area 8 px, `--border-subtle`,
  `--accent` on hover.
- The viewport is the central widget — it is never a dock and cannot be closed.

### 6.3 The six-dot panel menu

Every dock title bar carries, at its **right**: the `grip-horizontal` six-dot
button, then `x` to close. Clicking the grip opens:

```
┌─────────────────────────┐
│  ⌷  Float               │
│  ◧  Dock Left           │
│  ◨  Dock Right          │
│  ⬒  Dock Top            │
│  ⬓  Dock Bottom         │
│ ─────────────────────── │
│  ⊞  Tab with…         ▸ │
│  ⧉  Duplicate           │
│ ─────────────────────── │
│  ↺  Reset This Panel    │
│  ✕  Close               │
└─────────────────────────┘
```

The grip is also the **drag handle** — press and drag it to move the dock, exactly
as the six-dot affordance implies. Double-clicking the title bar toggles
float/dock. This is the "famous six icon top right of a panel" behaviour.

### 6.4 Workspaces

- Presets shipped: **Modelling**, **Rigging**, **Materials**, **Export**.
- Switcher lives at the right of the toolbar; `⌘1`–`⌘4` switch directly.
- **Save Workspace As…** captures the current layout under a user name.
- **Reset Workspace** restores the preset; **Reset All Workspaces** in Settings.
- Serialised to `~/Library/Application Support/MakeHuman/workspaces/<name>.json`:
  the Qt `saveState()` blob (base64) plus a schema version plus per-panel state.
  On a version mismatch the layout falls back to the preset **with a notice** —
  never a silent loss and never a crash.

---

## 7. Components

### 7.1 Slider (the most-used control in the app)

```
 Gender                                    0.500  [⇄]
 ├──────────────●───────────────────────────────┤
 female                                      male
```

- 24 px row; 4 px track, `--bg-input`; fill `--accent` from the origin (centre for
  bipolar `[-1,1]`, left for unipolar `[0,1]` — matching
  `legacy/python/apps/humanmodifier.py:453-459`).
- Numeric field is editable and mono; commits on Enter or blur.
- Drag = continuous update; release = commit + undo entry. This mirrors the
  reference's onChanging/onRelease split (`legacy/python/lib/modifierslider.py:114-172`),
  which exists for good performance reasons.
- Shift = fine (0.1×). Alt/⌥-click = reset to default. Scroll adjusts by one step.
- `⇄` toggles symmetry linking for paired modifiers.
- Optional thumbnail strip for targets that ship one.

### 7.2 Asset browser

Grid of thumbnails, 96 px default (72/96/128 selectable). Search field filters by
name and tag; tag chips below the search filter cumulatively. Empty state names
what is missing and offers the download action — important, because **clothes,
hair, and eyebrows do not ship in-repo** and must be fetched.

### 7.3 Other components

- **Buttons**: primary (`--accent` fill), secondary (`--bg-elevated` + border),
  ghost (text only), destructive (`--danger`). 28 px high, 12 px horizontal padding.
- **Inputs**: 24 px, `--bg-input`, 1 px `--border-strong`, focus = 2 px `--accent`.
- **Collapsible section**: `chevron-right` → `chevron-down`, 28 px header,
  state persisted per panel.
- **Progress**: determinate bar in the status bar for load/export/bake; modal
  only when the operation genuinely blocks. Always cancellable when cancellation
  is safe.
- **Toast**: bottom-right, `--elev-2`, auto-dismiss 4 s, stacks to 3, never
  covers the status bar.
- **Dialogs**: `--elev-3`, 480 px default, actions bottom-right, primary rightmost.

---

## 8. Interaction

### 8.1 Viewport navigation (defaults preserve the reference's bindings,
`legacy/python/core/mhmain.py:197-202`)

| Input | Action |
|---|---|
| LMB drag | Orbit |
| MMB drag / ⌥ + LMB | Pan |
| RMB drag / scroll | Zoom |
| ⌃ + RMB | Focus point |
| Double-click a body part | Frame that part |
| `1`–`6` | Front / Back / Left / Right / Top / Bottom |
| `0` | Reset camera |
| `F` | Frame selection |

All bindings are remappable and stored **symbolically** — not as raw Qt enum
integers, which is what the reference does (`legacy/python/core/mhmain.py:1021-1027`)
and which does not survive a Qt version change.

### 8.2 Feedback timing

| Delay | Response |
|---|---|
| < 100 ms | none — feels instant |
| 100 ms – 1 s | cursor busy state |
| 1 s – 5 s | status-bar determinate progress |
| > 5 s | progress + cancel; modal only if truly blocking |

### 8.3 States every interactive element must define

default · hover · focus (keyboard) · active/pressed · selected · disabled ·
loading · error · empty. A component is not done until all applicable states exist.

---

## 9. Accessibility

- **Full keyboard operability.** Every action reachable without a mouse; visible
  focus ring at all times; logical tab order; no keyboard traps.
- **VoiceOver**: accessible name, role, and value on every control.
  `QAccessible` properties are set, not left to inference.
- **Contrast**: AA minimum for all text, AAA for primary. Verified with a
  contrast checker, not by eye.
- **Motion**: honour `NSWorkspace` reduce-motion; animations ≤ 200 ms and always
  skippable. The reference animates camera transitions
  (`legacy/python/core/mhmain.py:1573-1635`) — keep, but make them respect the setting.
- **Text scaling**: layouts must survive 200% without clipping or overlap.
- **No colour-only signals.**

---

## 10. Information architecture

Nine top-level task groups, carried over from the reference's category structure
so existing users are not lost (see `architecture.md` §I.8 for the full 50-task
inventory):

```
Files       Load · Save · Import · Export
Modelling   Macro · Face · Torso · Arms & Legs · Body shapes · Measure · Custom · Random
Geometries  Eyes · Eyebrows · Eyelashes · Teeth · Tongue · Hair · Clothes · Topologies
Materials   Skin · Per-asset materials
Pose        Skeleton · Pose · Expressions · Animations
Render      Render · Scene · Viewer
Utilities   Scripting · Targets · Material editor · Logs
Community   Download assets · Mass produce
Settings    General · Appearance · Shortcuts · Mouse · Plugins · Workspaces
```

**Import** is new — the reference has no import capability at all (verified: zero
importer machinery in `legacy/python/`).

---

## 11. Implementation notes

- Styling via a single QSS generated from the token table in
  `resources/themes/dark.qss`, so a token change never requires hunting literals.
  **Do not** repeat the reference's mistake of `url()` paths relative to the
  process CWD (`legacy/python/data/themes/makehuman.qss:193`) — use Qt resources.
- Do **not** subclass `QCommonStyle` and hand-forward virtuals; the reference does
  this only because PyQt lacked `QProxyStyle`
  (`legacy/python/lib/qtgui.py:233-288`). C++ has `QProxyStyle`.
- HiDPI is automatic in Qt 6; the reference's pre-`QApplication` settings scan
  (`legacy/python/lib/core.py:60-78`) is obsolete and is not ported.
- Every string is translatable via `tr()`. The reference bakes strings at widget
  construction so language changes need a restart
  (`legacy/python/plugins/5_settings_0_settings.py:126`) — we support live switching.
- RTL must actually work. The reference reads an `rtl` setting and never applies
  it (verified: no `setLayoutDirection` anywhere).
