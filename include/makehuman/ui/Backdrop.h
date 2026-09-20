// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <QRectF>
#include <QSize>

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace mh::ui {

/// Which axis view a backdrop image is bound to.
///
/// The reference binds a background to a side and shows it only from that side
/// (`plugins/0_modeling_background.py`), because the point is modelling to an
/// ORTHOGRAPHIC reference photo: a front photo hanging behind a three-quarter
/// view is worse than no photo at all. This port already has the same six views
/// on the View menu (`MainWindow.cpp:510-537`), so the binding maps directly.
enum class BackdropSide : uint8_t { Front, Back, Left, Right, Top, Bottom };

/// Where a backdrop sits, relative to the plain cover fit.
///
/// The identity -- and the default -- is the cover fit unchanged, so a backdrop
/// that nobody has dragged renders exactly as it did before this type existed.
///
/// @param x,y  pan, as a fraction of the VISIBLE part of the image. A fraction
///             rather than pixels because a drag is measured on screen: +0.5
///             moves the image half a window whatever the file's resolution,
///             and the same saved value reproduces the same framing on a
///             different sized viewport.
/// @param scale zoom, where 2 shows HALF as much of the image (the picture
///             looks twice as big). **Zero or less is refused** rather than
///             clamped to something arbitrary: it makes the source rectangle
///             empty and `SceneResources::draw` SKIPS an empty rect, so the
///             backdrop would vanish with no error at all.
struct BackdropTransform {
    float x{0.0F};
    float y{0.0F};
    float scale{1.0F};
};

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
/// @param transform pan and zoom applied to that rectangle. The result is
///        CLAMPED back inside the image, because the two consumers disagree
///        about what lies outside it: the viewport's sampler is `ClampToEdge`
///        and SMEARS the edge pixel, while `QImage::copy` of an out-of-bounds
///        rectangle fills TRANSPARENT BLACK. Clamping here -- in the one place
///        that owns the rule -- is what keeps `--render` and the viewport
///        showing the same thing.
[[nodiscard]] QRectF coverSource(QSize frame, QSize image, BackdropTransform transform = {});

/// The smallest zoom a saved file may ask for.
///
/// Not a taste choice: zero makes the source rectangle empty, and
/// `SceneResources::draw` SKIPS an empty rect, so a `scale 0` in a
/// hand-edited or reference-written `.mhm` would make the backdrop silently
/// fail to appear. Clamping on parse turns that into a visible -- if very
/// zoomed-out -- image the user can then fix.
inline constexpr float kMinBackdropScale = 0.01F;

/// One `background <side> <file> <aspect> <transX> <transY> <scale>` line.
struct BackdropPlacement {
    BackdropSide side{BackdropSide::Front};
    std::string file;
    /// The reference's own aspect field. Carried so a round trip does not
    /// silently rewrite someone else's number; this port derives what it draws
    /// from the image and the frame instead.
    float aspect{1.0F};
    BackdropTransform transform;
};

/// Reads one `background ...` line from a `.mhm`.
///
/// **Parsed from the TAIL.** The reference writes the filename unquoted and
/// filenames contain spaces (`0_modeling_background.py:439-445`), so the side
/// is token 1, the LAST FOUR tokens are `aspect transX transY scale`, and
/// everything between them is the name.
///
/// @return nothing for a line this port does not own -- `background enabled
///         <bool>`, an unrecognised side such as the reference's three-quarter
///         `other`, too few tokens, or a non-numeric tail. **Nothing means
///         "leave it alone", and the writer must preserve such a line
///         verbatim** rather than dropping it.
[[nodiscard]] std::optional<BackdropPlacement> parseBackgroundLine(const std::string& line);

/// The inverse of `parseBackgroundLine`, for the lines this port owns.
[[nodiscard]] std::string formatBackgroundLine(const BackdropPlacement& placement);

/// Rewrites the `background` lines in @p unhandled to match @p placements.
///
/// **Deliberately not `recordLine`.** That helper erases every line whose first
/// token matches the key and appends exactly one, which is right for a
/// single-valued key and wrong here: applied to `background` it would collapse
/// all six sides into one line. Ownership is by SIDE, not by key.
///
/// A `background` line this port does not own -- the reference's three-quarter
/// `other`, or its `background enabled <bool>` flag -- is **left exactly where
/// it is**, because dropping it would lose the user's data on a round trip.
/// `parseBackgroundLine` returning nothing is precisely that signal.
///
/// Replaces rather than appends, so save-load-save does not grow the file.
void recordBackgrounds(std::vector<std::string>& unhandled,
                       const std::vector<BackdropPlacement>& placements);

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
