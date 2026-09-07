// SPDX-License-Identifier: Apache-2.0
//
// Keyboard shortcut overrides, stored SYMBOLICALLY.
//
// The reference persists raw Qt enum ints --
// `f.write('%d %d %s\n' % (shortcut[0], shortcut[1], action))`
// (`legacy/python/core/mhmain.py:1022`) — and pays for it twice. The file is
// opaque to a human (`67108864 90 undo`), and the numbers only mean anything to
// the Qt build that wrote them, so it carries a magic sentinel
// (`'_versionSentinel': (0, 0x87654321)`, `:192`) and discards the WHOLE file
// when it fails to match (`:988-989`).
//
// We store `"Ctrl+Z"` via `QKeySequence::PortableText`. Readable, hand-editable,
// stable across Qt versions — so there is no sentinel and nothing is silently
// thrown away. A deliberate divergence from the reference under CLAUDE.md hard
// rule 3, not an oversight.
//
// Actions are addressed by `objectName` (`edit.undo`, `file.export`), which the
// window already sets for the icon audit and for tests. Only actions that HAVE
// an object name participate: an unnamed action cannot be referred to, so it
// cannot be rebound.
#pragma once

#include <QString>
#include <QStringList>

class QMainWindow;
class QSettings;

namespace mh::ui::shortcuts {

/// The settings group every key lives under.
inline constexpr auto kGroup = "shortcuts";

/// Applies the stored overrides to @p window's named actions.
///
/// Also records each action's shipped sequence in a dynamic property, so
/// `save` can tell an override from a default and `reset` can put it back.
/// Call this once, before anything changes a shortcut.
///
/// Nothing is applied silently: an unknown action name, an unparseable
/// sequence, a bare integer left by the reference's format, or a collision
/// with another action all leave the default in place and produce a message.
///
/// @return one human-readable problem per rejected entry, empty when all
///         applied. The caller decides whether that is a log line or a dialog;
///         this module is Apache-2.0 and owns neither.
[[nodiscard]] QStringList apply(QMainWindow& window, QSettings& settings);

/// Writes the overrides — and ONLY the overrides.
///
/// An action still on its shipped default is removed from the file rather than
/// written. That is what makes a changed default in a later release reach the
/// user: the reference writes every binding, so its own defaults are frozen on
/// disk the first time anything is saved, which is the reason it needs a
/// version sentinel at all.
///
/// Requires a prior `apply` on the same window to know the defaults; without
/// one every sequence looks like a default and nothing is written.
void save(const QMainWindow& window, QSettings& settings);

/// Puts every shipped default back and clears the stored overrides.
void reset(QMainWindow& window, QSettings& settings);

}  // namespace mh::ui::shortcuts
