# `.bvh` — Biovision Hierarchy

## Purpose and provenance

Skeletal motion. **This is where MakeHuman's poses actually live** — there are
no `.mhpose` files in `data/` at all. `data/poses/tpose.bvh` and
`data/poseunits/face-poseunits.bvh` (60 facial pose units) are the shipped
assets.

Unlike the formats above, BVH **is** published. Our reader is therefore written
from the format rather than translated, which is what lets it live in the
Apache-2.0 `mh_io` module: `src/io/BvhReader.cpp`.

## Grammar

```
HIERARCHY
ROOT <name>
{
    OFFSET <x> <y> <z>
    CHANNELS <n> <channel>...
    JOINT <name> { ... }
    End Site { OFFSET <x> <y> <z> }
}
MOTION
Frames: <n>
Frame Time: <seconds>
<value>...            # one row per frame, all channels concatenated
```

Channels are `Xposition` `Yposition` `Zposition` `Xrotation` `Yrotation`
`Zrotation`.

## Semantics

### The up axis is NOT recorded

This is the trap. BVH stores no up-axis metadata and both conventions are in the
wild. **Both shipped MakeHuman files are Z-up.** A reader that assumes Y-up
produces a complete, plausible skeleton **lying on its side** — nothing fails.

`readBvh` measures it: it finds a joint from a known humanoid set that has a
child and compares `|dy|` against `|dz|` along that bone. A humanoid's spine or
leg is longer vertically than in depth, so whichever component dominates names
the up axis.

When Z-up is detected:

| | conversion |
|---|---|
| offsets | `(x, y, z)` -> `(x, z, -y)` |
| `Yposition` | becomes `-z` |
| `Zposition` | becomes `y` |
| `Yrotation` | negated, and its order letter becomes `z` |
| `Zrotation` | its order letter becomes `y` |

### Rotation order

The order string is built by **prepending** each rotation letter as the channels
are read, which reverses them: channels `Xrotation Yrotation Zrotation` mean the
`szyx` convention. Under the Z-up fix the same channels become **`syzx`**.

Angles are passed to `eulerMatrix` in reverse channel order to match.

### Translation

BVH allows position channels on any joint, but for a skeletal pose only the
root's translation is meaningful — letting every joint translate lets a limb
detach from its parent. Default policy is root-only; the pose-unit file is
loaded with translation disabled entirely.

## Our support

| | |
|---|---|
| Read | yes — parity on both shipped files, **12,942 joint-frames**, worst delta < 1e-5 |
| Write | not yet |

**One deliberate improvement**: the reference names every End Site
`"End effector"`, so all 49 collide and none is individually addressable. We
derive `<parent>_end`.

## Pose units

`face-poseunits.json` names the 60 frames via `framemapping`. Mapping onto the
rig walks the **rig's** 163 bones and takes each one's identically-named BVH
joint — the BVH has 212 joints and they are not the same set, so walking the BVH
instead would silently reorder every bone.

Blending units is **order-dependent** and deliberately so: `quat = q_i * quat`,
left-multiplied. Reversing a five-unit blend moves the result by 0.034. Weights
are **not** normalised — the blend is additive.

## Known issues in the reference

`bvh.py:155` uses an invalid escape sequence (`"(.*)_\d+$"` in a non-raw
string), which is a `SyntaxWarning` on modern Python and will eventually be an
error.
