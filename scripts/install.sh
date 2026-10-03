#!/bin/sh
# Build MakeHuman, put the disk image in output/, and install the app.
#
# One command for the whole trip: configure, compile, package, and drop
# MakeHuman.app into /Applications. The disk image is kept in `output/`, which
# is gitignored, so a build never leaves anything to commit.
#
#   scripts/install.sh                 build, package, install
#   scripts/install.sh --no-install    build and package only
#   scripts/install.sh --force         replace an existing installation
#   scripts/install.sh --prefix DIR    install somewhere other than /Applications
#
# WHY THE QUARANTINE FLAG COMES OFF. The app is signed AD-HOC -- free, no Apple
# Developer membership, which the owner has declined -- so Gatekeeper has no
# developer identity to check. Launching a quarantined ad-hoc bundle gives
# "MakeHuman.app is damaged and can't be opened", which is alarming and false.
# The bundle is built HERE, from this source tree, by this script; it did not
# come from the internet and the quarantine bit does not belong on it. Removing
# it is what makes a self-compiled app double-clickable, and it is exactly the
# step a downloaded binary should NOT get.
set -eu

ROOT=$(cd "$(dirname "$0")/.." && pwd)
PRESET=macos-arm64-release
OUT="$ROOT/output"
PREFIX=/Applications
DO_INSTALL=1
FORCE=0

while [ $# -gt 0 ]; do
    case "$1" in
        --no-install) DO_INSTALL=0 ;;
        --force) FORCE=1 ;;
        --prefix) shift; PREFIX=${1:?--prefix needs a directory} ;;
        --preset) shift; PRESET=${1:?--preset needs a name} ;;
        -h|--help) sed -n '2,20p' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
        *) echo "unknown option: $1 (try --help)" >&2; exit 2 ;;
    esac
    shift
done

case "$(uname -s)" in
    Darwin) ;;
    *) echo "this installs a macOS .app bundle; $(uname -s) is not macOS" >&2; exit 1 ;;
esac

echo "==> configuring ($PRESET)"
cmake --preset "$PRESET" >/dev/null

echo "==> building"
cmake --build --preset "$PRESET" -j

# The dmg target signs the bundle and verifies the signature, so a broken
# package fails HERE rather than on the machine it is copied to.
echo "==> packaging"
cmake --build --preset "$PRESET" --target dmg

DMG="$ROOT/build/$PRESET/MakeHuman.dmg"
[ -f "$DMG" ] || { echo "the dmg target produced no image at $DMG" >&2; exit 1; }

mkdir -p "$OUT"
cp "$DMG" "$OUT/MakeHuman.dmg"
echo "==> image: ${OUT#"$ROOT"/}/MakeHuman.dmg ($(du -h "$OUT/MakeHuman.dmg" | cut -f1))"

if [ "$DO_INSTALL" -eq 0 ]; then
    echo "==> --no-install, stopping before installation"
    exit 0
fi

DEST="$PREFIX/MakeHuman.app"
if [ -e "$DEST" ] && [ "$FORCE" -eq 0 ]; then
    echo "$DEST already exists. Re-run with --force to replace it." >&2
    exit 1
fi

MNT=$(mktemp -d)
# An image left mounted takes the volume name, so the next run fails somewhere
# less obvious than here.
cleanup() { hdiutil detach "$MNT" -force -quiet 2>/dev/null || true; rmdir "$MNT" 2>/dev/null || true; }
trap cleanup EXIT

echo "==> installing to $PREFIX"
hdiutil attach "$OUT/MakeHuman.dmg" -mountpoint "$MNT" -nobrowse -quiet
[ -d "$MNT/MakeHuman.app" ] || { echo "the image carries no MakeHuman.app" >&2; exit 1; }

# Into place via a temporary copy, so a failure part-way through does not leave
# a half-written bundle where a working one used to be.
STAGE="$PREFIX/.MakeHuman.app.$$"
rm -rf "$STAGE"
cp -R "$MNT/MakeHuman.app" "$STAGE"
rm -rf "$DEST"
mv "$STAGE" "$DEST"

xattr -dr com.apple.quarantine "$DEST" 2>/dev/null || true

# Say plainly whether what was installed is intact, rather than assuming the
# copy preserved the signature.
if codesign --verify --deep --strict "$DEST" >/dev/null 2>&1; then
    echo "==> installed $DEST (signature valid)"
else
    echo "==> installed $DEST, but its signature does not verify" >&2
    exit 1
fi

echo "    open it with:  open -a MakeHuman"
