// SPDX-License-Identifier: AGPL-3.0-or-later
#include "makehuman/rig/PosedMesh.h"

#include "makehuman/rig/Skinning.h"

namespace mh::rig {

std::expected<void, PoseError> poseMesh(core::Mesh& mesh, PoseRig& rig, PoseOptions options) {
    if (!rig.posed()) return {};

    // Posing switched off. The mesh is already at its morph base -- the
    // caller's rebuild put it there -- so there is nothing to undo, but the
    // live-rig capture MUST be cleared: an exporter reads a non-empty
    // `restCoords` as "this mesh is posed, swap it for the rest one", and those
    // coordinates go stale the moment a modifier moves.
    if (!options.apply) {
        rig.restCoords.clear();
        rig.globalPose.clear();
        return {};
    }

    if (!rig.skeleton.updateJoints(mesh.coord()) || !rig.skeleton.buildRestMatrices()) {
        return std::unexpected(PoseError::RefitFailed);
    }
    const auto skinning = computeSkinningMatrices(rig.skeleton, rig.localPose);

    // Kept for a live-rig export, which ships THESE vertices and lets the
    // consumer pose them. Captured before skinning, because afterwards the
    // rest positions are gone.
    rig.restCoords.assign(mesh.coord().begin(), mesh.coord().end());
    rig.globalPose.clear();
    rig.globalPose.reserve(rig.skeleton.bones.size());
    for (size_t b = 0; b < rig.skeleton.bones.size(); ++b) {
        const foundation::Mat4& rest = rig.skeleton.bones[b].matRestGlobal;
        rig.globalPose.push_back(b < skinning.size() ? skinning[b] * rest : rest);
    }

    // LBS by default, DQS when asked. DQS costs more and is indistinguishable
    // wherever the bones do not disagree much -- which is most of a body most
    // of the time -- so it is opt-in rather than a silent change to every
    // existing export.
    // Correctives, if any: AFTER the re-fit and the rest capture above, BEFORE
    // the skinning below. See PoseOptions::correctives for why the order is not
    // the caller's to choose.
    //
    // `setRest` every time rather than once at bind, because the body may have
    // been re-morphed since: this is the character-static path, and the deltas
    // have to go on the shape that exists now.
    std::span<const foundation::Vec3> toSkin = mesh.coord();
    if (options.correctives != nullptr) {
        if (!options.correctives->setRest(mesh.coord())) {
            return std::unexpected(PoseError::CorrectiveFailed);
        }
        if (!options.correctives->update(rig.localPose)) {
            return std::unexpected(PoseError::CorrectiveFailed);
        }
        toSkin = options.correctives->positions();
    }

    std::vector<foundation::Vec3> posed;
    const bool skinned = options.method == SkinningMethod::DualQuaternion
                             ? skinPositionsDqs(toSkin, rig.weights, skinning, posed)
                             : skinPositions(toSkin, rig.weights, skinning, posed);
    if (!skinned) return std::unexpected(PoseError::SkinningFailed);

    // changeCoords, not setCoords: posing must not redefine the morph base the
    // next rebuild resets to, or every rebuild poses on top of the last one.
    if (!mesh.changeCoords(std::move(posed))) return std::unexpected(PoseError::StoreFailed);
    return {};
}

}  // namespace mh::rig
