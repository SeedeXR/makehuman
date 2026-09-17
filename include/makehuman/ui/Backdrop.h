// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <QRectF>
#include <QSize>

#include <cstdint>
#include <optional>

namespace mh::ui {

/// Which axis view a backdrop image is bound to.
///
/// The reference binds a background to a side and shows it only from that side
/// (`plugins/0_modeling_background.py`), because the point is modelling to an
/// ORTHOGRAPHIC reference photo: a front photo hanging behind a three-quarter
/// view is worse than no photo at all. This port already has the same six views
/// on the View menu (`MainWindow.cpp:510-537`), so the binding maps directly.
enum class BackdropSide : uint8_t { Front, Back, Left, Right, Top, Bottom };

/// The part of @p image that a cover-fit draw into @p frame shows.
///
/// Cover means: scale until BOTH dimensions reach the frame, centre, and crop
/// the overflow evenly -- CSS `background-size: cover`, and the one placement
/// rule here that needs no arbitrary constant. It never distorts the image and
/// never shows a bar of some invented colour.
///
/// Returned as a rectangle in IMAGE pixels because there are two consumers and
/// one rule: `overBackground` crops the scaled image for `--render`, and the
/// viewport's backdrop turns the same rectangle into UV scale and offset. Two
/// implementations of one rule is how they drift apart.
///
/// @return an EMPTY rectangle for a degenerate size, rather than dividing by
///         zero -- a zero-width frame happens while a dock is being resized.
[[nodiscard]] QRectF coverSource(QSize frame, QSize image);

/// Is the camera looking along @p side closely enough to show its backdrop?
///
/// @param yawDegrees   accumulates as the user drags and is never normalised,
///                     so 360 and -360 are the front view just as much as 0.
/// @param pitchDegrees clamped by the viewport to +-89 (`kMaxPitchDegrees`), so
///                     the pitch that selects Top is not exactly 90.
[[nodiscard]] bool facingSide(BackdropSide side, float yawDegrees, float pitchDegrees);

/// Which side the camera is facing, if any.
///
/// The inverse of `facingSide`, and the reason it exists is the UI: the
/// reference picks a side with radio buttons, and this port can simply use the
/// view you are already looking from -- orbit to the left, load the left photo.
/// Nothing is returned for a three-quarter view, where the answer would be a
/// guess.
[[nodiscard]] std::optional<BackdropSide> sideFacing(float yawDegrees, float pitchDegrees);

/// How far off-axis a view may be and still count as facing a side.
///
/// 45 degrees is not a taste choice: it is the half-way point between two
/// adjacent axis views, so every camera angle belongs to exactly one side and
/// no angle belongs to two. Anything larger would show two backdrops at once.
inline constexpr float kFacingToleranceDegrees = 45.0F;

}  // namespace mh::ui
