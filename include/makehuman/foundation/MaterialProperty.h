// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <string>

namespace mh::foundation {

/// One editable property of a material, in plain data.
///
/// The licence bridge, the same one `SliderSpec` is for modifiers and
/// `RenderView` is for geometry: the material editor lives in `mh_ui`, which is
/// Apache-2.0 and may never include a core header, while `mh::core::Material`
/// is AGPL. `mh_core` reads the `.mhmat` and hands the panel these.
///
/// The panel turns a row back into `id + "=" + value` and emits that string,
/// which is **exactly** what `mh::core::setMaterialProperty` takes -- so the
/// panel and `--set-material` cannot drift into meaning different things by the
/// same edit.
struct MaterialProperty {
    /// The `.mhmat` key, in the spelling a FILE must use.
    ///
    /// Not the lookup key. Our parser lowercases before comparing, but the
    /// reference's compares `words[0]` case-SENSITIVELY
    /// (`material.py:369-448`), so a file saying `normalmaptexture` loads here
    /// and is silently ignored by MakeHuman 1.x -- the texture just vanishes.
    std::string id;
    std::string label;

    /// What kind of control the row wants. The panel picks a widget from this;
    /// the VALUE is always text, because text is what the format holds and what
    /// the setter parses.
    enum class Kind : uint8_t { Colour, Scalar, Flag, Text, Texture };
    Kind kind{Kind::Text};

    /// Spelled the way `saveMaterial` writes it -- `foundation::formatShortest`
    /// for numbers (so `1`, not `1.0`) and `True`/`False` for flags. That is
    /// what makes this the exact inverse of `setMaterialProperty` rather than
    /// approximately its inverse.
    std::string value;
};

}  // namespace mh::foundation
