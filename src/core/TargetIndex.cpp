// SPDX-License-Identifier: AGPL-3.0-or-later
#include "makehuman/core/TargetIndex.h"

#include <algorithm>

namespace mh::core {
namespace {

/// Splits on '-', '_' and '.', which the reference does by rewriting the latter
/// two to '-' first (targets.py:203).
std::vector<std::string> splitTokens(std::string_view s) {
    std::vector<std::string> out;
    std::string cur;
    for (const char c : s) {
        if (c == '-' || c == '_' || c == '.') {
            if (!cur.empty()) out.push_back(std::move(cur));
            cur.clear();
        } else {
            cur.push_back(c);
        }
    }
    if (!cur.empty()) out.push_back(std::move(cur));
    return out;
}

std::string join(const std::vector<std::string>& parts, char sep) {
    std::string out;
    for (size_t i = 0; i < parts.size(); ++i) {
        if (i != 0) out.push_back(sep);
        out += parts[i];
    }
    return out;
}

/// Folds one path component's tokens into a component being built.
/// @param isRoot true for the first path component, where a literal "targets"
///        is dropped rather than becoming part of the key (targets.py:205-214).
/// @return false if the same category was claimed twice with different values,
///         which the reference treats as a hard error (targets.py:112-113).
bool applyTokens(TargetComponent& c, std::string_view name, bool isRoot) {
    const auto tokens = splitTokens(name);
    for (size_t i = 0; i < tokens.size(); ++i) {
        const std::string& tok = tokens[i];
        if (isRoot && i == 0 && tok == "targets") continue;

        if (const auto v = macroValueFromToken(tok)) {
            const auto slot = static_cast<size_t>(categoryOf(*v));
            if (c.data[slot].has_value() && *c.data[slot] != *v) return false;
            c.data[slot] = *v;
        } else if (tok == "target") {
            // The extension, dropped rather than keyed (targets.py:128-129).
        } else {
            c.key.push_back(tok);
        }
    }
    return true;
}

}  // namespace

std::vector<MacroValue> TargetComponent::variables() const {
    std::vector<MacroValue> out;
    for (const auto& slot : data) {
        if (slot) out.push_back(*slot);
    }
    return out;
}

std::string TargetComponent::groupName() const {
    return join(key, '-');
}

TargetIndex TargetIndex::build(const std::filesystem::path& root) {
    TargetIndex idx;
    std::error_code ec;
    if (!std::filesystem::exists(root, ec)) return idx;

    for (const auto& entry : std::filesystem::recursive_directory_iterator(root, ec)) {
        if (ec) break;
        if (!entry.is_regular_file() || entry.path().extension() != ".target") continue;

        const auto rel = std::filesystem::relative(entry.path(), root, ec);
        if (ec) continue;

        TargetComponent c;
        bool ok      = true;
        bool isFirst = true;
        for (const auto& part : rel) {
            if (!applyTokens(c, part.string(), isFirst)) {
                ok = false;
                break;
            }
            isFirst = false;
        }
        if (!ok) continue;  // conflicting categories: the reference raises

        c.path         = entry.path();
        c.relativePath = rel.generic_string();

        idx.groups_[c.groupName()].push_back(std::move(c));
        ++idx.componentCount_;
    }
    return idx;
}

std::span<const TargetComponent> TargetIndex::group(const std::vector<std::string>& key) const {
    return group(std::string_view{join(key, '-')});
}

std::span<const TargetComponent> TargetIndex::group(std::string_view dashedKey) const {
    const auto it = groups_.find(std::string{dashedKey});
    if (it == groups_.end()) return {};
    return it->second;
}

std::vector<std::string> TargetIndex::groupNames() const {
    std::vector<std::string> out;
    out.reserve(groups_.size());
    for (const auto& [name, _] : groups_)
        out.push_back(name);
    std::ranges::sort(out);
    return out;
}

float targetWeight(const TargetComponent& component, const MacroFactors& factors, float value,
                   float groupFactor) {
    // weight = value * PRODUCT(factor) -- humanmodifier.py:644-652.
    // A target with no macro dependencies (a plain shape target) reduces to
    // value * groupFactor, which is what a Universal modifier's signed share is.
    float product = groupFactor;
    for (const auto& slot : component.data) {
        if (slot) product *= factors.value(*slot);
    }
    return value * product;
}

}  // namespace mh::core
