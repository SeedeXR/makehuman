# `.mhclo` / `.proxy` — proxy mesh

## Purpose and provenance

A mesh fitted to the body by barycentric reference: clothes, hair, eyes, teeth,
or an alternate body topology. As the body changes shape, the proxy follows.

No specification. The definition is `legacy/python/shared/proxy.py:376-543`.
Reader: `src/core/Proxy.cpp`.

**4 shipped**: `data/3dobjs/base.mhclo`, `data/3dobjs/a7_converter.proxy`, and
the two eye meshes. CC0.

## Grammar

Line-oriented, whitespace-split, `#` comments. Keys, then blocks.

| Key | Meaning |
|---|---|
| `name`, `description`, `uuid`, `tag`, `version` | metadata |
| `basemesh` | the base mesh it was authored against |
| `obj_file` | the proxy's own geometry |
| `material` | a `.mhmat`; `.mhmat` is appended if absent |
| `z_depth` | render order |
| `max_pole` | doubled on load (`proxy.py:540`) |
| `x_scale` `y_scale` `z_scale` | `<v1> <v2> <den>` — a TMatrix axis |
| `verts` | opens the vertex block |
| `delete_verts` | opens the hidden-vertex block |
| `weights` | opens a block we skip |

### The `verts` block

Either form, one per proxy vertex:

```
<v>                                     # exact: bound to one base vertex
<v0> <v1> <v2> <w0> <w1> <w2> [dx dy dz] # barycentric, with an offset
```

### The `delete_verts` block

Integers, with `-` marking an inclusive range:

```
delete_verts
10
- 14          # deletes 10..14 -- the range CARRIES ACROSS the line break
```

## Semantics

- **Fitting**: `P_i = SUM_k w_ik * H[v_ik] + M * d_i`, where `M` is the TMatrix
  diagonal. The single-index form is `(1,0,0)` weights and a zero offset.
- **The TMatrix rescales offsets as the body changes proportion** — each axis is
  the ratio of a measured span on the *current* body to the span the asset was
  authored against (`proxy.py:900-918`). Without it, clothes keep a fixed
  thickness as the character grows.
- **`z_depth` of exactly `-1` means "absent"** and becomes 50. The reference
  tests `== -1`, not `< 0`, so `z_depth -5` stays -5.
- **`delete_verts` ranges carry across lines**, because `v0` is a function-level
  local in the reference. Treating the state as per-line silently deletes fewer
  vertices.
- **Face hiding**: a face is hidden only when **every** corner is hidden. The
  inverted reading — hide as soon as one corner goes — is the natural-seeming one
  and is wrong. See `Mesh::faceMaskForVisibleVertices`.

## Our support

| | |
|---|---|
| Read | yes — barycentric fit parity on 3 proxies x 2 body shapes |
| Write | no |
| Not exercised by any shipped asset | `TMatrix` shear forms; `delete_verts` (all four files declare zero) |

## Known issues in the reference

- `delete_verts` indices are used unguarded to size an array. A file saying
  `4294967295` caused an **out-of-bounds write** here before the bound check
  (confirmed under ASan), and `0 - 4294967295` looped forever. Indices are now
  capped; see `kMaxDeleteVertIndex`.
- A `verts` line with 2-5 fields is silently skipped by neither implementation
  cleanly: the reference raises `IndexError`; we return `MalformedLine`. Dropping
  it shifts every later proxy vertex onto the wrong reference triangle.
