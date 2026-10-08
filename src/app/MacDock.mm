// SPDX-License-Identifier: AGPL-3.0-or-later
//
// WHY THIS FILE EXISTS.
//
// `--mcp` is a JSON-RPC server speaking over stdin and stdout. It opens no
// window and nobody launches it by hand -- a client starts one per session.
// It was nonetheless appearing in the Dock and in Cmd-Tab as a running copy
// of MakeHuman, so four open client sessions put four MakeHuman icons on
// screen that the user never started. MEASURED with `lsappinfo`, which
// reported `type="Foreground"` for every one of them.
//
// The cause is in main(): QApplication is constructed before a single
// argument has been parsed, and on macOS constructing one transforms the
// process into a foreground application. That transform is what the Dock
// shows. By the time `--mcp` is read, the icon is already there.
//
// WHY NOT LSUIElement. The obvious fix is the Info.plist key, and it is the
// wrong one: it applies to the BUNDLE, and this bundle is also the real GUI
// application, which must keep its Dock icon. The policy is therefore changed
// at RUNTIME and only on the --mcp path.
//
// WHY ACCESSORY AND NOT PROHIBITED. Prohibited severs the window-server
// connection, and the `render` tool needs a real Metal device -- the same
// constraint that rules out QT_QPA_PLATFORM=offscreen here. Accessory keeps
// the connection and drops only the Dock icon and the menu bar, which is
// exactly the difference being asked for.

#import <AppKit/AppKit.h>

namespace mh::app {

bool hideFromDock() {
    // NSApp is nil until QApplication has built the shared NSApplication, and
    // messaging nil is a no-op that would make this silently claim success.
    // The read-back below is what rules that out: on nil it yields
    // NSApplicationActivationPolicyRegular (0) and this returns false.
    [NSApp setActivationPolicy:NSApplicationActivationPolicyAccessory];

    // READ BACK rather than trust the setter. setActivationPolicy returns NO
    // when AppKit refuses the transform, and a caller that reports its own
    // intent rather than the system's answer is not evidence of anything.
    return [NSApp activationPolicy] == NSApplicationActivationPolicyAccessory;
}

}  // namespace mh::app
