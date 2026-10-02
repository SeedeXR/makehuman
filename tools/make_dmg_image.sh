#!/bin/sh
# Build the distributable disk image, with its Finder layout.
#
# WHY THIS IS NOT ONE `hdiutil create`. The obvious version -- drop a
# `.DS_Store` into the staging folder and image the lot -- builds a correct
# image on a developer Mac and fails on a GitHub runner with
#
#     hdiutil: create failed - Resource busy
#
# MEASURED, not guessed: CI failed on 1ae1f6b6 with the two copies in place and
# went green on 7d93a66a with nothing else changed. `hdiutil create -srcfolder`
# creates and ATTACHES a temporary image to copy the tree into, and a `.DS_Store`
# sitting in the scanned folder is enough to make that attach return EBUSY on a
# runner where Spotlight and diskarbitration are in a different state than here.
#
# So the layout goes in AFTER the filesystem exists, which is what every DMG
# tool does and what the failure was telling us to do:
#
#   1. create a READ-WRITE image from the staging folder, which holds only the
#      app and the Applications symlink;
#   2. attach it and copy the background and the .DS_Store in;
#   3. detach, and convert to the compressed read-only image that ships.
#
# The size is computed with slack rather than left to `-srcfolder`, because
# `-srcfolder` fits the image to its input and step 2 then has nowhere to write.
set -eu

STAGE=${1:?staging directory}
OUT=${2:?output .dmg}
DS_STORE=${3:?committed .DS_Store}
BACKGROUND=${4:?committed background png}
VOLNAME=${5:-MakeHuman}

[ -d "$STAGE" ] || { echo "no staging directory at $STAGE" >&2; exit 1; }
[ -f "$DS_STORE" ] || { echo "no .DS_Store at $DS_STORE" >&2; exit 1; }
[ -f "$BACKGROUND" ] || { echo "no background at $BACKGROUND" >&2; exit 1; }

WORK=$(mktemp -d)
MNT="$WORK/mnt"
# A mounted image left behind is worse than a failed build: the next run finds
# the volume name taken and fails somewhere less obvious.
cleanup() {
    [ -d "$MNT" ] && hdiutil detach "$MNT" -force -quiet 2>/dev/null || true
    rm -rf "$WORK"
}
trap cleanup EXIT

# Slack for the layout files plus HFS+ overhead. The background is a few KB and
# the .DS_Store ~10 KB; 32 MB is far more than needed and costs nothing, because
# the read-only image produced in step 3 is sized to its contents.
KB=$(du -sk "$STAGE" | cut -f1)
SIZE_KB=$((KB + 32768))

echo "staging $((KB / 1024)) MB -> read-write image of $((SIZE_KB / 1024)) MB"
hdiutil create -volname "$VOLNAME" -srcfolder "$STAGE" -ov -format UDRW \
    -size "${SIZE_KB}k" -quiet "$WORK/rw.dmg"

mkdir -p "$MNT"
hdiutil attach "$WORK/rw.dmg" -mountpoint "$MNT" -nobrowse -quiet

mkdir -p "$MNT/.background"
cp "$BACKGROUND" "$MNT/.background/dmg-background.png"
cp "$DS_STORE" "$MNT/.DS_Store"
# Finder stamps what it touches and codesign refuses "Finder information"; the
# committed files are clean, and this keeps the copies clean too.
xattr -c "$MNT/.background/dmg-background.png" "$MNT/.DS_Store" 2>/dev/null || true
sync

hdiutil detach "$MNT" -quiet
rmdir "$MNT" 2>/dev/null || true

hdiutil convert "$WORK/rw.dmg" -format UDZO -ov -quiet -o "$OUT"
echo "wrote $OUT ($(du -h "$OUT" | cut -f1))"
