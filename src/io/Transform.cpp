// SPDX-License-Identifier: Apache-2.0
#include "makehuman/io/Transform.h"

#include <array>

namespace mh::io {

float unitScale(Unit u) noexcept {
    // apps/gui/guiexport.py:124-129
    switch (u) {
        case Unit::Decimeter: return 1.0F;
        case Unit::Meter: return 0.1F;
        case Unit::Centimeter: return 10.0F;
        case Unit::Inch: return 1.0F / 0.254F;
    }
    return 1.0F;
}

std::string_view unitName(Unit u) noexcept {
    switch (u) {
        case Unit::Decimeter: return "decimeter";
        case Unit::Meter: return "meter";
        case Unit::Centimeter: return "centimeter";
        case Unit::Inch: return "inch";
    }
    return "decimeter";
}

Transform meshTransform(float scale, bool feetOnGround, const foundation::RenderView& mesh) {
    // One entry, so the scene rule applies unchanged.
    struct One {
        const foundation::RenderView& mesh;
    };

    const std::array<One, 1> one{One{mesh}};
    return sceneTransform(scale, feetOnGround, one);
}

}  // namespace mh::io
