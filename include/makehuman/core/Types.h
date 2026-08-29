// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

// The vector and face-group types moved to mh_foundation (Apache-2.0) so the
// io layer can use them without linking AGPL code. They are re-exported into
// mh::core here so existing call sites are unaffected.
#include "makehuman/foundation/Types.h"

namespace mh::core {

using foundation::FaceGroup;
using foundation::kDecimetresToCentimetres;
using foundation::Vec2;
using foundation::Vec3;
using foundation::Vec4;

using foundation::cross;
using foundation::dot;
using foundation::operator+;
using foundation::operator-;
using foundation::operator*;

}  // namespace mh::core
