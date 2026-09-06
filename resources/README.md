# resources/

Runtime assets for the application shell. Distinct from `data/`, which holds the
CC0 MakeHuman asset set (meshes, targets, skins) inherited from the reference.

| Directory | Contents | Licence |
|---|---|---|
| `fonts/` | 42dot Sans, variable weight | OFL-1.1 (`fonts/OFL.txt`) |
| `icons/lucide/` | 57 UI icons | ISC (`icons/lucide/LICENSE`) |
| `icons/custom/` | Icons for concepts Lucide lacks | AGPL-3.0 (ours) |
| `shaders/rhi/` | Ports to Qt RHI GLSL 450 | AGPL-3.0 |

## Fonts

`42dotSans-VariableFont_wght.ttf`, 5,772,308 bytes — one variable file covering
the whole weight axis, so the UI needs no separate Regular/Medium/Bold files.
Verified structurally after download (23 sfnt tables, `fvar` present, no table
extending past EOF): a truncated download still reports as "TrueType Font data"
to `file`, and the first attempt here *was* truncated at 1.29 MB.

## Icons

Lucide v1.37.0, the subset named in `memory/design.md` §4 plus the common UI
verbs. Two deliberate changes from upstream:

- `stroke-width` is rewritten from Lucide's `2` to the **1.5** the design system
  specifies (`design.md` §4). All 57 re-validated as well-formed XML afterwards.
- Nothing else is touched: `stroke="currentColor"` is preserved so one asset
  serves every state and theme, recoloured at render time.

## Shaders

`shaders/rhi/` holds the Qt RHI GLSL 450 ports. The GLSL 120 originals are NOT
copied here -- they already live in `data/shaders/glsl/`, and a second copy
would drift. Each port names its source file in its header.

**These are translations of AGPL shader source and are therefore AGPL-3.0.**
They must never move into a permissive module (`LICENSING.md` §4).

Ported so far: **litsphere** (the headline material — every skin uses it).
Remaining: phong, normalmap, skin, toon, xray — sources in
`data/shaders/glsl/`, tracked in `memory/todo.md` M6.

Compiled by the build: `src/render/CMakeLists.txt` globs this directory and
runs `qsb` on every `.vert`/`.frag`, so a new stage needs no CMake edit. Each
produces a `.qsb` carrying SPIR-V, GLSL 450 and Metal. Verified: the `0.495`
litsphere constant survives into the generated MSL.
