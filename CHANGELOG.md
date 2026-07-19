## Unreleased

A sweep of engine-tuning features, with every existing feature kept intact.

**Performance & privacy settings.** Settings → *Performance & Privacy* now
exposes the rendering-engine knobs that used to be hard-coded. Whatly still
disables the GPU by default on Linux (the long-standing fix for blank windows
and start-up crashes on some GPU/driver setups, issues #200 / #234 / #252), but
you can now turn acceleration back on, ignore the driver blocklist, run the GPU
in-process, toggle GPU compositing and VSync, or pick a lower-memory process
model (single-process / process-per-site). A JavaScript memory cap
(`--max-old-space-size`, for the "eats RAM" reports #241 / #255) and an HTTP
cache type/size control round it out. A *Prevent WebRTC IP leak* switch stops
WebRTC from revealing your local IP over non-proxied connections. All of these
are stored machine-wide and applied at start-up, so a change takes effect after
a restart. Covered by new unit tests (`TstPerformance`) and translated into all
15 languages.

**Network proxy.** Settings → *Network & Startup* now lets you route Whatly
through a proxy: *System* (follow the OS, the default), *None* (connect
directly), or a manual *SOCKS5* / *HTTP* proxy with host, port and optional
username/password. It is applied application-wide, so every account uses it, and
manual changes take effect for new connections without a restart. Covered by new
unit tests (`TstNetworkProxy`).

**Start at login.** The same section has a *Start Whatly when I log in* switch.
On Linux it manages an XDG autostart entry
(`~/.config/autostart/net.shakaran.whatly.desktop`); on Windows a per-user
`Run` entry. Covered by new unit tests (`TstAutostart`).

**Interface scale control.** You can now set an interface/content scale factor
from Settings instead of only via the `QT_SCALE_FACTOR` environment variable
(which still wins if set). It scales the whole window and the page together
(matching #203) and applies after a restart.

**Portal notifications (Flatpak).** Native notifications can now be delivered
through the XDG desktop portal (`org.freedesktop.portal.Notification`) instead of
libnotify. A Flatpak build cannot always reach the system notification service
directly, but it can always reach the portal. Settings → notifications has a new
*Notification delivery* choice on Linux: *Automatic* (use the portal inside a
Flatpak sandbox, the system service otherwise), *Desktop portal (Flatpak)*, or
*System service (libnotify)*. Clicking a portal notification still raises the
window and marks the chat. Covered by new unit tests (`TstPortalNotification`).

**Custom JavaScript addons.** Alongside the existing custom-CSS support, you can
now load your own `.js` files to run on WhatsApp Web (Settings → *Custom
JavaScript addons*). Add several, tick/untick each to enable or disable it, or
remove it. Every addon runs inside its own `try`/`catch` sandbox, so a broken
one can never take down the page or the other addons. Covered by new unit tests
(`TstCustomJs`).

**Per-account custom CSS/JS.** Custom CSS and the new JS addons are now stored
per account: the default account keeps its existing `custom.css` (nothing moves
on upgrade), while a named `--profile` account gets its own stylesheet and its
own addon set.

**Grid view for multiple accounts.** When you have several in-window accounts you
can now show them all at once in a tiled grid instead of one at a time. Toggle it
from the tray menu (*Grid view* / *Tabbed view*) or with `Ctrl+G`; the choice is
remembered. The existing layouts are untouched: the tabbed view remains the
default, and separate accounts in separate windows are still available by
launching with `--profile`.

## 6.2.1 (2026-07-19)

Bug-fix and hardening release.

**Fix a crash in the automatic-theme setup.** Opening Settings → automatic theme
on a system with no geolocation backend (a headless run, or any box without a Qt
geo plugin) crashed on close: the dialog's destructor dereferenced a null
position source. Found by a new in-process SettingsWidget test.

**Clean shutdown on SIGTERM.** Whatly now quits gracefully when it receives
`SIGTERM` (from a session manager, `kill`, or systemd) instead of being torn
down abruptly, using the Qt-safe socketpair + `QSocketNotifier` pattern.

**Quieter terminal.** The benign "QThreadStorage: entry … destroyed before end
of thread" lines that Qt WebEngine prints while tearing down at exit are no
longer echoed to the terminal (they are still kept in the in-app debug log for
bug reports).

**About screen icons.** The buttons on the About dialog (Donate, Ko-fi, Wise,
Rate, More apps, Source code, Report a bug, Debug info) were rendering without
their icons. They are set from the bundled resources again, and two new icons —
`heart-line` and `github-line` — were added for the Ko-fi and Source-code
buttons. The Rate-the-app screen's logo and button icons were verified to be
correct and unchanged.

**Unit tests.** A QtTest suite now covers the headless parts of the app — the
`Utils` helpers (including the cache-delete guard from issue #230), the
injected-script generators (fonts, chat themes, muted status, privacy blur,
wallpaper, custom CSS, tweaks, linked-device name), the scheduled-message queue
and its persistence, the sun calculations, identicons, palettes, dictionary
resolution and the About/Rate screens' assets. Roughly 92% line / 95% function
coverage of that layer (measure it with `tools/coverage.sh`). Build with
`-DWHATLY_TESTS=ON`, run with `ctest`; it also runs in CI on every push.

## 6.2.0 (2026-07-18)

Desktop-integration features and finer control, from a sweep of the upstream
issue tracker.

**Taskbar unread badge.** The unread total is now published as a launcher badge
over the standard `com.canonical.Unity.LauncherEntry` D-Bus protocol, so KDE
Plasma's task manager, GNOME's Dash-to-Dock and others paint the count on
Whatly's taskbar button — no extra dependency, ignored where it isn't supported
(issue #122).

**Interface font size.** Settings → Appearance has a new *Interface font size*
control that scales the app's own chrome — menus, dialogs and Settings itself —
independently of WhatsApp Web's zoom. Applied live and on the next launch (issue
#76).

**Reload automatically after a crash.** An optional setting reloads WhatsApp
Web's page process by itself if it ever crashes, instead of interrupting you with
a prompt (issue #225).

**Network status in the tray.** The tray tooltip now reads "Waiting for network…"
while disconnected, so a silent drop after a suspend/resume is noticeable beyond
the dimmed icon (issue #208).

**HiDPI / 4K scaling.** Setting `QT_SCALE_FACTOR` now also scales WhatsApp Web's
content to match, so a single variable enlarges both the window chrome and the
page on high-density displays (issue #203). Setting `WHATLY_MAX_FPS` to a
non-zero value lifts Chromium's frame-rate cap for those who want it (issue #221).

**Build.** Source distributions can now build a subset of the spell-check
dictionaries with `-DWHATLY_DICTIONARIES="en-US;es-ES;…"` instead of all 31
(issue #61).

**Windows.** The release workflow can optionally code-sign the Windows build via
the SignPath Foundation free open-source programme; it stays off until configured
(issue #325, see `packaging/windows-signing.md`).

**macOS (experimental).** Whatly now builds as a macOS `.app` bundle; CI produces
a `.dmg` and attaches it to the release. It is **unsigned** (first launch needs a
right-click → Open) and has not yet been validated at runtime — community testing
is welcome (issue #119).

**Small screens.** The window and dialog minimum sizes now adapt to the display,
so Whatly fits on small screens such as a Linux phone in portrait instead of
being pinned too large (issue #239).

All new interface strings are translated into the 15 shipped languages.

## 6.1.1 (2026-07-18)

Bug-fix release.

**Sharper notification icons.** Native notifications carried their app icon as a
32-pixel image, which looked blurry on desktops that draw large notification
popups (for example Cinnamon on Linux Mint). Whatly now hands the notification
daemon a 256-pixel icon, so the logo stays crisp at any popup size (issue #2).

## 6.1.0 (2026-07-17)

New features and more ways to install.

**Scheduled messages.** Write a message to any number and have Whatly send it at
a time you pick. The schedule is saved to disk, so a message still goes out even
if Whatly was closed when it came due — the next time you open the app, anything
overdue is sent as a catch-up. While the app is open a timer sends due messages
on time. Manage the queue from **Scheduled messages…** in the tray menu. Sending
opens the recipient's chat (issue #250).

**Font family.** Settings → Appearance now has a *Font family* picker that
renders WhatsApp Web's text in any font installed on your system. Emoji, icons
and monospaced message formatting are left untouched (issue #219).

**Hide muted status updates.** A new toggle hides the "Muted updates" section of
the Status panel, so statuses from contacts you have muted do not show up at all
(issue #242).

**Donations.** Wise is now offered alongside Ko-fi and PayPal, in the About
dialog and the README.

**Packaging.** Whatly can now be built and installed as a **Flatpak** and an
**AppImage** (with `.zsync` delta updates), in addition to the snap, deb and
Fedora/COPR configurations. Flatpak and AppImage are built automatically on each
release and attached to the GitHub release. See `packaging/README.md`.

All new interface strings are translated into the 15 shipped languages.

## 6.0.0 (2026-07-16)

First release of the fork maintained at https://github.com/shakaran/whatly.
Everything below is on top of upstream 5.1.0.

**The app is now called Whatly** (it was WhatSie). The application id is
`net.shakaran.whatly`, the binary is `whatly`, and user data lives under
`shakaran/whatly`. Existing installs are carried over automatically on first
run — settings and the logged-in session are copied from the old `shakaran`
namespace and from the previous `WhatSie` layout, so nobody is logged out by
upgrading. If the automatic copy ever misses something, `whatly
--migrate-from=whatsie` does it by hand (`--dry-run` to preview first).

#### 🎁 Features

* **Multiple WhatsApp accounts.** Either as separate windows —
  `whatly --profile=<name>`, each with its own session, settings file and
  instance — or as tabs inside one window, added with a **+** and renamed or
  removed from a right-click menu. Every account is a separate Chromium storage
  partition, so the sessions never touch; the tray badge sums the unread count
  across them all. The default account keeps the exact storage it had, so an
  upgrade neither moves it nor logs it out, and the tab bar hides itself when
  there is only one account.
* **Spell checker, working.** It was not broken, it was gone — and the build
  asserted that the system hunspell package supplied the dictionaries. Qt
  WebEngine uses Chromium's spell checker, which reads `.bdic` and cannot read
  hunspell's `.dic`/`.aff` at all, so the language list was empty on every
  distribution. The 31 dictionaries already in the tree are now converted at
  build time and installed.
* **Chat themes.** Fourteen of them, recolouring WhatsApp Web itself. Keyed on
  the colour *values*, since WhatsApp's CSS variable names are
  compiler-generated and change with each of its deploys. Photos and avatars are
  untouched.
* **Chat wallpaper** — your own image behind the messages.
* **Custom CSS.** Load a .css file to restyle WhatsApp Web — the community
  stylesheets (catppuccin and the like) work — applied on top of the chat theme.
* **Smooth scrolling**, as an option.
* **Privacy blur.** Blurs the chat list and the open conversation until you
  hover a row, so someone glancing at the screen cannot read them. Five levels.
* **Theme and blur buttons inside WhatsApp's own sidebar**, above the avatar,
  reachable without opening Settings.
* **Interface translations**, with a language picker. 15 languages (Italian was
  human-written; the other 14 are machine-translated and unreviewed).
* **Windows support**, behind `Q_OS` guards, with a build workflow.
* Image paste from a browser's clipboard.
* A connection watchdog that reloads the page when WhatsApp's WebSocket hangs,
  capped at three reloads per episode.
* Identify as Whatly in the phone's linked-devices list, instead of as Chrome —
  reported as a desktop client so the phone shows a computer icon beside the
  "Whatly for Linux" label rather than leaving it blank.
* Close the emoji panel by clicking outside it (opt-in).
* `F1` opens About; its **Report a Bug** button fills in the GitHub issue with
  the version, commit, memory usage of the whole process tree, and the recent
  log — including WhatsApp Web's console.

#### 🐞 Bug Fixes

* **Logging out of KDE could stall on Whatly.** With close-to-tray on, the
  window vetoed *every* close — including the one the session manager sends at
  logout — so the desktop saw an app refusing to quit and waited on it. A
  session-end close is now honoured as a real quit. (Reported on KDE; the fix
  hooks `QGuiApplication::commitDataRequest`, which fires only on a real session
  logout, so it could not be exercised in a normal run.)
* **Clearing the cache could delete your home directory.** If the profile ever
  handed back an empty storage path, the recursive delete ran on `.` — the
  working directory, which is your home when the app is launched from a desktop
  or file manager. The delete now refuses any path that is empty, relative, the
  home directory, the root, or not inside the app's own storage. (Gentoo dropped
  the app over this.)
* **The theme could not be set to dark in any language but English.** The
  setting was stored as the combo box's *displayed* text, which is translated:
  running in Spanish wrote `windowTheme=claro`, and every comparison in the code
  is against `"dark"`. A value written by an older build is repaired at startup.
* **The permissions dialog did nothing at all** — wrong enum, a double-prefixed
  settings key, and a signal nothing was connected to. It never once reached the
  engine. This is also why voice and video calls appeared not to work: the
  microphone and camera could not be granted.
* **Notifications went to the app's own popup, on the primary monitor**, rather
  than to the desktop's notification service and the screen the window is on.
* Notification avatars had red and blue swapped (a byte-order mismatch between
  `QImage` and the freedesktop `image-data` hint).
* The window would not restore down from maximized (Wayland reports the
  maximized geometry as the normal one).
* The theme was reset to light on every exit.
* Pasting an image copied from a browser silently produced "no content".
* Attachments: the desktop's file chooser is used, and the last directory is
  remembered. Qt's built-in dialog has no bookmarks, no Recent and no address
  bar, so a file outside the directory it happened to open in was unreachable.
* Quitting could turn into minimize-to-tray.
* "Restore" in the tray menu could be left permanently disabled, with no way
  left to bring the window back.
* Logging out hung on the "Logging out" overlay and opened a browser tab.
* Unhandled permission types were denied *and the denial was persisted*, so they
  stayed denied forever once the app learned to ask.
* The injected sidebar buttons burned about 40% of a CPU core with the app
  idle — a MutationObserver whose own repaints retriggered it.
* The User-Agent is derived from the engine, so it always reports the truth.

#### 📦 Packaging (snap)

* Stage `libxcb-shape0`: `libqxcb` links against it, and without it the snap
  does not launch at all.
* Drop the `hunspell-dictionaries` content mount and `DICPATH`. It could never
  have fed the spell checker — Chromium reads `.bdic`, not hunspell's
  `.dic`/`.aff` — and the dictionaries are shipped inside the snap now.

#### 📖 Documentation

* The build docs describe the actual CMake build. Old Qt or CMake now fails with
  an actionable error instead of a confusing one.
* `docs/TRANSLATIONS.md`, `docs/WINDOWS_BUILD.md`.

#### ⚠️ Known limitations

* **Screen sharing during a call does not work.** Qt WebEngine as packaged
  enumerates zero screens and zero windows (measured, on Wayland and on X11
  alike); it is built without PipeWire. Nothing in the application can change
  that.
* Memory use is Chromium's, not the app's: with every injected script disabled,
  the renderer still accounts for the great majority of it.

## 5.1.0 (2026-04-03)

#### 🎁 Feature

* migrate build system workflow to CMake + Ninja for Qt 6 builds

#### 🐞 Bug Fixes

* improve WebEngine profile/session persistence handling on Qt 6
* update in-app theme application logic for current WhatsApp Web changes
* show notifications through org.freedesktop.Notifications directly on Qt 6

#### 🚧 Chores

* align release metadata for minor version bump to 5.1.0

## 4.16.0 (2024-10-09)

#### 🎁 Feature

* Add secure compilation flags (4720ffeb)

#### 🐞 Bug Fixes

* change zoom factor when app starts minimized in systray (be47a73d)
* set QTWEBENGINE_DICTIONARIES_PATH to fix spell checker (#199) (40da519b)

## 4.15.1 (2024-08-01)

#### 🚧 Chores

* revert to build action Qt to version 5.15.2 (bbccc551)
* bump version 4.15.1 (d8746929)
* update dist icon (d0304ad1)
* new icon (f54e608c)
* update notification icons (2718a85d)
* add new keywords to appdata (4e803bfa)
* update appstream metadata for 4.15.0 release (a535ee1b)
* do not mark statusCode unused in desktopOpenUrl util method (51e28237)
* udapte CHANGELOG (5940b6ec)


## 4.15.0 (2024-05-25)

#### 🎁 Feature

* efficient use of element mutation observers (e553c00c)
* add line widget at bottom of cutom notification widget (0d19713d)
* use xdg-open as fallback to open local files (30921582)

#### 🐞 Bug Fixes

* restore window state video fullscreen toggle (e0483dda)
* prioritize xdg-open for handling openUrl request (36cd46b7)
* set correct lock state when app starts minimized (b58d9422)

#### 🚧 Chores

* CI Update Qt version to 5.15.4 for build (e030486b)
* update UA to latest Chrome (cc2852a7)
* snap use portal if available (8cef2047)
* version 4.15 (08fba4cf)

## 4.14.2 (2023-12-01)

#### 🐞 Bug Fixes

* incorrect full width modification (#150) (3d20ebe2)

#### 🚧 Chores

* bump version 4.14.2 (c478a7d6)
* Use qmake-provided _DATE_ (#146) (fec5644c)



## 4.14.1 (2023-05-20)

#### 🐞 Bug Fixes

* fix unread message parsing (906ca7eb)

#### 🚧 Chores

* update workflow and changelog (41225b19)



## 4.14.0 (2023-05-17)

#### 🎁 Feature

* minor fixes + code cleanup (5f10a0f9)

#### 📄 Documentation

* **changelog:** update cl & ver after release (b4b5dc33)

#### 🚧 Chores

* bump version to 4.14 (c235b8ae)


## 4.13.0 (2023-03-22)

#### 🎁 Feature

* add open file option in download item (7b64cf51)

#### 🐞 Bug Fixes

* prevent overwrite if file exists (dd687dc9)

#### 📄 Documentation

* **changelog:** update cl & ver after release (fa5add01)

#### 🚧 Chores

* bump version 4.13 (3484bccc)
* update js to improve perf (1b030ca7)
* code cleanup (c2bd1a32)
* update utils (8f983031)


## 4.12.1 (2023-01-27)

#### 🐞 Bug Fixes

* icon on Plasma Wayland (#100) (7fc4ce38)

#### 📄 Documentation

* **changelog:** update cl & ver after release (4417eced)

#### 🚧 Chores

* code cleanup (085205eb)
* cleanup (011db449)
* escape key to close child windows (56c55a94)


## 4.12.0 (2023-01-26)

#### 🎁 Feature

* close permission dialog with esc key (ee519bcc)
* close with esc button (2119c3d1)

#### 🐞 Bug Fixes

* prevent zoom with ctrl+mouse (0eb7ea05)

#### 📄 Documentation

* **changelog:** update cl & ver after release (b3e2a2be)

#### 🚧 Chores

* bump version 4.12 (12bce6d2)
* cleanup (c78394d1)


## 4.11.0 (2023-01-26)

#### 🎁 Feature

* bump version 4.11 (63440d96)

#### 🐞 Bug Fixes

* applock state fix (32b0e5ca)

#### 📄 Documentation

* **changelog:** update cl & ver after release (74216cfd)

#### 🚧 Chores

* remove debugging from MoreApps widget (103a3686)
* cleanup + inhancements (f31197a4)
* cleanup + addition (21ca36bd)


## 4.10.3 (2022-12-18)

#### 📄 Documentation

* **changelog:** update cl & ver after release (d7f1faee)

#### 🚧 Chores

* add moreapps widget in lock screen (074b0f98)


## 4.10.2 (2022-09-20)

#### 📄 Documentation

* **changelog:** update cl & ver after release (3a71de33)

#### 🚧 Chores

* make description under 1000 chars (1c5bfc42)


## 4.10.1 (2022-09-17)

#### 📄 Documentation

* **changelog:** update cl & ver after release (56c06a92)

#### 🚧 Chores

* update appdata, to make flatpak-builder happy (c8b1b838)


## 4.10.0 (2022-09-17)

#### 🎁 Feature

* systemtray notification counter (530c24bf)
* add toggle theme desktop action (f8c9b339)
* **ci:** add release workflow (83cd6383)
* **i18n:** add Italian localization (#55) (ced5547d)
* enable support for traybar entries on GNOME dash (#53) (66d20d3e)
* some new features (21113900)
* unlock animation plus some cleanup (0a182a9e)
* implement IPC & other improvements (81faa022)
* add open downloads directory button in download widget (419ffb29)
* app auto locking (d06a4abb)
* v4.0 (#35) (474b9212)
* start application minimized. closes #19 (c5bf7a98)

#### 🐞 Bug Fixes

* duplicate action in desktop file (2837c87e)
* auto lock while scrolling (baa52666)
* **build:** fix build due to missing icon (5464060d)
* focus on password edit when echo (cee2dc85)
* **web:** bypass lock check while loading quirk (6c6275c3)
* obey fullview settings on first launch & initial window size (b2f0fe49)
* properly hide custom notification on multi monitor setups (20057675)
* use availableGeometry to map position of notification (538d7d5d)
* add missing icon, enabling install_icon target generation (clos… (#45) (48b9028f)
* show notifications on correct screen (ff99a5f7)
* logout flow during changepassword (92382d7b)
* properly load setting for autoapplock checkbox (522eb75a)
* save geometry in quit event (4a968554)
* raise window from hidden state when clicked on notification (0620e43e)
* debug in debug mode (147487f2)
* notification popup click behavior (e800208f)
* **snap:** supress warnings (f2b06da6)
* improve logout flow, on change password (ed5f760b)
* change lock screen password beahvior (fa4012a5)
* theme switching (7cd4b219)
* improve download file behavior (#32) (8f071469)

#### 📄 Documentation

* **changelog:** update cl & ver after release (1f2bb6fc)
* **changelog:** update cl & ver after release (5cdba515)
* **changelog:** update cl & ver after release (9472e9e6)
* **changelog:** update cl & ver after release (bd7386a1)
* **changelog:** update cl & ver after release (554ceff4)
* **changelog:** update cl & ver after release (572d6948)
* **changelog:** update cl & ver after release (e0d15c2e)
* **changelog:** update cl & ver after release (974933d0)

#### 🎨 Styles

* code refactor (21940ee6)

#### 🚧 Chores

* update appdata (273a9138)
* **snap:** use svg icon for snap store (bcec3eef)
* skip dictionaries conversion if build type is flatpak build (f16086c2)
* Version bump 4.9 (af39ff62)
* **snap:** use svg icon for desktop entry (51dcb0d4)
* **ci:** release dry-run action (7ef46aaf)
* delete filedialog after exec (5e50519c)
* add notification icons (997ae821)
* **webengine:** disable support for Pepper plugins (325d841e)
* some enhancements (ff575c45)
* update UA to 104 (d03e9fc6)
* bump version 4.8 (7fde1e4c)
* unlock action button (771625da)
* **build:** use Qt5.15.4 for build (54f97210)
* update readme (0d3bd466)
* udapte new settings window screenshot (6f3f18c6)
* remove unused xml module (2d71f12c)
* version bump in pro (211699e3)
* **ci:** disable auto release and version file (6f134db2)
* Merge branch 'dev' (9f566869)
* **ui:** update ui color (8a74ccbc)
* use pre-commit (f82dcc68)
* update todo (2aa08e03)
* setQuitOnLastWindowClosed false (c751be26)
* set a minimum of 4 digits for the lock code (#56) (79b2b791)
* notification connect before show (f8455de7)
* update app description (c6fd2e8d)
* use appinstall artifacts from dist (247ed75f)
* distribution related files (88c46fad)
* **CI:** use latest version of install-qt-action (60b6c225)
* **CI:** build with github action (ac31abdb)
* define fallback values for macros (14f190c0)
* **qmake:** avoid error message when .git folder is missing (close #49) (#52) (91d0cf11)
* add full view support closes #46 (b96a28db)
* version 4.4 (26f5659b)
* install dicts using qmake (90210de2)
* add git sponser link (122828f4)
* improve settings window show behavior (d9909011)
* improve window geo restore (3a08d5d5)
* nitification popup tweak; code cleanup (5c2764f7)
* update readme (a4c73b0f)
* version 4.3 (3dae93a1)
* use Ctrl+W to hide window to tray (dba5a9bc)
* filter contextmenu items (6f4750c8)
* restore window directly when another instance is launched (39117158)
* use new chat trigger method to invoke new chats (1d950cd8)
* update changelog (59abd9d9)
* version 4.2 (1f4816a2)
* remove runguard (8c0df6d3)
* window show behavior (7d302466)
* update default UA (dfb5b9ca)
* stop timer instantly if rated already (cc43d4c7)
* bump version 4.1 (a1af1bde)
* minor improvements (ea4056dc)
* clean UA & disable js debug in app stdout (8cfbcf4b)
* set default zoom factor for maximized windows to 1.0 (046e2e13)
* inform app is minimized via notification (19734a99)
* unify passowrd echomode in lock widget (5be4cae9)
* test qpt gtk3 (020ac6da)
* add Desktop entry GenericName (e4bbdd15)
* move desktop file to src (4f0558a9)
* use desktop-launch from content snap (dcc39239)

#### 📦 Build

* **snap:** use SNAPCRAFT_ARCH_TRIPLET (8962c8bb)
* migrate to qt 5.15 (9867a6b6)

#### chaore

* **CI:** use Qt 5.15.2 (846d1218)

#### cleanup

* removed snap_launcher (e658c464)


## 4.9.1 (2022-09-13)

#### 📄 Documentation

* **changelog:** update cl & ver after release (5cdba515)

#### 🚧 Chores

* skip dictionaries conversion if build type is flatpak build (f16086c2)


## 4.9.0 (2022-09-03)

#### 🎁 Feature

* systemtray notification counter (530c24bf)

#### 📄 Documentation

* **changelog:** update cl & ver after release (9472e9e6)

#### 🚧 Chores

* Version bump 4.9 (af39ff62)
* **snap:** use svg icon for desktop entry (51dcb0d4)
* **ci:** release dry-run action (7ef46aaf)
* delete filedialog after exec (5e50519c)
* add notification icons (997ae821)
* **webengine:** disable support for Pepper plugins (325d841e)
* some enhancements (ff575c45)
* update UA to 104 (d03e9fc6)


## 4.8.2 (2022-08-27)

#### 🐞 Bug Fixes

* duplicate action in desktop file (2837c87e)
* auto lock while scrolling (baa52666)

#### 📄 Documentation

* **changelog:** update cl & ver after release (bd7386a1)


## 4.8.1 (2022-08-27)

#### 🐞 Bug Fixes

* **build:** fix build due to missing icon (5464060d)

#### 📄 Documentation

* **changelog:** update cl & ver after release (554ceff4)

#### 🚧 Chores

* bump version 4.8 (7fde1e4c)


## 4.7.2 (2022-07-22)

#### 📄 Documentation

* **changelog:** update cl & ver after release (e0d15c2e)


## 4.7.1 (2022-07-04)

#### 🐞 Bug Fixes

* focus on password edit when echo (cee2dc85)
* **web:** bypass lock check while loading quirk (6c6275c3)

#### 📄 Documentation

* **changelog:** update cl & ver after release (974933d0)

#### 🚧 Chores

* Merge branch 'dev' (9f566869)
* **ui:** update ui color (8a74ccbc)


## 4.7.0 (2022-07-03)

#### 🎁 Feature

* **ci:** add release workflow (83cd6383)
* **i18n:** add Italian localization (#55) (ced5547d)
* enable support for traybar entries on GNOME dash (#53) (66d20d3e)
* some new features (21113900)
* unlock animation plus some cleanup (0a182a9e)
* implement IPC & other improvements (81faa022)
* add open downloads directory button in download widget (419ffb29)
* app auto locking (d06a4abb)
* v4.0 (#35) (474b9212)
* start application minimized. closes #19 (c5bf7a98)

#### 🐞 Bug Fixes

* obey fullview settings on first launch & initial window size (b2f0fe49)
* properly hide custom notification on multi monitor setups (20057675)
* use availableGeometry to map position of notification (538d7d5d)
* add missing icon, enabling install_icon target generation (clos… (#45) (48b9028f)
* show notifications on correct screen (ff99a5f7)
* logout flow during changepassword (92382d7b)
* properly load setting for autoapplock checkbox (522eb75a)
* save geometry in quit event (4a968554)
* raise window from hidden state when clicked on notification (0620e43e)
* debug in debug mode (147487f2)
* notification popup click behavior (e800208f)
* **snap:** supress warnings (f2b06da6)
* improve logout flow, on change password (ed5f760b)
* change lock screen password beahvior (fa4012a5)
* theme switching (7cd4b219)
* improve download file behavior (#32) (8f071469)

#### 🎨 Styles

* code refactor (21940ee6)

#### 🚧 Chores

* use pre-commit (f82dcc68)
* update todo (2aa08e03)
* setQuitOnLastWindowClosed false (c751be26)
* set a minimum of 4 digits for the lock code (#56) (79b2b791)
* notification connect before show (f8455de7)
* update app description (c6fd2e8d)
* use appinstall artifacts from dist (247ed75f)
* distribution related files (88c46fad)
* **CI:** use latest version of install-qt-action (60b6c225)
* **CI:** build with github action (ac31abdb)
* define fallback values for macros (14f190c0)
* **qmake:** avoid error message when .git folder is missing (close #49) (#52) (91d0cf11)
* add full view support closes #46 (b96a28db)
* version 4.4 (26f5659b)
* install dicts using qmake (90210de2)
* add git sponser link (122828f4)
* improve settings window show behavior (d9909011)
* improve window geo restore (3a08d5d5)
* nitification popup tweak; code cleanup (5c2764f7)
* update readme (a4c73b0f)
* version 4.3 (3dae93a1)
* use Ctrl+W to hide window to tray (dba5a9bc)
* filter contextmenu items (6f4750c8)
* restore window directly when another instance is launched (39117158)
* use new chat trigger method to invoke new chats (1d950cd8)
* update changelog (59abd9d9)
* version 4.2 (1f4816a2)
* remove runguard (8c0df6d3)
* window show behavior (7d302466)
* update default UA (dfb5b9ca)
* stop timer instantly if rated already (cc43d4c7)
* bump version 4.1 (a1af1bde)
* minor improvements (ea4056dc)
* clean UA & disable js debug in app stdout (8cfbcf4b)
* set default zoom factor for maximized windows to 1.0 (046e2e13)
* inform app is minimized via notification (19734a99)
* unify passowrd echomode in lock widget (5be4cae9)
* test qpt gtk3 (020ac6da)
* add Desktop entry GenericName (e4bbdd15)
* move desktop file to src (4f0558a9)
* use desktop-launch from content snap (dcc39239)

#### 📦 Build

* **snap:** use SNAPCRAFT_ARCH_TRIPLET (8962c8bb)
* migrate to qt 5.15 (9867a6b6)

#### chaore

* **CI:** use Qt 5.15.2 (846d1218)

#### cleanup

* removed snap_launcher (e658c464)


## 4.6.5 (2022-07-03)

#### 🚧 Chores

* **ci:** update release wf (#59) (f40ac9c9)


## 4.6.3 (2022-07-03)

#### 📄 Documentation

* **changelog:** update changelog after release (7699d885)

#### 🚧 Chores

* **ci:** fix update file name (c0158c0d)


## 4.6.2 (2022-07-03)

#### 📄 Documentation

* **changelog:** update changelog after release (c87524db)

#### 🚧 Chores

* **ci:** update version on release (d715c8eb)


## 4.6.1 (2022-07-03)

#### 🚧 Chores

* **CI:** commit changelog on release (75b0cffe)


## Change log:

### 4.3
- feat: IPC; restore window directly when another instance is launched
- feat: allow context menu on editable, selected and copyble data types
- fix: properly load setting for autoapplock checkbox
- fix: logout flow during changepassword
- fix: the minimize behavior; replace Ctrl+H with Ctrl+W to hide window to tray

### 4.2
- fix: raise window from hidden state when clicked on notification
- updated new UA
- fix: window geometry persistence behavior
- feat: open download directory straight from the download manager
- fix: consistent window show behavior
- feat: implement IPC
   - lets run only one instance of application
   - lets pass arguments from secondary instances to main instance
   - open new chat without reloading page
   - restore application with command line argument to secondary instance:
          example: `whatly whatsapp://whatly`
          will restore the primary instance of whatly process

### 4.0
- fix(SystemTray) tray icon uses png rather than SVG
- feat(SystemTray) added settings to lets users change the system tray icon click behavior(minimize/maximize on right-click)
- feat(Download) added setting that lets the user set default download directory, avoid asking while saving files
- fix(Notification) clicking popup now correctly restores the app window
- feat(Lock) added setting to let users change the current set password for the lock screen
- feat(Lock) added setting to enable disable auto app locking, with defined duration
- feat(Lock) current set password is now hidden by default and can be revealed for 5 seconds by pressing the view button
- feat(Style/Theme) added ability to change widget style on the fly, added default light palette (prevent breaking of light theme on KDE EVs)
- fix(Theme) dark theme update
- feat(WebApp) added setting to set zoom factor when the window is maximized and fullscreen (gives user ability to set different zoom factor for Normal, Maximized(Fullscreen WindowStates)
- fix(Setting) settings UI is more organized
- fix(WebApp) enable JavaScript execCommand("paste")
- feat(WebApp) tested for new WhatsApp Web that lets users use Whatly without requiring the phone connected to the internet
- fix(Lock) unify passowrd echomode in lock widget


