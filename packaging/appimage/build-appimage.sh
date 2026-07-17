#!/usr/bin/env bash
#
# Build a Whatly AppImage with Qt WebEngine bundled and .zsync delta-update
# information baked in (updates pulled from the GitHub releases).
#
# Needs linuxdeploy + the Qt plugin (downloaded here if missing) and a Qt >= 6.10
# in the environment. Run on the oldest distro you want to support so glibc stays
# compatible. Produces:
#   Whatly-<version>-x86_64.AppImage
#   Whatly-<version>-x86_64.AppImage.zsync   (upload both to the release)
#
# AppImageUpdate / the built-in updater then fetches only changed chunks.
set -euo pipefail

REPO="$(cd "$(dirname "$0")/../.." && pwd)"
cd "$REPO"
VERSION="$(grep -oP 'set\(PROJECT_VERSION \K[0-9.]+' CMakeLists.txt)"
TOOLS="$REPO/build-appimage-tools"; mkdir -p "$TOOLS"

fetch() { # url -> executable in $TOOLS
  local out="$TOOLS/$(basename "$1")"
  [ -x "$out" ] || { curl -fsSL "$1" -o "$out"; chmod +x "$out"; }
  echo "$out"
}
LD=$(fetch https://github.com/linuxdeploy/linuxdeploy/releases/download/continuous/linuxdeploy-x86_64.AppImage)
LDQT=$(fetch https://github.com/linuxdeploy/linuxdeploy-plugin-qt/releases/download/continuous/linuxdeploy-plugin-qt-x86_64.AppImage)

echo "==> Build + install into AppDir"
cmake -S . -B build-appimage -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/usr
cmake --build build-appimage --parallel
rm -rf AppDir
DESTDIR="$PWD/AppDir" cmake --install build-appimage

echo "==> Bundle Qt + pack the AppImage (with .zsync)"
export UPDATE_INFORMATION="gh-releases-zsync|shakaran|whatly|latest|Whatly-*-x86_64.AppImage.zsync"
export OUTPUT="Whatly-${VERSION}-x86_64.AppImage"
export QMAKE="${QMAKE:-$(command -v qmake6 || command -v qmake)}"
"$LD" --appdir AppDir \
  --desktop-file AppDir/usr/share/applications/net.shakaran.whatly.desktop \
  --icon-file AppDir/usr/share/icons/hicolor/256x256/apps/net.shakaran.whatly.png \
  --plugin qt --output appimage

echo "Built $OUTPUT (+ .zsync)"
