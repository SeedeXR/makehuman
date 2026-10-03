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

    // Shuffled so dependent modifiers do not always resolve in the same order:
    // a symmetric pair is drawn from whichever side comes first, so a fixed
    // order would bias every character the same way (`:125-127`). The three
    // ethnic scalars used to need this too, because each renormalised the ones
    // before it; they are now applied together and normalised once, so their
    // outcome no longer depends on any order.
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

    // COUPLING, after the independent draw and before the pregnancy guard, so
    // the guard sees the character that will actually be built.
    //
    // Opt-in: with `correlated` false nothing below runs and the output is the
    // reference's, which the parity tests pin.
    if (options.correlated && options.macro) {
        const auto get = [&values](const char* key, float fallback) {
            const auto it = values.find(key);
            return it == values.end() ? fallback : it->second;
        };
        const auto put = [&values](const char* key, float v) {
            const auto it = values.find(key);
            if (it != values.end()) it->second = std::clamp(v, 0.0F, 1.0F);
        };

        // ONE latent per character, drawn from the same stream so the result
        // stays deterministic in the seed. Think of it as "how heavy-set is
        // this person", which is the axis muscle and weight share.
        const float build = randomValue(0.0F, 1.0F, 0.5F, 0.25F, state);

        // Muscle and weight pulled toward that shared axis. HALF, not all:
        // at 1.0 every character would sit on a line through the square and
        // the pair would carry one degree of freedom instead of two, which
        // trades one implausible population for another. At 0.5 the
        // independent draw still decides half of each.
        constexpr float kShare = 0.5F;
        const float muscle     = get("macrodetails-universal/Muscle", 0.5F);
        const float weight     = get("macrodetails-universal/Weight", 0.5F);
        float newMuscle        = muscle + kShare * (build - muscle);
        const float newWeight  = weight + kShare * (build - weight);

        // AGE GATES MUSCLE, one way only. A child at maximum muscle is a
        // caricature, and the reference's own randomiser produces one as often
        // as anything else. Below the quarter mark the ceiling closes
        // smoothly; above it nothing is changed, so an adult's draw is
        // untouched rather than quietly compressed.
        const float age = get("macrodetails/Age", 0.5F);
        if (age < 0.25F) {
            const float ceiling = 0.3F + 1.2F * age;  // 0.3 at age 0, 0.6 at 0.25
            newMuscle           = std::min(newMuscle, ceiling);
        }

        put("macrodetails-universal/Muscle", newMuscle);
        put("macrodetails-universal/Weight", newWeight);
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

    // Applied in ONE call so the three ethnic values normalise together
    // rather than each rescaling the two drawn before it.
    const std::vector<std::pair<std::string, float>> drawn(values.begin(), values.end());
    human.setModifierValues(drawn);

    // Reported as the character ENDED UP, not as drawn: renormalisation scales
    // the ethnic three, and these values become the undo step's "after" state
    // and the panel's slider positions. Returning the drawn value there would
    // put back a character the randomiser never produced.
    std::vector<std::pair<std::string, float>> applied;
    applied.reserve(drawn.size());
    for (const auto& [name, v] : drawn) {
        applied.emplace_back(name, human.modifierValue(name));
    }
    return applied;
}

}  // namespace mh::core
