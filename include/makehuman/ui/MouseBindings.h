// SPDX-License-Identifier: Apache-2.0
//
// Which mouse drag orbits the camera and which pans it, stored SYMBOLICALLY.
//
// The reference persists `'%d %d %s\n' % (modifier, buttonMask, methodName)`
// into `mouse.ini` (`legacy/python/core/mhmain.py:1027`) -- raw Qt enum ints,
// with the same two costs as its `shortcuts.ini`: unreadable by a human, and
// meaningful only to the build that wrote them. We store `"Middle"` and
// `"Alt+Left"`, the same choice `ui::shortcuts` makes and for the same reasons.
//
// It also exists to get the rule OUT of an event handler. The dispatch used to
// live inside `ViewportWidget::mouseMoveEvent` -- middle pans, anything else
// orbits -- where it was neither configurable nor testable.
#pragma once

#include <QString>
#include <QStringList>
#include <Qt>

#include <vector>

class QSettings;

namespace mh::ui {

/// What dragging with a bound gesture does. Zoom is absent on purpose: it is on
/// the wheel, which is not a button and has nothing to rebind.
enum class NavVerb { None, Orbit, Pan };

/// The gesture-to-verb table the viewport consults.
class MouseBindings {
public:
    /// The shipped defaults: LEFT orbits, MIDDLE pans.
    ///
    /// The reference zooms with RIGHT (`mhmain.py:198`); we zoom on the wheel,
    /// so right stays unbound rather than being given an invented job.
    MouseBindings();

    /// The settings group every key lives under.
    static constexpr auto kGroup = "mouse";

    /// The verb for @p held with @p modifiers, or `None`.
    ///
    /// Modifiers must match EXACTLY. "At least these" would make a plain-Left
    /// binding fire while the user holds Alt+Left -- stealing a gesture DCC
    /// users expect to mean something else, and leaving no way to bind Alt+Left
    /// separately afterwards.
    ///
    /// When several bound buttons are held the table order decides, and Pan is
    /// checked before Orbit. That is what the hardcoded handler did: it tested
    /// MIDDLE first and fell through to orbit.
    [[nodiscard]] NavVerb verbFor(Qt::MouseButtons held, Qt::KeyboardModifiers modifiers) const;

    /// Points @p verb at @p button with @p modifiers.
    /// @return false when another verb already holds that gesture; the table is
    ///         left untouched, because one of the two would be unreachable and
    ///         nothing would say which.
    [[nodiscard]] bool bind(NavVerb verb, Qt::MouseButton button, Qt::KeyboardModifiers modifiers);

    /// Applies the stored overrides.
    ///
    /// Nothing is applied silently: an unknown verb, an unparseable gesture, a
    /// bare integer left by the reference's format, or a collision all leave
    /// the defaults in place and produce a message.
    [[nodiscard]] QStringList apply(QSettings& settings);

    /// Writes the overrides, and ONLY the overrides -- a verb still on its
    /// shipped gesture has its key removed. That is what lets a changed default
    /// in a later release reach the user, and why this needs no version
    /// sentinel where the reference does.
    void save(QSettings& settings) const;

    /// Restores the shipped gestures and clears the stored overrides.
    void reset(QSettings& settings);
    /// Restores the shipped gestures without touching any settings.
    void reset();

    /// `"Middle"`, `"Alt+Left"`. Empty when @p button is `Qt::NoButton`.
    [[nodiscard]] static QString toText(Qt::MouseButton button, Qt::KeyboardModifiers modifiers);

    /// Parses that text.
    /// @return false when it names no button this class knows.
    [[nodiscard]] static bool fromText(const QString& text, Qt::MouseButton& button,
                                       Qt::KeyboardModifiers& modifiers);

private:
    struct Entry {
        NavVerb verb{};
        Qt::MouseButton button{};
        Qt::KeyboardModifiers modifiers{};
    };

    std::vector<Entry> entries_;
};

}  // namespace mh::ui
