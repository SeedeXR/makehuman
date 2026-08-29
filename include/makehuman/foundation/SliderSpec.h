// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <string>
#include <vector>

namespace mh::foundation {

/// One slider, described in plain data.
///
/// The UI needs a label, a range and something to identify the value it is
/// changing -- it does not need the modifier system, which is AGPL. This is the
/// same licence bridge `RenderView` is for geometry: `mh_core` resolves the
/// modifiers and hands `mh_ui` these.
struct SliderSpec {
    /// The modifier's full name, e.g. `head/head-age-decr|incr`. Opaque to the
    /// UI: it comes back unchanged when the value changes.
    std::string id;
    std::string label;
    /// A camera the reference moves to when this slider is touched
    /// (`frontView`, `leftView`, …). Empty when the file gives none.
    std::string camera;

    float minValue{0.0F};
    float maxValue{1.0F};
    float defaultValue{0.0F};
};

/// A named group of sliders inside a task view, e.g. "head shape".
struct SliderSection {
    std::string name;
    std::vector<SliderSpec> sliders;
};

/// One tab: "Face", "Torso", "Gender", …
struct TaskViewSpec {
    std::string name;
    /// The camera this whole view starts from; empty when unspecified.
    std::string cameraView;
    /// Tabs are ordered by this. Absent in some files, which is why the flag
    /// exists rather than a sentinel value that could collide with a real one.
    float sortOrder{0.0F};
    bool hasSortOrder{false};

    std::vector<SliderSection> sections;

    [[nodiscard]] size_t sliderCount() const {
        size_t n = 0;
        for (const auto& s : sections)
            n += s.sliders.size();
        return n;
    }
};

/// One selectable asset -- a skin, a pose, later a garment.
///
/// Plain data for the same reason `SliderSpec` is: the panel that lists these
/// must not link the AGPL asset layer. `id` is opaque to the UI and comes back
/// unchanged on selection, so the app can put a path, a UUID or a keyword in it.
struct AssetChoice {
    std::string id;
    std::string label;
};

/// A labelled set of mutually exclusive choices, e.g. "Skin" or "Pose".
struct AssetGroup {
    std::string name;
    std::vector<AssetChoice> choices;
    /// Index into `choices` that starts selected; -1 for none.
    int selected{-1};
};

}  // namespace mh::foundation
