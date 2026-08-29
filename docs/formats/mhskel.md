# `.mhskel` — skeleton

## Purpose and provenance

The bone hierarchy, and the vertex clouds that position it. JSON.

No specification. The definition is `legacy/python/shared/skeleton.py:87-140`.
Reader: `src/rig/Skeleton.cpp`.

**1 shipped**: `data/rigs/default.mhskel` — 163 bones, 326 joints, 163 rotation
planes. CC0.

## Grammar

```json
{
  "name": "default", "description": "...", "version": 110,
  "copyright": "...", "license": "CC0", "tags": [...],
  "weights_file": "default_weights.mhw",
  "joints": { "<joint name>": [<vertex index>, ...], ... },
  "planes":  { "<plane name>": [<joint>, <joint>, <joint>], ... },
  "bones": {
    "<bone name>": {
      "head": "<joint name>",
      "tail": "<joint name>",
      "parent": "<bone name>" | null,
      "rotation_plane": "<plane name>" | 0,
      "reference": null | [...],
      "weights_reference": null | [...]
    }, ...
  }
}
```

## A real example

```json
"breast.L": {
  "head": "breast.L____head",
  "parent": "spine02",
  "reference": null,
  "rotation_plane": "breast.L____plane",
  "tail": "breast.L____tail"
}
```

## Semantics

### A joint is a vertex cloud, not a point

A joint's position is the **mean** of its listed base-mesh vertices
(`skeleton.py:428-434`). That is what makes the rig follow the body: change the
mesh, recompute the means, and every bone moves with it. `updateJoints` computes
all 326, not just the ones bones use — rotation planes name joints no bone
references.

### There are TWO orderings, and they are not the same one

This is the single easiest thing to get wrong here, because a wrong answer still
looks like a valid skeleton.

1. **`fromFile`** (`skeleton.py:111-124`) relaxes over the bone map **in file
   order** — each pass appends every bone whose parent is already placed. The
   reference calls this breadth-first; it is not. What it fixes is the order
   children attach to each parent.
2. **`getBones()`** then does a **real** breadth-first walk from the roots. *That*
   is the canonical index order: the row order of the rest-matrix arrays and the
   order every exporter writes.

Implementing only (1) put **153 of 163 bones in the wrong slot** while still
producing a perfectly valid parents-first list. Because (1) depends on file
order, the JSON parser must preserve key order — hence `nlohmann::ordered_json`.

### Rest matrices

Each bone gets an orthonormal basis anchored at its head:

```
Y = normalize(tail - head)
Z = normalize(cross(normal, Y))
X = normalize(cross(Y, Z))          <- NOT the input normal
```

The plane normal only *seeds* the basis; it is generally not perpendicular to
the bone, which is why X is rebuilt. The normal itself is
`normalize(cross(normalize(p3-p2), normalize(p2-p1)))` over the plane's three
joints — **argument order matters**: swapping it flips every bone's roll by 180
degrees and still yields a valid orthonormal basis.

Axes are stored as **columns**, translation in the last column.

## Our support

| | |
|---|---|
| Read | yes — exact bone-order parity, and element-wise rest-matrix parity on all 163 bones |
| Write | no |
| Deliberately dropped | `reference` / `weights_reference` — null on every shipped bone |

## Compatibility

Exported through glTF and FBX; **Blender 5.2** reports 163 bones in 1 armature.

## Known issues in the reference

- A bone whose parent does not exist is **dropped with a warning**
  (`skeleton.py:122-124`) and the skeleton is built anyway. We make it an error:
  a rig that exports missing limbs with nothing in the log is worse than a
  refusal.
- `matrix.normalize` divides unguarded, so a zero-length bone or collapsed plane
  yields inf/nan that propagates into every child matrix.
- The whole skeleton path reaches `G.app.selectedHuman`, so it cannot run
  headlessly without a stub — which is why no Python benchmark exists for it.
