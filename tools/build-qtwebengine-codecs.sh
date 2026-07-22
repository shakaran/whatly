#!/usr/bin/env bash
# Build Qt WebEngine from source WITH proprietary codecs (H.264 / AAC), so
# WhatsApp video and voice messages work. Qt's official / aqt binaries ship
# WebEngine WITHOUT these codecs, which affects Whatly's AppImage and Windows
# builds (the Flatpak's KDE runtime already has them).
#
# This builds only the QtWebEngine module against an already-installed Qt of the
# SAME version, then installs the codec-enabled libraries over it. Run it on a
# machine with no CI time limit — a full Chromium build takes a few hours.
#
# Usage:
#   tools/build-qtwebengine-codecs.sh <qt-version> <qt-prefix> [build-dir]
# Example:
#   tools/build-qtwebengine-codecs.sh 6.10.2 ~/Qt/6.10.2/gcc_64
#
# The result overwrites the WebEngine libs inside <qt-prefix>. Back it up first
# if you want to keep the original (codec-less) build.
set -euo pipefail

VER="${1:?Usage: $0 <qt-version> <qt-prefix> [build-dir]}"
PREFIX="${2:?Usage: $0 <qt-version> <qt-prefix> [build-dir]}"
BUILD="${3:-$PWD/qtwebengine-codecs-build}"
MM="${VER%.*}"   # 6.10.2 -> 6.10

QMAKE="$PREFIX/bin/qmake"
CONFMOD="$PREFIX/bin/qt-configure-module"
if [[ ! -x "$CONFMOD" ]]; then
  echo "error: $CONFMOD not found — is <qt-prefix> a Qt $VER install?" >&2
  exit 1
fi

# ── Build dependencies ──────────────────────────────────────────────────────
# nasm is REQUIRED once proprietary codecs are on (ffmpeg assembly); the rest
# are the standard QtWebEngine/Chromium build tools.
need=(nasm gperf bison flex ninja node python3)
missing=()
for t in "${need[@]}"; do command -v "$t" >/dev/null 2>&1 || missing+=("$t"); done
if ((${#missing[@]})); then
  cat >&2 <<EOF
Missing build tools: ${missing[*]}
On Debian/Ubuntu:
  sudo apt-get install -y nasm gperf bison flex ninja-build nodejs python3 \\
    libnss3-dev libdbus-1-dev libegl1-mesa-dev libgbm-dev libxkbcommon-dev \\
    libxdamage-dev libxrandr-dev libxtst-dev libxss-dev
On Fedora:
  sudo dnf install -y nasm gperf bison flex ninja-build nodejs python3 \\
    nss-devel dbus-devel mesa-libEGL-devel libxkbcommon-devel
EOF
  exit 1
fi

# ── Optional: sccache/ccache to make re-builds fast ─────────────────────────
LAUNCHER_ARGS=()
if command -v sccache >/dev/null 2>&1; then
  echo "Using sccache as the compiler cache."
  export SCCACHE_CACHE_SIZE="${SCCACHE_CACHE_SIZE:-30G}"
  LAUNCHER_ARGS=(-DCMAKE_C_COMPILER_LAUNCHER=sccache
                 -DCMAKE_CXX_COMPILER_LAUNCHER=sccache)
elif command -v ccache >/dev/null 2>&1; then
  echo "Using ccache as the compiler cache."
  LAUNCHER_ARGS=(-DCMAKE_C_COMPILER_LAUNCHER=ccache
                 -DCMAKE_CXX_COMPILER_LAUNCHER=ccache)
fi

# ── Fetch the source (everywhere-src bundles Chromium; one download) ────────
SRC="qtwebengine-everywhere-src-$VER"
TAR="$SRC.tar.xz"
if [[ ! -d "$SRC" ]]; then
  URL="https://download.qt.io/official_releases/qt/$MM/$VER/submodules/$TAR"
  echo "Downloading $URL"
  curl -L --retry 3 -o "$TAR" "$URL"
  tar -xf "$TAR"
fi

# ── Configure (out-of-source) with proprietary codecs, then build + install ─
rm -rf "$BUILD"
mkdir -p "$BUILD"
cd "$BUILD"
"$CONFMOD" "$OLDPWD/$SRC" -- \
  -DCMAKE_BUILD_TYPE=Release \
  -DQT_FEATURE_webengine_proprietary_codecs=ON \
  "${LAUNCHER_ARGS[@]}"

cmake --build . --parallel
cmake --install .

echo
echo "Done. Codec-enabled Qt WebEngine installed into: $PREFIX"
echo "Verify with:  strings \"$PREFIX/lib/libQt6WebEngineCore.so\" | grep -i 'avc1\\|mp4a' | head"
command -v sccache >/dev/null 2>&1 && sccache --show-stats || true
