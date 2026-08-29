# `.mhw` — vertex bone weights

## Purpose and provenance

Which bones move which vertices, and by how much. JSON.

No specification. The definition is `VertexBoneWeights` in
`legacy/python/shared/animation.py:494-620`. Reader:
`src/rig/VertexWeights.cpp`.

**1 shipped**: `data/rigs/default_weights.mhw`, CC0.

## Grammar

```json
{
  "name": "MakeHuman weights",
  "description": "Symmetric weights for default makehuman mesh",
  "version": 110,
  "copyright": "...", "license": "CC0",
  "weights": {
    "<bone name>": [ [<vertex index>, <weight>], ... ],
    ...
  }
}
```

`weights` is the only key that carries data; the rest is metadata.

## A real example

```json
"breast.L": [
    [1399, 0.016], [1400, 0.018], [1401, 0.022], [1402, 0.027],
```

## Semantics

The four rules below are each load-bearing, and none is stated in the file.

1. **The numbers are RELATIVE, not absolute.** The total weight on a vertex is
   summed across *every* bone first, then each weight is stored as
   `w / total[v]`. Storing the raw values scales every vertex by an arbitrary
   factor. After loading, each vertex's weights sum to exactly 1.
2. **A vertex listed twice under one bone is merged**, not overwritten.
3. **Sub-threshold weights are dropped after normalising**, at `1e-4`.
4. **A vertex with no weight at all binds to the root bone at weight 1.**
   Without this it collapses to the origin the moment the rig is posed — which
   reads as a modelling bug, not a loader bug.

**Measured on the shipped file**: 139 of the rig's 163 bones carry weight;
57,107 entries; the most-influenced vertex has **12** bones.

## Truncation to 4

glTF's `JOINTS_0`/`WEIGHTS_0` are 4-wide, so `compile(skeleton, 4)` keeps the
four strongest influences and **re-normalises** — without that, every
heavily-weighted vertex loses mass and drifts. 5,923 of 19,158 vertices actually
take that path.

Ties break by **descending bone index**, matching Python's
`sorted(reverse=True)` over `(weight, index)` tuples. That is arbitrary, and it
is replicated because on a mirrored body two bones can carry identical weights,
so it decides which influence survives.

## Our support

| | |
|---|---|
| Read | yes — parity on all 139 bones and 57,107 entries |
| Write | no |
| Deliberately dropped | nothing |

## Compatibility

Exported through glTF and FBX; **Blender 5.2** confirms all 21,833 render
vertices are skinned, in 139 vertex groups.

## Known issues in the reference

Indexes the weight array unguarded, so a `.mhw` loaded against a smaller mesh
reads out of bounds. `loadWeights` validates every index and reports
`VertexOutOfRange`.
