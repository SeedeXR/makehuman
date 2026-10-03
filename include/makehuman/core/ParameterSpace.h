// SPDX-License-Identifier: AGPL-3.0-or-later
//
// The character's parameter space: what the dimensions ARE, what they range
// over, and how to move between a `Human` and a plain vector of floats.
//
// M10's first item, and the prerequisite for the two after it. A generative
// model over the modifier vector needs a vector to be over, and a fit from an
// image needs somewhere to put its answer. Both need the same thing first: an
// ordered, bounded, round-trippable description of a body.
//
// THE CONSTRAINT THIS IS BUILT AROUND is already recorded in memory/todo.md
// under M10's headless-determinism item: "a parameter vector is an ordered list
// where ethnicity is concerned, or ethnicity is sampled as the normalised
// triple it already is. A plain unordered map of slider values is not a
// complete specification of a body." Setting African, Asian and Caucasian one
// at a time renormalises the other two each time, so the order decides the
// result -- MEASURED there at 19,158 of 19,158 vertices moving by up to 3 cm.
// This type handles that rather than leaving it to every caller.
#pragma once

#include "makehuman/core/Modifier.h"

#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace mh::core {

/// One dimension of the space.
struct Parameter {
    /// The modifier's `fullName`, e.g. `macrodetails/Gender`.
    std::string name;
    /// Inclusive bounds. `[-1,1]` for a bipolar modifier, `[0,1]` for a
    /// unipolar one -- `Modifier::minValue/maxValue`, which follow whether the
    /// modifier has a left side.
    float minValue{0.0F};
    float maxValue{1.0F};
    float defaultValue{0.0F};
    /// One of the three ethnic components, which are COUPLED: they are
    /// renormalised to sum to 1 and cannot be set independently. Sampling draws
    /// them together and `fromVector` applies them together.
    bool ethnic{false};
};

/// An ordered, bounded description of every modifier a `Human` carries.
class ParameterSpace {
public:
    /// Builds the space from a human's loaded modifiers.
    ///
    /// SORTED BY NAME, not left in load order. The order is the meaning of a
    /// vector -- element 7 is only a particular slider if everyone agrees which
    /// -- so it must not depend on the order a JSON file happened to list
    /// things, nor change when a custom modifier directory is added. Sorting by
    /// `fullName` makes the mapping a property of the NAMES, which are stable.
    [[nodiscard]] static ParameterSpace of(const Human& human);

    [[nodiscard]] std::span<const Parameter> parameters() const noexcept { return parameters_; }

    [[nodiscard]] size_t size() const noexcept { return parameters_.size(); }

    /// The human's current values, in this space's order.
    [[nodiscard]] std::vector<float> toVector(const Human& human) const;

    /// Applies @p values to @p human, and returns how many were recognised.
    ///
    /// Returns 0 without touching @p human when @p values is not exactly
    /// `size()` long: a short vector would silently leave the tail at whatever
    /// the human already had, which is a different character than the caller
    /// asked for and reads as a sampling bug rather than a size bug.
    ///
    /// Goes through `Human::setModifierValues`, NOT a loop over
    /// `setModifierValue`, because that is the call that blocks the ethnic
    /// renormalisation until all three are in and normalises once. A loop here
    /// would make the result depend on this space's sort order, which is
    /// exactly the dependence the sort was meant to remove.
    uint32_t fromVector(std::span<const float> values, Human& human) const;

    /// A uniformly random point in the space, deterministic in @p seed.
    ///
    /// Each independent dimension is uniform over its own range. **The ethnic
    /// triple is NOT**: three independent uniforms divided by their sum is not
    /// uniform on the simplex -- it concentrates near the middle, so a crowd
    /// sampled that way would have far fewer strongly-one-ethnicity faces than
    /// chance allows. The triple is drawn as a Dirichlet(1,1,1) instead, via
    /// normalised exponentials, which is uniform on the simplex by
    /// construction.
    ///
    /// This is a UNIFORM sample of the parameter space, which is not the same
    /// as a plausible human: uniform draws put the gender slider at 0.5 as
    /// often as at 0, and real populations are not uniform in anything. A
    /// distribution over PEOPLE is M10's generative-model item, and
    /// `core::randomize` already offers the reference's hand-tuned Gaussian for
    /// the cases that want a believable face today.
    [[nodiscard]] std::vector<float> sample(uint64_t seed) const;

private:
    std::vector<Parameter> parameters_;
};

}  // namespace mh::core
