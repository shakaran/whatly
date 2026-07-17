#!/usr/bin/env bash
#
# Build (and install) the Whatly Flatpak locally.
#
#   packaging/flatpak/build-flatpak.sh            # build from the local checkout
#   packaging/flatpak/build-flatpak.sh --release  # build the tagged source + a bundle
#
# The default builds whatever is in your working tree (a `dir` source), which is
# what you want while developing. --release builds the git tag from the manifest
# and also writes a single-file whatly.flatpak you can share or attach to a
# GitHub release.
set -euo pipefail

REPO="$(cd "$(dirname "$0")/../.." && pwd)"
MANIFEST="$REPO/packaging/flatpak/net.shakaran.whatly.yml"
APPID="net.shakaran.whatly"
BUILDDIR="$REPO/build-flatpak"
RUNTIME_VER="6.9"
mode="${1:-}"

# 1. Tooling. flatpak-builder + the flathub remote (user-scoped, no root needed
#    for the remote/runtimes; only the apt install wants sudo).
if ! command -v flatpak-builder >/dev/null; then
  echo "==> Installing flatpak + flatpak-builder"
  sudo apt-get update -qq
  sudo apt-get install -y flatpak flatpak-builder
fi
flatpak remote-add --if-not-exists --user \
  flathub https://flathub.org/repo/flathub.flatpakrepo

# 2. Runtimes Whatly builds against (Qt6 + the WebEngine base app).
echo "==> Ensuring KDE $RUNTIME_VER runtime + QtWebEngine BaseApp"
flatpak install -y --user flathub \
  "org.kde.Platform//$RUNTIME_VER" \
  "org.kde.Sdk//$RUNTIME_VER" \
  "io.qt.qtwebengine.BaseApp//$RUNTIME_VER"

# 3. Build.
if [ "$mode" = "--release" ]; then
  echo "==> Building the tagged release + a shareable bundle"
  flatpak-builder --user --force-clean --repo="$REPO/build-flatpak-repo" \
    "$BUILDDIR" "$MANIFEST"
  flatpak build-bundle "$REPO/build-flatpak-repo" \
    "$REPO/whatly.flatpak" "$APPID"
  echo "Wrote $REPO/whatly.flatpak"
else
  echo "==> Building the local working tree"
  # Swap the git source for the local directory so it builds what you have.
  TMP="$(mktemp --suffix=.yml)"
  python3 - "$MANIFEST" "$REPO" > "$TMP" <<'PY'
import sys, re
manifest, repo = sys.argv[1], sys.argv[2]
text = open(manifest).read()
text = re.sub(r'(?s)    sources:.*$',
              "    sources:\n      - type: dir\n        path: " + repo + "\n",
              text)
sys.stdout.write(text)
PY
  flatpak-builder --user --install --force-clean "$BUILDDIR" "$TMP"
  rm -f "$TMP"
  echo "Installed. Run with: flatpak run $APPID"
fi
