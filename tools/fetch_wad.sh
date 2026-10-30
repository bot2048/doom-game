#!/bin/sh
# SPDX-License-Identifier: MIT
#
# Downloads the Freedoom: Phase 1 IWAD (BSD-licensed) into wad/, checking it
# against a known SHA-256. The WAD is not stored in git (28 MB).
set -eu

VERSION=0.13.0
URL="https://github.com/freedoom/freedoom/releases/download/v$VERSION/freedoom-$VERSION.zip"
WAD_SHA256=7323bcc168c5a45ff10749b339960e98314740a734c30d4b9f3337001f9e703d

root=$(cd "$(dirname "$0")/.." && pwd)
dest="$root/wad/freedoom1.wad"

if [ -f "$dest" ] && echo "$WAD_SHA256  $dest" | sha256sum -c --status; then
    echo "wad/freedoom1.wad is already present and verified."
    exit 0
fi

tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT

echo "Downloading Freedoom $VERSION..."
curl -fL --progress-bar -o "$tmp/freedoom.zip" "$URL"
unzip -q -o "$tmp/freedoom.zip" "freedoom-$VERSION/freedoom1.wad" -d "$tmp"

echo "$WAD_SHA256  $tmp/freedoom-$VERSION/freedoom1.wad" | sha256sum -c --status || {
    echo "Checksum mismatch: the downloaded WAD is not the expected file." >&2
    exit 1
}

mkdir -p "$root/wad"
mv "$tmp/freedoom-$VERSION/freedoom1.wad" "$dest"
echo "Saved $dest"
