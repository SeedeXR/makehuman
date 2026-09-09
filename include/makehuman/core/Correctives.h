// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Per-frame corrective application: rest mesh plus a weighted sum of sparse
// deltas, BEFORE skinning.
//
// The third stage of the pose-space deformer owner directive 12.3 describes.
// `foundation::SwingTwist` turns a joint rotation into a signal vector,
// `foundation::Rbf` turns that into a weight vector, and this turns the weight
// vector into moved geometry.
//
// **Pre-skin, in rest space**, which directive 12.2 commits to: the deltas go
// on before skinning so the skinning transform carries them, and they therefore
// compose with the body-shape targets rather than fight them. A corrective
// authored on one body type degrades gracefully on another instead of
// exploding.
//
// A corrective IS a `core::Target`. That type is already a sparse list of
// vertex indices and offsets -- MakeHuman's entire modelling primitive -- and
// inventing a second one for the same thing would mean two loaders, two
// validators and two ways to be wrong about vertex ordering.
//
// What is genuinely new is the per-frame shape, from directive 12.5: a dense
// scratch buffer, a dirty-index list built from the union of the active
// correctives, and a reset that touches only those vertices. The
// character-static path replays the whole 364-target morph stack from the morph
// base (`Human::applyStack`), which is right when a slider moves and far too
// slow for a frame.
#pragma once

#include "makehuman/core/Target.h"
#include "makehuman/core/Types.h"

#include <cstddef>
#include <span>
#include <vector>

namespace mh::core {

/// Holds a shaped rest mesh and produces `rest + sum(weight[c] * delta[c])`.
///
/// Reused across frames: the buffer keeps the positions and the list of
/// vertices the last call moved, so the next call restores only those before
/// accumulating again.
class CorrectiveBuffer {
public:
    /// Adopts @p rest as the shape to deform, and forgets the previous frame.
    ///
    /// Called from the CHARACTER-STATIC path -- whenever a slider changes the
    /// body and the shaped rest mesh is rebuilt. Forgetting matters: the dirty
    /// list refers to deltas that were applied to the OLD positions, and
    /// undoing them against the new ones would corrupt the new shape.
    void setRest(std::span<const Vec3> rest);

    /// Restores the vertices the last call moved, then accumulates the active
    /// correctives at their weights.
    ///
    /// @p correctives and @p weights are parallel, one weight per corrective,
    /// and a corrective at weight zero is skipped. Accumulation runs over the
    /// correctives in the order given and, within each, over its own vertex
    /// order -- fixed, because directive 12.5 warns that reordering float
    /// additions makes the same input give different output.
    ///
    /// A weight is used exactly as given, including a negative one: an RBF
    /// interpolant overshoots between example poses, and clamping here would
    /// quietly change what was authored.
    ///
    /// @return false, having written nothing, if the arrays do not pair up, a
    ///         corrective is null or ragged, or one indexes past the rest mesh.
    ///         Refused as a whole rather than per vertex -- a corrective
    ///         sculpted against a different topology is an authoring error, and
    ///         applying the part of it that happens to fit gives a character
    ///         that is subtly wrong with nothing reported. Checked for EVERY
    ///         corrective including inactive ones, or the error surfaces only
    ///         once an animation happens to activate it.
    [[nodiscard]] bool apply(std::span<const Target* const> correctives,
                             std::span<const float> weights);

    /// The deformed positions. Empty until `setRest`.
    [[nodiscard]] std::span<const Vec3> positions() const noexcept { return positions_; }

    /// How many vertices the last `apply` moved: the size of the dirty list.
    ///
    /// Exposed so the reset can be tested at all. Without it the optimisation
    /// is invisible -- a buffer that reset all 19,158 vertices every frame
    /// would pass every correctness test in the file.
    [[nodiscard]] size_t touched() const noexcept { return dirty_.size(); }

private:
    std::vector<Vec3> rest_;
    std::vector<Vec3> positions_;
    /// Vertices moved by the last apply, each listed once.
    std::vector<uint32_t> dirty_;
    /// Membership test for building `dirty_` without sorting: 1 while v is in
    /// the list. Cleared alongside the undo pass, so neither is ever a walk
    /// over the whole mesh.
    ///
    /// A generation counter was the first version of this and was replaced: it
    /// needed an extra member, and after 2^32 frames the counter wraps to the
    /// value an untouched vertex already holds, which would drop that vertex
    /// from the dirty list for one frame. Clearing what was set costs the same
    /// and has nothing to wrap.
    std::vector<uint8_t> marked_;
};

}  // namespace mh::core
