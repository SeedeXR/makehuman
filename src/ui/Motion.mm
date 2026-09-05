// SPDX-License-Identifier: Apache-2.0
//
// The one Objective-C++ translation unit. Reading "reduce motion" needs AppKit:
// QSettings cannot see another application's preference domain (measured --
// com.apple.dock reports 0 keys through QSettings while `defaults read` lists
// dozens), and macOS only writes com.apple.universalaccess/reduceMotion when the
// setting is turned on, so its absence is indistinguishable from an unreadable
// domain.
#include "makehuman/ui/Theme.h"

#include <cstdlib>

#import <AppKit/AppKit.h>

// Read once, at window construction. The setting is live and AppKit publishes
// NSWorkspaceAccessibilityDisplayOptionsDidChangeNotification, but observing it
// would mean a second Objective-C entry point, a callback crossing the language
// boundary and an observer to keep alive -- all to flip one flag on one widget,
// for a setting that is essentially never changed mid-session. Deliberate: do
// not "fix" this without a reason beyond symmetry.

namespace mh::ui::theme {

bool reduceMotion() {
    // An explicit override, checked first, for two reasons.
    //
    // **It is the only way the true branch is reachable.** The AppKit read
    // below answers whatever the machine's System Settings say, and a test --
    // or a build box -- cannot change that without changing a real user
    // preference. `dockOptionsFor(true)` was already tested in isolation, but
    // "a window actually built with reduce-motion on" was not, and that is the
    // half that has to hold.
    //
    // **And it is useful on its own.** Someone on a machine whose system
    // setting is unavailable, or who wants the quieter docking without turning
    // it on system-wide, gets the choice.
    //
    // Unset means "ask the system", which keeps the default behaviour exactly
    // what it was.
    if (const char* env = std::getenv("MH_REDUCE_MOTION"); env != nullptr && env[0] != '\0') {
        return env[0] == '1' || env[0] == 't' || env[0] == 'T' || env[0] == 'y' || env[0] == 'Y';
    }

    // accessibilityDisplayShouldReduceMotion is the documented accessor and is
    // live: it follows the setting without a restart.
    return [[NSWorkspace sharedWorkspace] accessibilityDisplayShouldReduceMotion];
}

}  // namespace mh::ui::theme
