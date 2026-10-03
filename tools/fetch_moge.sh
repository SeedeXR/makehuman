#!/usr/bin/env bash
# Fetch the MoGe model into the cache, verifying what arrived.
#
# NOT committed and not fetched by the build: 134 MB against a 152 MB data/
# tree would roughly double the distribution for a feature most characters
# never use (LICENSING.md 5.2c). Everything that uses it skips when it is
# absent, so this is opt-in.
#
# The SHA256 is pinned, which is the same discipline LICENSING.md 5.1 applies
# to nlohmann/json: a download that can change under us is not a dependency,
# it is a liability.
set -euo pipefail

URL="https://huggingface.co/Ruicheng/moge-2-vits-normal-onnx/resolve/main/model.onnx"
SHA256="24eacb5dc7a2c54c7bc98f7de085ffbed79ad006ea5b664c2c2cdc02ff3a52f0"

dest="${MH_MOGE_MODEL:-$HOME/Library/Caches/MakeHuman/models/moge-2-vits-normal.onnx}"
mkdir -p "$(dirname "$dest")"

if [ -f "$dest" ]; then
    got="$(shasum -a 256 "$dest" | cut -d' ' -f1)"
    if [ "$got" = "$SHA256" ]; then
        echo "MoGe model already present and verified: $dest"
        exit 0
    fi
    # A file that is there but WRONG is worse than no file: it would load and
    # predict something. Say so and refuse rather than silently re-downloading
    # over whatever the user put there.
    echo "refusing to overwrite $dest: sha256 $got does not match the pinned $SHA256" >&2
    echo "move it aside and re-run if you want the pinned model" >&2
    exit 1
fi

echo "fetching MoGe (134 MB) -> $dest"
tmp="$dest.partial"
curl -fL --progress-bar -o "$tmp" "$URL"

got="$(shasum -a 256 "$tmp" | cut -d' ' -f1)"
if [ "$got" != "$SHA256" ]; then
    rm -f "$tmp"
    # Deleted, not left behind. A half-trusted 134 MB file in a cache is how a
    # corrupt download becomes a bug report three weeks later.
    echo "sha256 mismatch: got $got, pinned $SHA256 -- download discarded" >&2
    exit 1
fi
mv "$tmp" "$dest"
echo "verified and installed: $dest"
