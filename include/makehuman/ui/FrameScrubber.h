// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <QWidget>

class QLabel;
class QSlider;

namespace mh::ui {

/// The frame scrubber over an animation loaded ELSEWHERE.
///
/// This is the port of `class AnimationLibrary(gui3d.TaskView)`
/// (`legacy/python/plugins/3_libraries_animation.py:48`, registered into
/// `Pose/Animate` at `:182`). In its 189 lines there is no file list: a frame
/// slider, four transport buttons, a frame label and a status line, over
/// whatever animation some other view put on the character. The file is picked
/// in the Assets dock's Animation chooser, which is the same split the
/// reference has.
///
/// **It emits on RELEASE, not per tick.** The reference calls `updateFrame`
/// from `onChanging` as well as `onChange` (`:126-129`), which re-skins the
/// whole body on every pixel of a drag. That is affordable there and is not
/// here: this port has no cached `BvhFile` and no `setFrame`, so each frame
/// change re-reads the `.bvh` and refits the skeleton. A drag therefore moves
/// the slider and the label, and asks for one reload when the thumb is let go.
///
/// **No "select a skeleton first" state**, which the reference has at `:166`.
/// There `human.getSkeleton()` can be None; here every `--rig` names a
/// `.mhskel` that `loadPoseRig` loads unconditionally and the Skeleton chooser
/// offers no empty entry, so the branch could never be taken. A status line
/// nobody can reach is a line that goes stale unnoticed.
class FrameScrubber : public QWidget {
    Q_OBJECT

public:
    explicit FrameScrubber(QWidget* parent = nullptr);

    /// The ONE door: range, clamp, enabled state and status line, together.
    ///
    /// @param frameCount frames in the animation now on the character; 0 when
    ///        it is a static pose or none.
    ///
    /// Does NOT emit. It reports a load that has already happened, and a
    /// scrubber that asked for a reload in response to one would loop.
    ///
    /// A shorter animation resets to frame 0 rather than clamping to its last
    /// frame -- the reference's `onShow` does the same (`:154`) and the reason
    /// is that clamping leaves the slider claiming a frame the user never
    /// chose.
    void setAnimation(int frameCount);

    /// The frame the slider is showing.
    [[nodiscard]] int frame() const;

    /// Moves the slider WITHOUT emitting, for putting it back after a load
    /// that failed.
    ///
    /// Out of range is ignored rather than clamped, for the reason
    /// `AssetPanel::setChoice` ignores an unknown id: a control showing
    /// something nobody asked for is worse than one showing the old value.
    void setFrame(int frame);

    /// What the status line reads. For tests, and for the same reason
    /// `AssetPanel::choice` exists -- asserting on the widget's own words
    /// rather than on a copy of them.
    [[nodiscard]] QString status() const;

    /// Whether the controls are live, i.e. there is more than one frame.
    [[nodiscard]] bool isScrubbable() const;

signals:
    /// The user asked for @p frame. Never emitted by `setAnimation` or
    /// `setFrame`.
    void frameChosen(int frame);

private:
    /// Suppresses `frameChosen` while this object's own code moves the slider.
    ///
    /// A `QSignalBlocker` on the slider would do it, and would also stop the
    /// frame label updating -- which is exactly the drift `AssetPanel` had to
    /// close by hand after blocking its picker. One flag, and every path still
    /// runs through the one `valueChanged` handler that writes the label.
    bool quiet_{false};

    /// The frame the thumb was grabbed at, so releasing without moving asks
    /// for nothing. Written on `sliderPressed` and read on `sliderReleased`;
    /// it is not a second copy of the current frame, which is the slider's.
    int pressedFrame_{0};

    QSlider* slider_{};
    QLabel* frameLabel_{};
    QLabel* status_{};
};

}  // namespace mh::ui
