// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include "makehuman/rig/RetargetMap.h"
#include "makehuman/rig/Skinning.h"

#include <optional>

namespace mh::rig {

/// Collapses a skin onto the smaller skeleton a retarget map describes.
///
/// WHY THIS EXISTS. Two things are wanted from one character and no single
/// skeleton gives both. Working needs OUR rig: 179 bones, 59 of them in the
/// face, because eye aiming, the jaw, expressions and FACS are all bone-driven
/// here. Handing the character on needs THEIRS: Mixamo's 65, under Mixamo's
/// names, because that is what Mixamo, Unity's Humanoid importer and Unreal
/// recognise. Mixamo's skeleton has no face at all.
///
/// Switching the working rig to Mixamo's was tried and MEASURED: it fails 25
/// tests, and they are features rather than counts -- `--look-at` answers "the
/// skeleton has no eye bones", and eyelid follow, jaw-driven teeth and tongue,
/// eyelashes, every expression and all of FACS stop. So the conversion happens
/// at EXPORT instead, and the session keeps its face.
///
/// WHAT HAPPENS TO THE BONES THAT HAVE NO COUNTERPART. Their influence is
/// given to the nearest ancestor that HAS one, never dropped. `spine04` is not
/// a Mixamo bone; the band of torso it holds still has to be driven by
/// something, and the something is whichever ancestor Mixamo does have.
/// Dropping it instead would leave a ring of vertices weighted to nothing --
/// which a consumer renders as a collapsed waist, not as an error.
///
/// @param skin   the skin to reduce, as `buildSkinData` produced it.
/// @param toSource target bone name -> source bone name, i.e. the retarget map
///        read in the direction `data/rigs/<naming>_retarget.json` stores it.
/// @return the reduced skin, or nullopt when @p skin names none of the
///         map's sources -- which means the map and the rig disagree, and a
///         silent empty skeleton would be worse than a refusal.
[[nodiscard]] std::optional<SkinData> reduceSkin(const SkinData& skin, const RetargetMap& toSource);

}  // namespace mh::rig
