// SPDX-License-Identifier: AGPL-3.0-or-later
#include "makehuman/core/ParameterSpace.h"

#include <algorithm>
#include <cmath>

namespace mh::core {

namespace {

/// The three coupled components, by the macro variable each modifier drives.
///
/// Keyed on `macroVariable` rather than on the modifier's name, because the
/// name is a path the data file chooses and the variable is what
/// `MacroFactors` actually renormalises.
bool isEthnicVariable(std::string_view v) {
    return v == "African" || v == "Asian" || v == "Caucasian";
}

/// SplitMix64. Small, fast, and -- the part that matters here -- specified, so
/// a seed gives the same character on every platform and compiler. A
/// `std::mt19937` seeded the obvious way would too, but `std::uniform_real_
/// distribution` is NOT specified to produce identical values across standard
/// libraries, and a sample that differs between a developer machine and CI is
/// a reproducibility claim this project cannot make.
uint64_t nextBits(uint64_t& state) {
    state += 0x9E3779B97F4A7C15ULL;
    uint64_t z = state;
    z          = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
    z          = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
    return z ^ (z >> 31);
}

/// Uniform in [0,1). 53 bits, which is every bit a double has.
double nextUnit(uint64_t& state) {
    return static_cast<double>(nextBits(state) >> 11) * 0x1.0p-53;
}

}  // namespace

ParameterSpace ParameterSpace::of(const Human& human) {
    ParameterSpace space;
    space.parameters_.reserve(human.modifiers().size());
    for (const Modifier& m : human.modifiers()) {
        space.parameters_.push_back(Parameter{.name         = m.fullName,
                                              .minValue     = m.minValue(),
                                              .maxValue     = m.maxValue(),
                                              .defaultValue = m.defaultValue,
                                              .ethnic       = isEthnicVariable(m.macroVariable)});
    }
    // By NAME, so the mapping from index to slider is a property of the names
    // rather than of the order a file listed them. See the header.
    std::ranges::sort(space.parameters_,
                      [](const Parameter& a, const Parameter& b) { return a.name < b.name; });
    return space;
}

std::vector<float> ParameterSpace::toVector(const Human& human) const {
    std::vector<float> out;
    out.reserve(parameters_.size());
    for (const Parameter& p : parameters_)
        out.push_back(human.modifierValue(p.name));
    return out;
}

uint32_t ParameterSpace::fromVector(std::span<const float> values, Human& human) const {
    // All or nothing. A short vector applied as far as it goes leaves the tail
    // at whatever the human already carried, which is a different person than
    // the caller described and looks like a sampling bug rather than a length
    // bug.
    if (values.size() != parameters_.size()) return 0;

    std::vector<std::pair<std::string, float>> pairs;
    pairs.reserve(parameters_.size());
    for (size_t i = 0; i < parameters_.size(); ++i) {
        const Parameter& p = parameters_[i];
        pairs.emplace_back(p.name, std::clamp(values[i], p.minValue, p.maxValue));
    }
    // One call, because this is the one that blocks the ethnic renormalisation
    // until every value is in. A loop over `setModifierValue` would reintroduce
    // the order dependence the sort exists to remove.
    return human.setModifierValues(pairs);
}

std::vector<float> ParameterSpace::sample(uint64_t seed) const {
    uint64_t state = seed;
    std::vector<float> out(parameters_.size(), 0.0F);

    // The independent dimensions first, so the ethnic draw below does not have
    // its stream position depend on where the triple happens to sort.
    for (size_t i = 0; i < parameters_.size(); ++i) {
        const Parameter& p = parameters_[i];
        if (p.ethnic) continue;
        const double u  = nextUnit(state);
        const double lo = static_cast<double>(p.minValue);
        const double hi = static_cast<double>(p.maxValue);
        out[i]          = static_cast<float>(lo + u * (hi - lo));
    }

    // The coupled triple, as a Dirichlet(1,1,1): three exponentials over their
    // sum is uniform on the simplex. Three uniforms over their sum is NOT --
    // it piles up near (1/3, 1/3, 1/3), so a sampled crowd would be short of
    // strongly-one-ethnicity faces.
    std::vector<size_t> ethnic;
    for (size_t i = 0; i < parameters_.size(); ++i) {
        if (parameters_[i].ethnic) ethnic.push_back(i);
    }
    if (!ethnic.empty()) {
        std::vector<double> e(ethnic.size(), 0.0);
        double total = 0.0;
        for (double& v : e) {
            // -log(1-u) with u in [0,1): 1-u is in (0,1], so the log is finite
            // and the exponential never comes back infinite.
            v = -std::log(1.0 - nextUnit(state));
            total += v;
        }
        // Three exponentials summing to zero is not reachable in floating
        // point -- each is strictly positive -- but dividing by a sum without
        // checking is how a NaN reaches a mesh.
        if (total > 0.0) {
            for (size_t k = 0; k < ethnic.size(); ++k) {
                out[ethnic[k]] = static_cast<float>(e[k] / total);
            }
        }
    }
    return out;
}

}  // namespace mh::core
