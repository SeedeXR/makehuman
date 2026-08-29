# `.target` — morph target

## Purpose and provenance

A sparse list of per-vertex displacements applied to the base mesh. Everything
MakeHuman does to a body — age, gender, muscle, every slider — is a weighted sum
of these.

No specification exists. The definition is `legacy/python/shared/targets.py` and
`algos3d.py`; this document and `src/core/Target.cpp` are the port of it.

**1,280 shipped**, CC0. Reader: `src/core/Target.cpp`.

## Grammar

Line-oriented ASCII. That is the whole grammar:

```
<line>    ::= <comment> | <blank> | <entry>
<comment> ::= '#' ...            # '#' as the FIRST token only
<entry>   ::= <index> <dx> <dy> <dz>
```

- `<index>` — a 0-based vertex index into the base mesh.
- `<dx> <dy> <dz>` — the displacement, in mesh units (decimetres).

Fields are whitespace-separated. There are no sections, no header, and no
declared count.

## A real example

`data/targets/head/head-oval.target`, first data lines:

```
0 -.004 0 0
2 -.005 0 0
4 -.005 0 0
```

Note `-.004` — a **leading-dot float with no integer part**. This is common
throughout the shipped targets, so a parser that requires a digit before the
point fails on real data.

## Semantics

- **Units**: decimetres, matching the mesh.
- **Coordinates**: Y-up, model faces +Z, right-handed.
- **Sparsity**: a target touches a few thousand of the base mesh's 19,158
  vertices. `head-oval` touches 2,143. Vertices not listed do not move.
- **Application**: `coord[v] += offset * (scale * factor)` — additive, so
  multiple targets compose.
- **Zero rows are legal and present.** `nose/nose-base-up.target` has 305 rows,
  **11 of which are literally `(0, 0, 0)`**. They are no-ops. This matters when
  counting: a consumer that reports "moved vertices" will report 294, not 305,
  and that discrepancy looks like a bug until it is traced.
- **Out-of-range indices**: the reference indexes unguarded. We skip them, and
  `Target::maxVertexIndex` lets a caller validate a target against a mesh once
  rather than per application.

## Our support

| | |
|---|---|
| Read | yes — all 1,280 shipped targets parse, 0 failures, 6,147,800 entries |
| Write | no |
| Deliberately dropped | nothing |

`expandTargetToRenderVertices` densifies a target onto the unwelded render
vertices for glTF/FBX morph export, giving every copy of a UV-seam vertex the
same delta so the seam does not tear.

## Compatibility

Exported as glTF and FBX blend shapes; verified in **Blender 5.2** by
moved-vertex count (`tools/run_blender_validation.sh`).

## Known issues in the reference

- Applies targets without bounds-checking the vertex index
  (`algos3d.py`), so a target authored against a different base mesh reads out
  of bounds rather than reporting anything.
- Parsing the full set takes **3,225 ms** in Python against **465 ms** here;
  see `benchmarks/baseline_python.json`.
