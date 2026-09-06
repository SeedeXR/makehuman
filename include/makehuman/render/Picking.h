// SPDX-License-Identifier: Apache-2.0
//
// Turning a click into a point on the model.

#pragma once

#include "makehuman/foundation/Geometry.h"
#include "makehuman/foundation/Types.h"
#include "makehuman/render/SceneResources.h"

#include <optional>

namespace mh::render {

/// A ray in the MESH's own coordinates, not eye space.
///
/// Model space is where the geometry is, and in this port the model is what
/// rotates (`Camera`), so the ray has to be carried back through that rotation
/// rather than the mesh carried forward through it.
struct Ray {
    foundation::Vec3 origin;
    /// Unit length.
    foundation::Vec3 direction;
};

/// The ray through pixel (@p px, @p py) of a @p width x @p height viewport.
///
/// Screen coordinates are Qt's: origin top-left, y growing DOWN. The returned
/// direction has y growing up, like the world.
///
/// Built in eye space and transformed back, deliberately. Going through the
/// full inverse MVP would drag in `QRhi::clipSpaceCorrMatrix()` and the
/// backend's NDC depth range -- Metal's is [0,1], OpenGL's is [-1,1] -- for no
/// gain: the eye-space ray of a perspective camera is just the field of view,
/// and it is the same on every backend.
[[nodiscard]] Ray rayThroughPixel(const Camera& camera, int px, int py, int width, int height);

/// The nearest point where @p ray meets @p mesh, in mesh coordinates.
///
/// Moller-Trumbore over the triangle list, nearest hit wins, hits behind the
/// origin rejected. Both faces count: a click on the inside of an open mesh --
/// the mouth bag, a proxy's hem -- is still a click on the model.
///
/// Linear in triangles. The base mesh is 36,972 of them and this runs once per
/// click, so there is nothing here for an acceleration structure to save; build
/// one when something casts rays per FRAME.
[[nodiscard]] std::optional<foundation::Vec3> intersect(const foundation::RenderView& mesh,
                                                        const Ray& ray);

/// Pans @p camera so @p pointInMesh sits at the centre of the viewport.
///
/// Distance and field of view are untouched: this recentres, it does not zoom.
/// Matches what the reference does with a pick (`camera.py:774
/// mousePickHumanCenter`); what it does NOT match is how the point is found,
/// since `selectedGroup` there is written and never read (`mhmain.py:293,433`
/// are its only occurrences).
void focusOn(Camera& camera, const foundation::Vec3& pointInMesh);

}  // namespace mh::render
