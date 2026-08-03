# Media codecs (H.264 / MP4 video)

## The problem

WhatsApp needs the **proprietary H.264 / AAC** codecs to process and send **MP4
videos**. Qt WebEngine only has those codecs when it is *built* with
`-webengine-proprietary-codecs` (a compile-time flag). They are **off by default**
in the official Qt binaries — including the ones from **aqtinstall** that the CI
uses for the portable builds — because H.264/AAC are patent-encumbered
(MPEG-LA), which carries redistribution/licensing obligations.

Consequences:

- The **AppImage** and the **`.deb`/`.rpm`** built in CI (aqt Qt) **cannot send
  MP4 video**. WhatsApp rejects it as *"not supported"*. This also fails through
  WhatsApp's own **+** button, because it is the browser engine, not Whatly.
- **Photos work** (no codec needed) and **WebM/VP8/VP9 videos work** (open
  codecs).
- A **source build against a system Qt that ships the codecs** works fully. Many
  distros enable them (e.g. Arch's `qt6-webengine`; some Ubuntu builds too),
  which is why the AUR `whatly` (source) package can send MP4.

Whatly detects this at runtime (`video.canPlayType('video/mp4; …')`) and shows a
one-time notice when H.264 is missing, so a failed MP4 attach is explained.

> **Why there is no downloadable codec plugin.** Qt WebEngine links FFmpeg
> **statically** into `libQt6WebEngineCore`; unlike Electron/Chromium it has **no
> loadable `libffmpeg.so` hook**. A separate codec pack cannot be dropped in — the
> codecs must be present in the engine at build time.

## Checking whether an engine has the codecs

Run the app and, in a JS console (or a quick page), evaluate:

```js
document.createElement('video').canPlayType('video/mp4; codecs="avc1.42E01E"')
```

`"probably"`/`"maybe"` = H.264 present; `""` = missing (MP4 will be rejected).

## Enabling the codecs on your own machine

> **Project stance.** Whatly's official releases deliberately **do not ship the
> H.264/AAC codecs**, to keep the maintainer clear of the MPEG-LA patent
> licensing that redistributing them would involve (see *Licensing note* below).
> The steps here are for **building/enabling the codecs on your own machine for
> your own use**, where that licensing situation does not apply the same way. If
> you rebuild and then *redistribute* the result, the licensing responsibility
> becomes yours.

### Option A — package against a system Qt that has the codecs (recommended, light)

If your distro's Qt WebEngine already ships the codecs (verify with the snippet
above against the system Qt), you do **not** need to rebuild Qt at all: build
Whatly against the system Qt and bundle *that* Qt into the AppImage/`.deb`,
instead of the codec-less aqt Qt the CI uses.

1. Install the system Qt 6.10+ WebEngine dev packages for your distro and confirm
   they include the codecs (canPlayType returns `"probably"`).
2. Build against the system Qt (no `-DCMAKE_PREFIX_PATH` pointing at an aqt Qt):

   ```bash
   cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release \
     -DCMAKE_INSTALL_PREFIX=/usr
   cmake --build build --parallel
   DESTDIR="$PWD/AppDir" cmake --install build
   ```

3. Bundle the **system** Qt with linuxdeploy (point `QMAKE` at the system qmake),
   exactly as `.github/workflows/release-artifacts.yml` does but with the system
   Qt, then build the AppImage / repack the `.deb`. The bundled
   `libQt6WebEngineCore` then carries the codecs.

This is only a few GB and minutes. The trade-off is the **licensing** note below:
you are now redistributing H.264/AAC.

### Option B — build Qt WebEngine from source with the codecs (heavy, full control)

Only if no suitable system Qt is available. This is a **Chromium-scale build**:
roughly **80–120 GB of disk**, lots of RAM, and hours of CPU — not feasible on
GitHub-hosted runners, and it needs plenty of free space locally too.

```bash
# Match your Qt version exactly (e.g. 6.10.3).
git clone --depth 1 --branch v6.10.3 https://github.com/qt/qtwebengine.git
cmake -S qtwebengine -B qtwebengine-build -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_PREFIX_PATH="$QT_ROOT_DIR" \
  -DCMAKE_INSTALL_PREFIX="$QT_ROOT_DIR" \
  -DQT_FEATURE_webengine_proprietary_codecs=ON
cmake --build qtwebengine-build --parallel
cmake --install qtwebengine-build
```

Then bundle that Qt WebEngine as in Option A.

## Licensing note

H.264 and AAC are covered by patents (MPEG-LA / Via LA pools). This is why the
official Qt binaries and most distributions ship without them, and why Whatly's
CI portable builds do too — and why the project does **not** distribute
codec-enabled binaries: as the *distributor*, the maintainer would take on the
licensing obligations. Building or enabling the codecs **for your own use** is a
different matter; **redistributing** a codec-enabled binary makes those
obligations yours. Whatly ships WebM/VP9 support (royalty-free) out of the box and
leaves MP4 to a codec-enabled system Qt.

*This document is engineering guidance, not legal advice; consult the actual
licence terms (or a lawyer) before redistributing.*
