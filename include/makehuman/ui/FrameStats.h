// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <QImage>

#include <string>

namespace mh::ui {

/// Describes a rendered frame, and says whether anything was actually drawn.
///
/// **A blank frame saves as a perfectly valid PNG.** That is the whole reason
/// this exists: without it a render that produced nothing exits 0, and the
/// failure looks exactly like success in a log. `--screenshot` has always
/// checked; `--render` did not, because this logic was a lambda in `main.cpp`
/// where only one caller could reach it.
///
/// A FLAT FILL counts as nothing drawn: a frame that is only its clear colour
/// is what a failed render actually looks like, and every arithmetic assertion
/// about coverage passes on one.
///
/// The background is taken to be **pixel (0, 0)**, which is what makes this
/// work without being told the clear colour -- and is also its one assumption:
/// a frame whose top-left corner is part of the subject measures the subject as
/// background and everything else as covered.
///
/// @param out receives the text the application prints -- size, covered pixels,
///        the percentage and the luminance range. Several ctest entries match
///        on this string, so its shape is part of the contract.
/// @return false when the frame is null or a flat fill.
[[nodiscard]] bool describeFrame(const QImage& img, std::string& out);

}  // namespace mh::ui
