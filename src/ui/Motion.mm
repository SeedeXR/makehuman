// SPDX-License-Identifier: Apache-2.0
//
// The one Objective-C++ translation unit. Reading "reduce motion" needs AppKit:
// QSettings cannot see another application's preference domain (measured --
// com.apple.dock reports 0 keys through QSettings while `defaults read` lists
// dozens), and macOS only writes com.apple.universalaccess/reduceMotion when the
// setting is turned on, so its absence is indistinguishable from an unreadable
// domain.
#include "makehuman/ui/Theme.h"

#import <AppKit/AppKit.h>

// Read once, at window construction. The setting is live and AppKit publishes
// NSWorkspaceAccessibilityDisplayOptionsDidChangeNotification, but observing it
// would mean a second Objective-C entry point, a callback crossing the language
// boundary and an observer to keep alive -- all to flip one flag on one widget,
// for a setting that is essentially never changed mid-session. Deliberate: do
// not "fix" this without a reason beyond symmetry.

namespace mh::ui::theme {

bool reduceMotion() {
    // accessibilityDisplayShouldReduceMotion is the documented accessor and is
    // live: it follows the setting without a restart.
    return [[NSWorkspace sharedWorkspace] accessibilityDisplayShouldReduceMotion];
}

}  // namespace mh::ui::theme
