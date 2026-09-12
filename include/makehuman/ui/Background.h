// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <QImage>

namespace mh::ui {

/// Puts @p background behind @p frame.
///
/// The reference shows an image behind the model so a user can model TO a
/// photograph (`plugins/0_modeling_background.py:104`). The same image is what
/// turns a production render into a composite, and that is what this does:
/// `--render` asks the renderer for alpha, and the backdrop goes underneath the
/// finished frame. No part of the draw path has to know about it.
///
/// **Scaled to COVER, centred.** The backdrop is a file the user chose and
/// nothing says it matches the render's aspect: it is scaled until it fills
/// both dimensions and the overflow is cropped evenly, so the result never
/// distorts the image and never shows a bar of some invented colour. That is
/// the same rule CSS `background-size: cover` states, and it is the one choice
/// here that needs no arbitrary constant.
///
/// **Composite AFTER checking the frame, never before.** `describeFrame` calls
/// a frame blank when it is a flat fill and reads pixel (0, 0) as the clear
/// colour. A render that drew nothing, composited over a photograph first, is
/// neither flat nor clear-coloured in the corner -- so the blank-render guard
/// would pass on precisely the failure it exists to catch, and write it out as
/// a valid PNG. The caller checks, then composites.
///
/// The result is a fresh ARGB32 image, explicitly cleared before painting:
/// QImage's buffer is uninitialised, and the null-backdrop path would otherwise
/// depend on it happening to be zero-filled.
///
/// @return a frame-sized image. A null @p frame stays null; a null @p background
///         -- which is what an unreadable `--background` file gives -- returns
///         the frame untouched, so a bad path is reported rather than silently
///         blanking the render.
[[nodiscard]] QImage overBackground(const QImage& frame, const QImage& background);

}  // namespace mh::ui
