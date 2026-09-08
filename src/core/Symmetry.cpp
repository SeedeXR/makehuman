// SPDX-License-Identifier: AGPL-3.0-or-later
#include "makehuman/core/Symmetry.h"

namespace mh::core {

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

std::vector<std::pair<std::string, float>> symmetrise(Human& human, char targetSide) {
    if (targetSide != 'l' && targetSide != 'r') return {};
    const char source = targetSide == 'l' ? 'r' : 'l';

    std::vector<std::pair<std::string, float>> changed;
    // Iterating `modifiers()` while setting values is safe: setModifierValue
    // writes the value map and the macro scalars, never the modifier list.
    for (const Modifier& m : human.modifiers()) {
        if (symmetrySide(m.name) != source) continue;
        // setModifierValue is the only guard needed: it returns false for a
        // name it does not know, which is exactly the half-installed pair the
        // reference would have raised on. A separate findModifier check ahead
        // of it looked careful and tested nothing.
        const std::string opposite = symmetricOpposite(m);
        const float before         = human.modifierValue(opposite);
        if (!human.setModifierValue(opposite, human.modifierValue(m.fullName))) continue;
        const float after = human.modifierValue(opposite);
        if (after == before) continue;  // already mirrored; nothing to undo
        changed.emplace_back(opposite, after);
    }
    return changed;
}

std::vector<std::pair<std::string, float>> mirroredEdit(const Human& human,
                                                        std::string_view fullName, float value) {
    const Modifier* m = human.findModifier(fullName);
    if (m == nullptr) return {};

    std::vector<std::pair<std::string, float>> edit{{m->fullName, value}};
    const std::string opposite = symmetricOpposite(*m);
    // A modifier with no side has no mirror, and half a pair has none either --
    // 291 of the shipped modifiers are in the first case.
    if (!opposite.empty() && human.findModifier(opposite) != nullptr) {
        edit.emplace_back(opposite, value);
    }
    return edit;
}

}  // namespace mh::core
