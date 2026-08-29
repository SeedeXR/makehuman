# Mixamo bone order — the rig naming standard for this project

**Decision (project owner, 2026-08-29): the skeleton uses the standard Mixamo
FBX bone naming and ordering.** This document records that standard as
*measured*, not as remembered.

## Provenance

Extracted with assimp 6.0.4 from the seven reference clips in
`references/human_based_fbx_mixamo_animations/` (see `LICENSING.md` §5.4).

All seven files carry a **byte-identical 65-bone hierarchy** — verified by
diffing the extracted name lists pairwise, 7/7 identical, 0 differing. The
clips contain `meshes=0, anims=1`: they are animation-only, so they define the
skeleton and the naming, not geometry.

## The `mixamorig:` prefix

Every bone is prefixed `mixamorig:`. The prefix is part of the name in the file
and most DCC round-trips preserve it. Strip or keep it at the **boundary**
(import/export), never in the middle of the rig code, so there is exactly one
place that knows about it.

## The `$AssimpFbx$` trap

assimp does **not** hand back this clean hierarchy by default. It decomposes
each FBX node transform into extra synthetic nodes:

```
mixamorig:Hips_$AssimpFbx$_Translation
  mixamorig:Hips_$AssimpFbx$_PreRotation
    mixamorig:Hips_$AssimpFbx$_Rotation
      mixamorig:Hips
```

A naive node walk therefore reports ~190 nodes, not 65, and every parent index
is wrong. Any importer must either collapse `$AssimpFbx$` nodes into their
owning bone or read the pre-decomposition transform. This is recorded because
it is invisible until the rig is already wrong.

## Comparison with the MakeHuman default skeleton

MakeHuman's own default rig is **163 bones** (`memory/project_context.md` §6).
Mixamo's is 65. Mixamo is therefore a *retarget target*, not a replacement:
the mapping is many-to-one in places (MakeHuman's spine and hand detail exceed
Mixamo's), and any MakeHuman→Mixamo export is lossy by construction. The
mapping table belongs in M5 and does not exist yet.

## The hierarchy, in file order

Index is depth-first order as it appears in the FBX. Indentation is depth.

```
    0  mixamorig:Hips
    1    mixamorig:Spine
    2      mixamorig:Spine1
    3        mixamorig:Spine2
    4          mixamorig:Neck
    5            mixamorig:Head
    6              mixamorig:HeadTop_End
    7          mixamorig:RightShoulder
    8            mixamorig:RightArm
    9              mixamorig:RightForeArm
   10                mixamorig:RightHand
   11                  mixamorig:RightHandThumb1
   12                    mixamorig:RightHandThumb2
   13                      mixamorig:RightHandThumb3
   14                        mixamorig:RightHandThumb4
   15                  mixamorig:RightHandIndex1
   16                    mixamorig:RightHandIndex2
   17                      mixamorig:RightHandIndex3
   18                        mixamorig:RightHandIndex4
   19                  mixamorig:RightHandMiddle1
   20                    mixamorig:RightHandMiddle2
   21                      mixamorig:RightHandMiddle3
   22                        mixamorig:RightHandMiddle4
   23                  mixamorig:RightHandRing1
   24                    mixamorig:RightHandRing2
   25                      mixamorig:RightHandRing3
   26                        mixamorig:RightHandRing4
   27                  mixamorig:RightHandPinky1
   28                    mixamorig:RightHandPinky2
   29                      mixamorig:RightHandPinky3
   30                        mixamorig:RightHandPinky4
   31          mixamorig:LeftShoulder
   32            mixamorig:LeftArm
   33              mixamorig:LeftForeArm
   34                mixamorig:LeftHand
   35                  mixamorig:LeftHandThumb1
   36                    mixamorig:LeftHandThumb2
   37                      mixamorig:LeftHandThumb3
   38                        mixamorig:LeftHandThumb4
   39                  mixamorig:LeftHandIndex1
   40                    mixamorig:LeftHandIndex2
   41                      mixamorig:LeftHandIndex3
   42                        mixamorig:LeftHandIndex4
   43                  mixamorig:LeftHandMiddle1
   44                    mixamorig:LeftHandMiddle2
   45                      mixamorig:LeftHandMiddle3
   46                        mixamorig:LeftHandMiddle4
   47                  mixamorig:LeftHandRing1
   48                    mixamorig:LeftHandRing2
   49                      mixamorig:LeftHandRing3
   50                        mixamorig:LeftHandRing4
   51                  mixamorig:LeftHandPinky1
   52                    mixamorig:LeftHandPinky2
   53                      mixamorig:LeftHandPinky3
   54                        mixamorig:LeftHandPinky4
   55    mixamorig:RightUpLeg
   56      mixamorig:RightLeg
   57        mixamorig:RightFoot
   58          mixamorig:RightToeBase
   59            mixamorig:RightToe_End
   60    mixamorig:LeftUpLeg
   61      mixamorig:LeftLeg
   62        mixamorig:LeftFoot
   63          mixamorig:LeftToeBase
   64            mixamorig:LeftToe_End
```

## Regenerating

This table is measured, not hand-written. To re-extract after changing the
reference clips, walk the node tree with assimp, skip any name containing
`$AssimpFbx$`, and emit depth-first order — the procedure used above.
