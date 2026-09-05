// SPDX-License-Identifier: AGPL-3.0-or-later
#include "makehuman/core/Random.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <random>
#include <set>

namespace mh::core {
namespace {

/// The groups each flag turns on (`0_modeling_8_random.py:110-120`).
constexpr std::string_view kMacroGroups[]  = {"macrodetails", "macrodetails-universal",
                                              "macrodetails-proportions"};
constexpr std::string_view kHeightGroups[] = {"macrodetails-height"};
constexpr std::string_view kFaceGroups[]   = {"eyebrows", "eyes", "chin", "forehead", "head",
                                              "mouth",    "nose", "neck", "ears",     "cheek"};
constexpr std::string_view kBodyGroups[]   = {"pelvis", "hip",      "armslegs", "stomach",
                                              "breast", "buttocks", "torso"};

/// Tighter than the rest: these two read as deformities well before they reach
/// the range the others use (`:133-135`).
bool isVeryTight(std::string_view fullName) {
    return fullName == "forehead/forehead-nubian-less|more" ||
           fullName == "forehead/forehead-scale-vert-less|more";
}

/// The lateral translations, which move a feature off the midline. Under full
/// symmetry the reference pins them to their default rather than mirroring
/// them, because there is no opposite modifier to mirror INTO -- the pair is
/// the feature and itself (`:136-146`).
bool isLateralTranslation(std::string_view fullName) {
    static constexpr std::string_view kNames[] = {
        "hip/hip-trans-in|out",   "torso/torso-trans-in|out", "neck/neck-trans-in|out",
        "head/head-trans-in|out", "nose/nose-trans-in|out",   "mouth/mouth-trans-in|out"};
    return std::ranges::find(kNames, fullName) != std::ranges::end(kNames);
}

/// Per-group spread (`:147-157`). The face wants a tenth of its range or every
/// character is a caricature; the macro scalars want three tenths or every
/// character is the same person.
float sigmaFor(const Modifier& m) {
    if (isVeryTight(m.fullName)) return 0.02F;
    static constexpr std::string_view kFine[] = {"head", "forehead", "eyebrows", "neck",  "eyes",
                                                 "nose", "ears",     "chin",     "cheek", "mouth"};
    if (std::ranges::find(kFine, m.group) != std::ranges::end(kFine)) return 0.1F;
    if (m.group == "macrodetails") return 0.3F;
    return 0.1F;
}

bool wanted(const Modifier& m, const RandomOptions& o) {
    const auto in = [&m](auto& groups) {
        return std::ranges::find(groups, m.group) != std::ranges::end(groups);
    };
    return (o.macro && in(kMacroGroups)) || (o.height && in(kHeightGroups)) ||
           (o.face && in(kFaceGroups)) || (o.body && in(kBodyGroups));
}

}  // namespace

char symmetrySide(std::string_view modifierName) noexcept {
    size_t start = 0;
    while (start <= modifierName.size()) {
        const size_t dash           = modifierName.find('-', start);
        const std::string_view part = modifierName.substr(
            start, dash == std::string_view::npos ? std::string_view::npos : dash - start);
        if (part == "l") return 'l';
        if (part == "r") return 'r';
        if (dash == std::string_view::npos) break;
        start = dash + 1;
    }
    return '\0';
}

std::string symmetricOpposite(const Modifier& m) {
    if (symmetrySide(m.name) == '\0') return {};

    std::string flipped;
    flipped.reserve(m.name.size());
    size_t start = 0;
    while (true) {
        const size_t dash        = m.name.find('-', start);
        const std::string_view p = std::string_view(m.name).substr(
            start, dash == std::string::npos ? std::string_view::npos : dash - start);
        flipped += (p == "l") ? "r" : (p == "r") ? "l" : std::string(p);
        if (dash == std::string::npos) break;
        flipped += '-';
        start = dash + 1;
    }
    return m.group + "/" + flipped;
}

float randomValue(float minValue, float maxValue, float middle, float sigmaFactor,
                  uint64_t& state) {
    const float range = std::fabs(maxValue - minValue);
    // A zero-width range has one answer, and normal_distribution with sigma 0
    // is undefined behaviour rather than a constant.
    if (range == 0.0F) return minValue;

    std::mt19937_64 rng(state);
    std::normal_distribution<float> gauss(middle, sigmaFactor * range);
    float v = gauss(rng);
    state   = rng();  // carry the stream forward for the next draw

    // REFLECT, do not clamp -- see the header. Reflection can still land
    // outside when the draw is more than a full range away, which is why the
    // clamp below stays.
    if (v < minValue) {
        v = minValue + std::fabs(v - minValue);
    } else if (v > maxValue) {
        v = maxValue - std::fabs(v - maxValue);
    }
    return std::clamp(v, minValue, maxValue);
}

std::vector<std::pair<std::string, float>> randomize(Human& human, const RandomOptions& options,
                                                     uint64_t seed) {
    std::vector<const Modifier*> chosen;
    for (const Modifier& m : human.modifiers()) {
        if (wanted(m, options)) chosen.push_back(&m);
    }

    // Shuffled so dependent modifiers do not always resolve in the same order.
    // The three ethnic scalars renormalise each other, so whichever is set last
    // effectively wins; a fixed order would make one of them systematically
    // dominant across every character a user generates (`:125-127`).
    std::mt19937_64 shuffler(seed);
    std::ranges::shuffle(chosen, shuffler);

    uint64_t state = shuffler();
    std::map<std::string, float> values;

    for (const Modifier* m : chosen) {
        if (values.contains(m->fullName)) continue;
        const float sigma = sigmaFor(*m);

        float v = 0.0F;
        if (isLateralTranslation(m->fullName)) {
            if (options.symmetry >= 1.0F) {
                v = m->defaultValue;
            } else {
                // Narrow the range around the default in proportion to how much
                // asymmetry was asked for, then draw inside it.
                const float w =
                    std::fabs(m->maxValue() - m->minValue()) * (1.0F - options.symmetry);
                const float lo = std::max(m->minValue(), m->defaultValue - w / 2.0F);
                const float hi = std::min(m->maxValue(), m->defaultValue + w / 2.0F);
                v              = randomValue(lo, hi, m->defaultValue, 0.1F, state);
            }
        } else {
            v = randomValue(m->minValue(), m->maxValue(), m->defaultValue, sigma, state);
        }
        values[m->fullName] = v;

        const std::string opposite = symmetricOpposite(*m);
        if (opposite.empty() || values.contains(opposite)) continue;
        const Modifier* other = human.findModifier(opposite);
        if (other == nullptr) continue;

        if (options.symmetry >= 1.0F) {
            values[opposite] = v;
        } else {
            const float deviation =
                (1.0F - options.symmetry) * std::fabs(other->maxValue() - other->minValue()) / 2.0F;
            const float lo   = std::clamp(v - deviation, other->minValue(), other->maxValue());
            const float hi   = std::clamp(v + deviation, other->minValue(), other->maxValue());
            values[opposite] = randomValue(lo, hi, v, sigma, state);
        }
    }

    // See the header: the reference's guard is `Age < 0.75` where its own
    // comment says "too old", so it fires on nearly every character. This is
    // the stated intent, not the shipped condition.
    const auto valueOr = [&values](const char* key, float fallback) {
        const auto it = values.find(key);
        return it == values.end() ? fallback : it->second;
    };
    const bool noPregnancy = valueOr("macrodetails/Gender", 0.0F) > 0.5F ||
                             valueOr("macrodetails/Age", 0.5F) < 0.2F ||
                             valueOr("macrodetails/Age", 0.5F) > 0.75F;
    if (noPregnancy) {
        if (const auto it = values.find("stomach/stomach-pregnant-decr|incr"); it != values.end()) {
            it->second = 0.0F;
        }
    }

    std::vector<std::pair<std::string, float>> applied;
    applied.reserve(values.size());
    for (const auto& [name, v] : values) {
        if (!human.setModifierValue(name, v)) continue;  // a name with no live modifier
        applied.emplace_back(name, v);
    }
    return applied;
}

}  // namespace mh::core
