// SPDX-License-Identifier: AGPL-3.0-or-later
#include "makehuman/core/Modifier.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <fstream>

namespace mh::core {
namespace {

/// Minimal reader for the modifier JSON, whose shape is fixed and shallow:
/// a list of objects with a "group" string and a "modifiers" list of flat
/// string-keyed objects. Not a general JSON parser -- see the note in
/// loadModifiers about why one is not pulled in for this.
struct JsonCursor {
    const std::string& text;
    size_t pos{};

    void skipWs() {
        while (pos < text.size() && (std::isspace(static_cast<unsigned char>(text[pos])) != 0)) {
            ++pos;
        }
    }

    bool consume(char c) {
        skipWs();
        if (pos < text.size() && text[pos] == c) {
            ++pos;
            return true;
        }
        return false;
    }

    [[nodiscard]] bool peek(char c) {
        skipWs();
        return pos < text.size() && text[pos] == c;
    }

    std::optional<std::string> readString() {
        skipWs();
        if (pos >= text.size() || text[pos] != '"') return std::nullopt;
        ++pos;
        std::string out;
        while (pos < text.size() && text[pos] != '"') {
            if (text[pos] == '\\' && pos + 1 < text.size()) ++pos;
            out.push_back(text[pos++]);
        }
        if (pos >= text.size()) return std::nullopt;
        ++pos;
        return out;
    }

    /// Reads a scalar (string, number, bool, null) as text.
    std::optional<std::string> readScalar() {
        skipWs();
        if (pos >= text.size()) return std::nullopt;
        if (text[pos] == '"') return readString();
        const size_t start = pos;
        while (pos < text.size() && text[pos] != ',' && text[pos] != '}' && text[pos] != ']')
            ++pos;
        std::string out = text.substr(start, pos - start);
        while (!out.empty() && (std::isspace(static_cast<unsigned char>(out.back())) != 0)) {
            out.pop_back();
        }
        return out;
    }
};

std::string lowerFirst(std::string s) {
    if (!s.empty()) s[0] = static_cast<char>(std::tolower(static_cast<unsigned char>(s[0])));
    return s;
}

}  // namespace

float Modifier::clamp(float v) const noexcept {
    return std::clamp(v, minValue(), maxValue());
}

std::string ModifierError::message() const {
    const char* k = "unknown error";
    switch (kind) {
        case ModifierErrorKind::NotFound: k = "file not found"; break;
        case ModifierErrorKind::Unreadable: k = "file unreadable"; break;
        case ModifierErrorKind::Malformed: k = "malformed JSON"; break;
    }
    std::string m = file + ": " + k;
    if (!detail.empty()) m += " (" + detail + ")";
    return m;
}

std::expected<std::vector<Modifier>, ModifierError> loadModifiers(
    const std::filesystem::path& jsonPath) {
    std::error_code ec;
    if (!std::filesystem::exists(jsonPath, ec)) {
        return std::unexpected(ModifierError{ModifierErrorKind::NotFound, jsonPath.string(), {}});
    }
    std::ifstream in(jsonPath);
    if (!in) {
        return std::unexpected(ModifierError{ModifierErrorKind::Unreadable, jsonPath.string(), {}});
    }
    const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());

    std::vector<Modifier> out;
    JsonCursor c{text};

    if (!c.consume('[')) {
        return std::unexpected(
            ModifierError{ModifierErrorKind::Malformed, jsonPath.string(), "expected a list"});
    }

    while (!c.peek(']')) {
        if (!c.consume('{')) break;

        std::string group;
        std::vector<std::unordered_map<std::string, std::string>> entries;

        while (!c.peek('}')) {
            const auto key = c.readString();
            if (!key || !c.consume(':')) break;

            if (*key == "group") {
                if (const auto v = c.readString()) group = *v;
            } else if (*key == "modifiers") {
                if (!c.consume('[')) break;
                while (!c.peek(']')) {
                    if (!c.consume('{')) break;
                    std::unordered_map<std::string, std::string> e;
                    while (!c.peek('}')) {
                        const auto k2 = c.readString();
                        if (!k2 || !c.consume(':')) break;
                        if (const auto v2 = c.readScalar()) e[*k2] = *v2;
                        if (!c.consume(',')) break;
                    }
                    c.consume('}');
                    entries.push_back(std::move(e));
                    if (!c.consume(',')) break;
                }
                c.consume(']');
            } else {
                (void)c.readScalar();
            }
            if (!c.consume(',')) break;
        }
        c.consume('}');

        for (const auto& e : entries) {
            Modifier m;
            m.group = group;

            const auto macro = e.find("macrovar");
            if (macro != e.end()) {
                // humanmodifier.py:681-686: a macrovar entry is a MacroModifier
                // unless modifierType names EthnicModifier.
                const auto type = e.find("modifierType");
                m.kind          = (type != e.end() && type->second == "EthnicModifier")
                                      ? ModifierKind::Ethnic
                                      : ModifierKind::Macro;
                m.macroVariable = macro->second;
                m.name          = macro->second;
                // The reference leaves left/right/center unset on a macro
                // modifier and resolves its targets from the group name
                // (humanmodifier.py:566). Keep that distinction.
                m.targetGroup  = group;
                m.defaultValue = (m.kind == ModifierKind::Ethnic) ? (1.0F / 3.0F) : 0.5F;
                if (m.kind == ModifierKind::Ethnic) {
                    m.macroValue = macroValueFromToken(lowerFirst(macro->second));
                }
            } else {
                const auto target = e.find("target");
                if (target == e.end()) continue;
                m.kind = ModifierKind::Universal;

                const std::string base = group + "-" + target->second;
                const auto lo          = e.find("min");
                const auto hi          = e.find("max");
                const auto mid         = e.find("mid");

                if (lo != e.end() && hi != e.end()) {
                    m.left  = base + "-" + lo->second;
                    m.right = base + "-" + hi->second;
                    if (mid != e.end()) {
                        m.center = base + "-" + mid->second;
                        m.name   = target->second + "-" + lo->second + "|" + mid->second + "|" +
                                 hi->second;
                    } else {
                        m.name = target->second + "-" + lo->second + "|" + hi->second;
                    }
                } else {
                    m.right = base;
                    m.name  = target->second;
                }
                m.defaultValue = 0.0F;
            }

            if (const auto dv = e.find("defaultValue"); dv != e.end()) {
                m.defaultValue = std::strtof(dv->second.c_str(), nullptr);
            }

            // humanmodifier.py:162-163: '/' becomes '-' in both parts.
            std::ranges::replace(m.group, '/', '-');
            std::ranges::replace(m.name, '/', '-');
            m.fullName = m.group + "/" + m.name;
            out.push_back(std::move(m));
        }

        if (!c.consume(',')) break;
    }

    return out;
}

// ------------------------------------------------------------------ Human ---
Human::Human(const TargetIndex* index, std::vector<Modifier> modifiers)
    : index_(index), modifiers_(std::move(modifiers)) {
    for (const Modifier& m : modifiers_)
        values_[m.fullName] = m.defaultValue;
    rebuildStack();
}

const Modifier* Human::findModifier(std::string_view fullName) const {
    const auto it =
        std::ranges::find_if(modifiers_, [&](const Modifier& m) { return m.fullName == fullName; });
    return it == modifiers_.end() ? nullptr : &*it;
}

float Human::modifierValue(std::string_view fullName) const {
    const auto it = values_.find(std::string{fullName});
    return it == values_.end() ? 0.0F : it->second;
}

bool Human::setModifierValue(std::string_view fullName, float value) {
    const Modifier* m = findModifier(fullName);
    if (m == nullptr) return false;

    values_[m->fullName] = m->clamp(value);

    // A macro modifier drives a scalar, whose derived per-value weights then
    // re-weight every target that depends on that category -- so the whole
    // stack is rebuilt rather than just this modifier's slice.
    if (m->kind != ModifierKind::Universal) {
        const std::string& v = m->macroVariable;
        if (v == "Gender") {
            factors_.setGender(values_[m->fullName]);
        } else if (v == "Age") {
            factors_.setAge(values_[m->fullName]);
        } else if (v == "Muscle") {
            factors_.setMuscle(values_[m->fullName]);
        } else if (v == "Weight") {
            factors_.setWeight(values_[m->fullName]);
        } else if (v == "Height") {
            factors_.setHeight(values_[m->fullName]);
        } else if (v == "BreastSize") {
            factors_.setBreastSize(values_[m->fullName]);
        } else if (v == "BreastFirmness") {
            factors_.setBreastFirmness(values_[m->fullName]);
        } else if (v == "BodyProportions") {
            factors_.setBodyProportions(values_[m->fullName]);
        } else if (v == "African") {
            factors_.setAfrican(values_[m->fullName]);
        } else if (v == "Asian") {
            factors_.setAsian(values_[m->fullName]);
        } else if (v == "Caucasian") {
            factors_.setCaucasian(values_[m->fullName]);
        }
    }

    rebuildStack();
    return true;
}

void Human::accumulate(const Modifier& m, float value) {
    if (index_ == nullptr) return;

    // humanmodifier.py:536-545. Each side contributes its signed share of the
    // slider; a macro modifier contributes 1.0 and lets the scalars weight it
    // (:607-610).
    struct Side {
        const std::string& group;
        float factor;
    };

    const float leftFactor   = -std::min(value, 0.0F);
    const float rightFactor  = std::max(0.0F, value);
    const float centerFactor = 1.0F - std::abs(value);

    const bool universal = m.kind == ModifierKind::Universal;
    const std::array<Side, 3> sides{
        // A macro modifier contributes 1.0 from its group and lets the derived
        // scalars weight the targets (humanmodifier.py:607-610).
        {{universal ? m.left : std::string{}, leftFactor},
         {universal ? m.right : m.targetGroup, universal ? rightFactor : 1.0F},
         {universal ? m.center : std::string{}, centerFactor}}};

    for (const Side& s : sides) {
        if (s.group.empty() || s.factor == 0.0F) continue;
        for (const TargetComponent& c : index_->group(s.group)) {
            const float w = targetWeight(c, factors_, 1.0F, s.factor);
            if (w == 0.0F) continue;  // zero weights never enter the stack
            // ASSIGNMENT, not accumulation. setDetail writes
            // targetsDetailStack[name] = value (human.py:918-921), so when
            // several macro modifiers resolve to the same group -- Gender, Age
            // and the three ethnic ones all point at "macrodetails" -- the last
            // write wins rather than the weights summing. They compute the same
            // value, so the group still contributes exactly 1.0 in total.
            stack_[c.relativePath] = w;
        }
    }
}

void Human::rebuildStack() {
    stack_.clear();
    for (const Modifier& m : modifiers_) {
        accumulate(m, values_[m.fullName]);
    }
    // human.py:918-921 -- a falsy weight deletes the entry rather than storing
    // it. Accumulation can cancel to zero, so prune afterwards too.
    std::erase_if(stack_, [](const auto& kv) { return kv.second == 0.0F; });
}

}  // namespace mh::core
