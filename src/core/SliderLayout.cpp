// SPDX-License-Identifier: AGPL-3.0-or-later
#include "makehuman/core/SliderLayout.h"
#include "makehuman/foundation/FileRead.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <cctype>
#include <fstream>
#include <ranges>
#include <unordered_map>
#include <unordered_set>

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

/// Words a user is likely to search for, per modifier.
///
/// EVIDENCE, not taste: the project's owner did not know several of these
/// sliders existed, and the searches recorded as having failed are "chubby",
/// "fat", "abs", "six pack" and "toned" -- against shipped labels reading
/// Weight, Stomach tone and Muscle. Every entry below exists because the word
/// on the left finds nothing today.
///
/// Matched on the modifier's full name by SUBSTRING, so one entry covers a
/// whole family (`breast/` catches six sliders). Deliberately short: a synonym
/// list that tries to be a thesaurus makes the search match everything, which
/// is the same as matching nothing.
///
/// A table in code rather than data, beside `guessSliderLabel`, which is the
/// other place this file decides what a user reads. It becomes a data file the
/// day someone wants to translate it -- a search that only works in English is
/// a known limit, recorded here rather than discovered later.
std::string searchKeywords(std::string_view modifier) {
    struct Entry {
        std::string_view match;
        std::string_view words;
    };

    static constexpr std::array<Entry, 12> kTable{{
        {"macrodetails-universal/Weight", "fat chubby heavy thin skinny slim overweight"},
        {"macrodetails-universal/Muscle", "toned muscular buff ripped strong athletic"},
        {"stomach/stomach-tone", "abs sixpack six-pack belly tummy gut core"},
        {"stomach/stomach-pregnant", "pregnant belly bump"},
        {"macrodetails/Age", "old young elderly child baby"},
        {"macrodetails/Gender", "male female man woman masculine feminine"},
        {"macrodetails/African", "ethnicity black"},
        {"macrodetails/Asian", "ethnicity"},
        {"macrodetails/Caucasian", "ethnicity white"},
        {"breast/", "bust chest bra cleavage"},
        {"pelvis/", "hips waist"},
        {"buttocks/", "bum butt glutes rear"},
    }};
    for (const Entry& e : kTable) {
        if (modifier.find(e.match) != std::string_view::npos) return std::string(e.words);
    }
    return {};
}

std::expected<std::vector<foundation::TaskViewSpec>, SliderLayoutError> loadSliderLayout(
    const std::filesystem::path& path, std::span<const Modifier> modifiers) {
    // openForRead, not exists()+ifstream: a DIRECTORY satisfies both and
    // then parses as an empty file, so this reader used to accept one.
    // See foundation/FileRead.h for what each reader did before.
    auto opened = foundation::openForRead(path);
    if (!opened) {
        // NotAFile maps to Unreadable rather than NotFound: something IS
        // there, and saying "not found" about a path that exists sends
        // whoever is debugging it looking in the wrong place.
        const auto kind = opened.error() == foundation::FileReadErrorKind::NotFound
                              ? SliderLayoutErrorKind::NotFound
                              : SliderLayoutErrorKind::Unreadable;
        return std::unexpected(SliderLayoutError{kind, path.string(), {}});
    }
    std::ifstream& in = *opened;

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
                spec.id       = full;
                spec.keywords = searchKeywords(full);
                spec.camera   = d.value("cam", std::string{});
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

    // `faceunits_*` is OURS and the other three are the reference's, which is
    // why it is a fourth file rather than rows appended to
    // `modeling_modifiers.json`: the inherited data stays byte-for-byte
    // inherited, so provenance is readable from the file list alone. It is
    // also generated -- `tools/make_faceunits.py` derives it from the targets
    // on disk, and `--check` fails if the two drift apart.
    for (const char* f : {"modeling_modifiers.json", "bodyshapes_modifiers.json",
                          "measurement_modifiers.json", "faceunits_modifiers.json"}) {
        auto m = loadModifiers(dataDir / f);
        if (!m) {
            return std::unexpected(SliderLayoutError{SliderLayoutErrorKind::Malformed,
                                                     (dataDir / f).string(), m.error().message()});
        }
        out.modifiers.insert(out.modifiers.end(), m->begin(), m->end());
    }

    for (const char* f : {"modeling_sliders.json", "bodyshapes_sliders.json",
                          "measurement_sliders.json", "faceunits_sliders.json"}) {
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

std::expected<std::vector<foundation::SliderPreset>, SliderLayoutError> loadCombinationPresets(
    const std::filesystem::path& dataDir, std::span<const Modifier> known) {
    const std::filesystem::path file = dataDir / "combination_presets.json";
    // Absent is not an error: a data directory without the file offers no
    // presets, the same way a rig without a retarget table simply renames
    // nothing.
    std::error_code ec;
    if (!std::filesystem::exists(file, ec)) return std::vector<foundation::SliderPreset>{};

    // openForRead rather than exists()+ifstream, for the reason
    // `loadSliderLayout` records above: a DIRECTORY satisfies both and then
    // parses as an empty file.
    auto opened = foundation::openForRead(file);
    if (!opened) {
        return std::unexpected(
            SliderLayoutError{SliderLayoutErrorKind::Unreadable, file.string(), {}});
    }
    const json parsed = json::parse(*opened, nullptr, false);
    if (parsed.is_discarded() || !parsed.is_array()) {
        return std::unexpected(SliderLayoutError{SliderLayoutErrorKind::Malformed, file.string(),
                                                 "expected an array"});
    }

    std::unordered_set<std::string_view> names;
    names.reserve(known.size());
    for (const Modifier& m : known)
        names.insert(m.fullName);

    std::vector<foundation::SliderPreset> out;
    for (const json& entry : parsed) {
        if (!entry.is_object()) continue;
        foundation::SliderPreset preset;
        preset.name        = entry.value("name", std::string{});
        const auto setting = entry.find("set");
        if (preset.name.empty() || setting == entry.end() || !setting->is_object()) {
            return std::unexpected(SliderLayoutError{SliderLayoutErrorKind::Malformed,
                                                     file.string(),
                                                     "a preset needs a name and a set"});
        }
        for (const auto& [id, value] : setting->items()) {
            if (!value.is_number()) {
                return std::unexpected(SliderLayoutError{SliderLayoutErrorKind::Malformed,
                                                         file.string(), id + " is not a number"});
            }
            // REFUSED, not skipped. A preset that quietly drops a modifier
            // still applies and still looks like it worked, which is exactly
            // how a recipe rots after someone renames a slider.
            if (!names.contains(id)) {
                return std::unexpected(SliderLayoutError{SliderLayoutErrorKind::Malformed,
                                                         file.string(), "no such modifier: " + id});
            }
            preset.values.emplace_back(id, value.get<float>());
        }
        out.push_back(std::move(preset));
    }
    return out;
}

}  // namespace mh::core
