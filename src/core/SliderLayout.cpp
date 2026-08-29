// SPDX-License-Identifier: AGPL-3.0-or-later
#include "makehuman/core/SliderLayout.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <fstream>
#include <ranges>
#include <unordered_map>

namespace mh::core {
namespace {

using json = nlohmann::ordered_json;

std::string capitalise(std::string_view word) {
    if (word.empty()) return {};
    std::string out(word);
    out[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(out[0])));
    // Only the first letter: the reference uses str.capitalize(), which also
    // lowercases the rest.
    for (size_t i = 1; i < out.size(); ++i) {
        out[i] = static_cast<char>(std::tolower(static_cast<unsigned char>(out[i])));
    }
    return out;
}

}  // namespace

std::string SliderLayoutError::message() const {
    const char* what = "slider layout";
    switch (kind) {
        case SliderLayoutErrorKind::NotFound: what = "no such slider layout"; break;
        case SliderLayoutErrorKind::Unreadable: what = "cannot read slider layout"; break;
        case SliderLayoutErrorKind::Malformed: what = "malformed slider layout"; break;
    }
    return std::string(what) + " (" + file + (detail.empty() ? "" : ": " + detail) + ")";
}

std::string guessSliderLabel(std::string_view modifierName, std::string_view groupName) {
    std::vector<std::string> parts;
    for (const auto part : std::views::split(modifierName, '-')) {
        parts.emplace_back(part.begin(), part.end());
    }
    // A trailing `decr|incr` names the two directions, not the feature.
    if (!parts.empty() && parts.back().find('|') != std::string::npos) parts.pop_back();
    // `head-oval` in group `head` is just "Oval".
    if (parts.size() > 1 && parts.front() == groupName) parts.erase(parts.begin());

    std::string label;
    for (size_t i = 0; i < parts.size(); ++i) {
        if (i > 0) label += ' ';
        label += capitalise(parts[i]);
    }
    return label;
}

std::expected<std::vector<foundation::TaskViewSpec>, SliderLayoutError> loadSliderLayout(
    const std::filesystem::path& path, std::span<const Modifier> modifiers) {
    std::error_code ec;
    if (!std::filesystem::exists(path, ec)) {
        return std::unexpected(
            SliderLayoutError{SliderLayoutErrorKind::NotFound, path.string(), {}});
    }
    std::ifstream in(path);
    if (!in) {
        return std::unexpected(
            SliderLayoutError{SliderLayoutErrorKind::Unreadable, path.string(), {}});
    }

    json doc;
    try {
        doc = json::parse(in);
    } catch (const json::exception& e) {
        return std::unexpected(
            SliderLayoutError{SliderLayoutErrorKind::Malformed, path.string(), e.what()});
    }
    if (!doc.is_object()) {
        return std::unexpected(SliderLayoutError{SliderLayoutErrorKind::Malformed, path.string(),
                                                 "top level is not an object"});
    }

    std::unordered_map<std::string_view, const Modifier*> byName;
    byName.reserve(modifiers.size());
    for (const Modifier& m : modifiers)
        byName.emplace(m.fullName, &m);

    std::vector<foundation::TaskViewSpec> views;
    for (const auto& [taskName, props] : doc.items()) {
        if (!props.is_object()) continue;

        foundation::TaskViewSpec view;
        view.name       = taskName;
        view.cameraView = props.value("cameraView", std::string{});
        if (const auto so = props.find("sortOrder"); so != props.end() && so->is_number()) {
            view.sortOrder    = so->get<float>();
            view.hasSortOrder = true;
        }

        const auto mods = props.find("modifiers");
        if (mods == props.end() || !mods->is_object()) {
            return std::unexpected(
                SliderLayoutError{SliderLayoutErrorKind::Malformed, path.string(),
                                  "task view '" + taskName + "' has no modifiers"});
        }

        for (const auto& [sectionName, defs] : mods->items()) {
            if (!defs.is_array()) continue;
            foundation::SliderSection section;
            section.name = sectionName;

            for (const json& d : defs) {
                if (!d.is_object()) continue;
                const std::string full = d.value("mod", std::string{});
                const auto found       = byName.find(full);
                // A slider whose modifier does not exist could not move
                // anything; showing it would be a lie.
                if (found == byName.end()) continue;
                const Modifier& m = *found->second;

                foundation::SliderSpec spec;
                spec.id     = full;
                spec.camera = d.value("cam", std::string{});
                // An explicit "label": "" is kept, matching the reference,
                // which tests the key's presence rather than its emptiness
                // (`guimodifier.py:239`). Not in the shipped data; latent.
                const auto label  = d.find("label");
                spec.label        = (label != d.end() && label->is_string())
                                        ? label->get<std::string>()
                                        : guessSliderLabel(m.name, m.group);
                spec.minValue     = m.minValue();
                spec.maxValue     = m.maxValue();
                spec.defaultValue = m.defaultValue;
                section.sliders.push_back(std::move(spec));
            }
            view.sections.push_back(std::move(section));
        }
        views.push_back(std::move(view));
    }
    return views;
}

std::expected<StandardLayout, SliderLayoutError> loadStandardLayout(
    const std::filesystem::path& dataDir) {
    StandardLayout out;

    for (const char* f :
         {"modeling_modifiers.json", "bodyshapes_modifiers.json", "measurement_modifiers.json"}) {
        auto m = loadModifiers(dataDir / f);
        if (!m) {
            return std::unexpected(SliderLayoutError{SliderLayoutErrorKind::Malformed,
                                                     (dataDir / f).string(), m.error().message()});
        }
        out.modifiers.insert(out.modifiers.end(), m->begin(), m->end());
    }

    for (const char* f :
         {"modeling_sliders.json", "bodyshapes_sliders.json", "measurement_sliders.json"}) {
        auto v = loadSliderLayout(dataDir / f, out.modifiers);
        if (!v) return std::unexpected(v.error());
        out.views.insert(out.views.end(), v->begin(), v->end());
    }

    // gui3d.py:310-317: a view with no sortOrder takes the lowest non-negative
    // integer not already in use, assigned in load order.
    std::vector<float> taken;
    for (foundation::TaskViewSpec& v : out.views) {
        if (!v.hasSortOrder) {
            float candidate = 0.0F;
            while (std::find(taken.begin(), taken.end(), candidate) != taken.end()) {
                candidate += 1.0F;
            }
            v.sortOrder    = candidate;
            v.hasSortOrder = true;
        }
        taken.push_back(v.sortOrder);
    }
    // Stable, so views that tie keep load order -- Macro modelling and Body
    // shapes are both 0, and the reference shows them in that order.
    std::stable_sort(out.views.begin(), out.views.end(),
                     [](const foundation::TaskViewSpec& a, const foundation::TaskViewSpec& b) {
                         return a.sortOrder < b.sortOrder;
                     });
    return out;
}

}  // namespace mh::core
