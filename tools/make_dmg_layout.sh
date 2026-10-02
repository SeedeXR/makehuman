#!/bin/sh
# Capture the disk image's Finder layout ONCE, into a committed .DS_Store.
#
# WHY IT IS COMMITTED RATHER THAN APPLIED AT BUILD TIME. Window bounds, icon
# size, icon positions and the background picture live in a .DS_Store, and the
# only supported way to write one is to ask Finder. A CI runner has no Finder,
# so a `dmg` target that scripted Finder would silently produce an unstyled
# image there and a styled one here -- two different artefacts from one target,
# and a layout nothing ever checks. Capturing it once and copying the file
# gives the same image everywhere, including on this project's own CI.
#
# Run it when the layout or the background changes:
#
#     tools/make_dmg_layout.sh
#
# It needs a built app, Finder, and nothing else. Regenerate the background
# first with tools/make_dmg_background.py; the two share the numbers below.
set -eu

ROOT=$(cd "$(dirname "$0")/.." && pwd)
APP=${1:-}
OUT="$ROOT/packaging/dmg-DS_Store"
BG="$ROOT/packaging/dmg-background.png"

if [ -z "$APP" ]; then
    APP="$ROOT/build/macos-arm64-release/dmg/MakeHuman.app"
fi
[ -d "$APP" ] || { echo "no app at $APP -- build the dmg target first" >&2; exit 1; }
[ -f "$BG" ] || { echo "no background at $BG -- run make_dmg_background.py" >&2; exit 1; }

# These four numbers are the same ones make_dmg_background.py draws against.
# If one moves, move both.
WIN_W=640
WIN_H=400
LEFT_X=160
RIGHT_X=480
ICON_Y=205

WORK=$(mktemp -d)
# Leaving a mounted image behind is worse than failing, so detach on any exit.
VOL="/Volumes/MakeHuman"
cleanup() {
    [ -d "$VOL" ] && hdiutil detach "$VOL" -quiet 2>/dev/null || true
    rm -rf "$WORK"
}
trap cleanup EXIT

echo "staging a read-write image..."
mkdir -p "$WORK/stage/.background"
cp "$BG" "$WORK/stage/.background/dmg-background.png"
# A copy of the real bundle, because Finder positions icons BY NAME and the
# name has to be the one the shipped image uses.
cp -R "$APP" "$WORK/stage/MakeHuman.app"
ln -s /Applications "$WORK/stage/Applications"

hdiutil create -volname MakeHuman -srcfolder "$WORK/stage" -ov \
    -format UDRW -quiet "$WORK/rw.dmg"
hdiutil attach "$WORK/rw.dmg" -nobrowse -quiet

echo "asking Finder for the layout..."
osascript - "$WIN_W" "$WIN_H" "$LEFT_X" "$RIGHT_X" "$ICON_Y" <<'APPLESCRIPT'
on run argv
    set {w, h, lx, rx, iy} to argv
    tell application "Finder"
        tell disk "MakeHuman"
            open
            set current view of container window to icon view
            set toolbar visible of container window to false
            set statusbar visible of container window to false
            set the bounds of container window to {200, 160, 200 + (w as integer), 160 + (h as integer)}
            set opts to the icon view options of container window
            set arrangement of opts to not arranged
            set icon size of opts to 112
            set text size of opts to 12
            set background picture of opts to file ".background:dmg-background.png"
            set position of item "MakeHuman.app" of container window to {lx as integer, iy as integer}
            set position of item "Applications" of container window to {rx as integer, iy as integer}
            update without registering applications
            close
        end tell
    end tell
end run
APPLESCRIPT

# Finder writes .DS_Store lazily; without this the file is empty or stale.
sync
sleep 2

[ -f "$VOL/.DS_Store" ] || { echo "Finder wrote no .DS_Store" >&2; exit 1; }
mkdir -p "$(dirname "$OUT")"
cp "$VOL/.DS_Store" "$OUT"
# Finder stamps its own .DS_Store with com.apple.FinderInfo, and codesign
# refuses "resource fork, Finder information, or similar detritus". Strip it
# at the source so the committed file is clean.
xattr -c "$OUT"
echo "wrote ${OUT#"$ROOT"/} ($(wc -c <"$OUT" | tr -d ' ') bytes)"
