# Mixamo compatibility without losing MakeHuman's rig

**Question:** can the rig carry Mixamo's 65 bones *and* MakeHuman's 163, so the
model is easy to animate from Mixamo without the rig getting worse?

**Answer: yes, and it makes the rig bigger, not smaller — 163 bones become 179.**

The six headline numbers below (65, 163, 49, 16, 179, 114) are printed by
`tools/mixamo_mapping.py`, which fails CI if the mapping stops holding. The
region breakdown is a hand-made summary for orientation — it is *not* checked by
anything, and an earlier version of it had two wrong rows. Treat the tool's
output as authoritative and this table as a sketch.

## Why "just use Mixamo's skeleton" would be a large downgrade

Both skeletons were measured — MakeHuman's from `data/rigs/default.mhskel`,
Mixamo's from the seven reference clips (see `mixamo_bone_order.md`).

MakeHuman's 163, counted by name prefix:

| group | n | | group | n |
|---|---:|---|---|---:|
| face and other | 59 | | metacarpals | 8 |
| finger bones | 30 | | root/pelvis/breast | 5 |
| toe bones | 28 | | spine | 5 |
| arm bones | 14 | | neck/head | 4 |
| leg bones | 8 | | feet | 2 |

Mixamo's 65: 40 finger, 8 arm, 6 foot/toe, 4 spine+hips, 4 leg, 3 neck/head.

The shape of it: **Mixamo has no facial rig at all** (59 bones lost), one ball
joint where MakeHuman articulates five toes (28 lost), no metacarpals (8 lost),
and no twist bones. It leads in exactly one place — a 4th joint per finger — and
that joint is a *tip locator*, not a deforming bone.

## The shape of the solution: a superset

Keep every MakeHuman bone. Make every Mixamo name resolve to one of them. Add
only what is genuinely absent.

- **49 of Mixamo's 65** already exist in MakeHuman under a different name.
  `Spine2` is `spine01`, `LeftForeArm` is `lowerarm01.L`, and so on.
- **16 must be added**: `Hips`, 10 fingertips, `HeadTop_End`, 2 × `Toe_End`,
  2 × `ToeBase`.
- **114 MakeHuman bones have no Mixamo counterpart** and simply stay.

`163 + 16 = 179`.

### Hips is not `root`, and this one would have broken every retarget

Structurally `root` is the only candidate: it alone parents both legs *and* the
spine, exactly as Mixamo's `Hips` parents both `UpLeg`s and `Spine`. So a
name-and-hierarchy mapping picks it, and the ancestry check is satisfied.

It is still wrong. Measured from the joint vertex groups:

```
root     head = (0, 0.5639, -0.7609)   tail = (0, 0.7268, 0.1445)
spine05  head = (0, 0.7268,  0.1445)   <- root's tail
pelvis.L head = (0, 0.7268,  0.1445)   <- the same point
```

The legs and the spine meet at `root`'s **tail**, 0.92 dm from its head. Mixamo's
`Hips` pivot sits on that junction. Binding `Hips` to `root` would swing the
whole character about a point ~9 cm behind and below the sacrum — on every
rotation, and on every root-motion translation, which is where all the
locomotion in these clips lives.

So the superset adds a `hips` bone at `(0, 0.7268, 0.1445)`, reparenting
`pelvis.L/R` and `spine05` under it. That is the 16th addition, and the reason
this is 179 rather than 178.

Worth noting for the implementation: `root`'s rest direction is mostly *forward*
`(0, +0.163, +0.905)` while Mixamo's `Hips` points *up* toward `Spine`, so the
hip rotation basis needs an explicit rest offset rather than a naive name bind.

### One arm choice that is a judgement call, not a fact

`LeftArm -> upperarm01.L` is the conventional humerus and carries the skin
weight, but geometrically Mixamo's `Arm` sits at 12.6% along the arm arc where
`shoulder01.L` is at 12.4% and `upperarm01.L` at 20.8%; the humerus segment
lengths agree the same way. The consequence of the current choice is that
`shoulder01` stays rigid while the retargeted arm swings beneath it — the
classic deltoid-collapse artifact. Recorded as a decision to revisit when the
skeleton is built and can be looked at, not as a settled fact.

Thirteen of the sixteen are **leaf markers** — they exist to give the previous
bone a direction. That is not an assumption: Blender reports which bones carry
animation channels, and across **all seven** reference clips the answer is the
same every time —

```
bones carrying animation channels: 52 of 65
leaf bones that ARE animated     : 0 of 13
```

52 = the 49 Mixamo bones MakeHuman already has, plus `Hips` and the two
`ToeBase`. **Every bone Mixamo actually drives is either one we already have or
one of the three real bones the superset adds.** The 13 markers are never keyed,
so adding them costs nothing and changes no deformation.

Three of the sixteen are real bones: `hips` above, and the two below. Mixamo's `ToeBase` is the **ball of the
foot**: one bone parenting every toe. MakeHuman hangs all five toes directly off
`foot`, so there is nothing to map to — and no way to express a foot roll. Adding
`ball.L/R` between `foot` and the toes gives Mixamo's `ToeBase` a true home *and*
gives MakeHuman a joint it was missing. This is the one place the superset gains
a weight-bearing bone rather than a marker.

## What the mapping is anchored on, and what the check does not prove

`Spine2` maps to `spine01` because `spine01` is the **only** MakeHuman bone
parenting both clavicles and the neck, exactly as `Spine2` parents both shoulders
and the neck. That one is forced by the hierarchy and the check confirms it:
mapping `Spine2` to `spine03` instead — which looks perfectly reasonable — is
rejected with

```
Spine2 -> spine03, but its parent Spine1 -> spine02 is not an ancestor of spine03
```

**Most of the mapping is not pinned that tightly, and the checks are necessary
rather than sufficient.** `Spine -> spine03` satisfies ancestry with `spine04`
or `spine05` too; what actually picks `spine03` is that it sits 0.1 cm away once
both rigs are scaled to a common frame — a measurement the tool does not make.

Three checks run, and each exists because it caught something:

| check | catches |
|---|---|
| ancestry | a bone mapped outside its own chain |
| injectivity | two Mixamo bones landing on one MakeHuman bone (the index finger driving the pinky passed silently before this, while coverage still read 50) |
| laterality | a **mirrored** rig — ancestry alone accepts swapping every left and right bone at once, since that preserves every parent relationship |

That last one matters: an earlier draft of this document claimed ancestry
rejected left-to-right mappings. It does not. A *single* swapped bone is caught
by its children; a whole-side mirror is not, and a whole-side mirror is the
version of that bug that actually happens.

### The geometric oracle, and why the obvious version does not work

Comparing bone **positions** looks like the obvious oracle and is wrong. The two
rigs are in different rest poses, so error accumulates down the arm — measured,
0.13 at the shoulder, 0.40 at the elbow, 0.83 at the wrist, over 1.0 at the
fingers — and 42 of 49 mappings "fail" while being perfectly correct.

What *is* pose-invariant is **arc length along a chain**: bone lengths do not
change when a rig moves. Expressing each bone as a percentage of its chain's
total length gives a measure both rigs agree on:

| chain | Mixamo | MakeHuman |
|---|---|---|
| `Spine` | 18.1% | `spine03` 19.1% |
| `Spine1` | 36.0% | `spine02` 33.6% |
| `Spine2` | 52.6% | `spine01` 57.7% |
| `Neck` | 82.6% | `neck01` 81.9% |
| `LeftShoulder` | 0.0% | `clavicle.L` 0.0% |
| `LeftForeArm` | 57.7% | `lowerarm01.L` 63.5% |
| `LeftLeg` | 43.2% | `lowerleg01.L` 44.3% |
| `LeftFoot` | 86.5% | `foot.L` 84.2% |

This settled the arm question with a number rather than an argument: `LeftArm`
is at **16.2%**, `shoulder01.L` at **16.0%**, `upperarm01.L` at **26.8%**. It is
now mapped to `shoulder01.L`.

**A chain root needs a different check.** A root is at 0% on both sides by
definition, so the arc measure can never fault it — it was blind to precisely
the error it was built for. Chain roots are therefore checked by position
against where the MakeHuman chain actually starts, which catches the original
mistake outright:

```
Hips maps to root, which sits 0.920 dm (14.2% of the chain) from spine05,
where that chain actually starts
```

Rest positions are measured once with Blender and committed to
`mixamo_rest_pose.json`, so the check runs in CI without Blender. Regenerate by
walking `armature.data.bones[].head_local` after importing any of the reference
clips.

## The evidence chain, verified end to end

The mapping tool reads Mixamo's hierarchy from the table in
`mixamo_bone_order.md`, which was extracted with **assimp**. If that table had
drifted from the files, the mapping would inherit the error silently.

So it was checked against the real FBX with a **second, independent importer** —
Blender's, which shares no code with assimp:

```
Blender (from FBX): 65 bones
doc  (parsed table): 65 bones
in FBX but not the doc : none
in doc but not the FBX : none
differing parents      : none
```

Repeat it with:

```bash
blender -b --python dump_rig.py -- references/human_based_fbx_mixamo_animations/run_forward.fbx
```

walking `armature.data.bones` for `(name, parent.name)`. Blender is not wired
into CI — it is a heavyweight dependency for a table that changes only when the
reference clips do — but this is the procedure when they change.

## What this buys

- **Mixamo animations retarget by name.** Every `mixamorig:` bone has a home, so
  a downloaded clip binds without a hand-built retarget table.
- **Nothing is lost.** Face, twists, toes and metacarpals all survive; they are
  simply bones Mixamo never drives.
- **The rig gets better**, not merely compatible: +16 bones, including a real
  ball-of-foot joint and a correctly-placed hip pivot.

## Open question, not yet answerable here

Whether Mixamo's **online auto-rigger** preserves an uploaded skeleton or always
replaces it with its own 65 could not be verified: web search was unavailable and
the Adobe help pages timed out. It does not change the design — the superset is
built for *retargeting downloaded animations*, which is the path the reference
clips in this repo actually exercise — but "upload our rig and have Mixamo keep
it" remains unproven and should not be claimed.

## Status

The mapping and its proof exist. **The superset skeleton itself is not built
yet** — that is the implementation step, and it needs the 16 bones added to
`default.mhskel` (or a derived `mixamo_superset.mhskel`) plus weights for
`ball.L/R`.
