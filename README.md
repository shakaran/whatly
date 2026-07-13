# WhatSie

Feature rich WhatsApp web client based on Qt WebEngine for Linux and Windows Desktop

> **This is a fork.** It is maintained by [Ángel Guzmán Maeso](https://shakaran.net)
> ([@shakaran](https://github.com/shakaran)) and builds on the original
> [WhatSie](https://github.com/keshavbhatt/whatsie) created by
> **Keshav Bhatt**, which remains MIT-licensed. All upstream copyright and
> authorship is preserved — see [LICENSE](LICENSE).

## What's new in this fork

On top of upstream WhatSie, this fork adds:

- **Windows 10+ support** from a single codebase — platform-specific pieces are
  behind `Q_OS_*` guards, so Linux behaviour is unchanged. Native toast
  notifications, Win32 Caps Lock detection and a GUI-subsystem executable with
  icon/version resources. Every push is compile-checked by a Windows CI workflow.
- **Connection watchdog** — WhatsApp Web's WebSocket can die or freeze, leaving
  the app stuck on *"Connecting…"* with messages that never send. A health probe
  now detects it and reloads the page automatically, capped at 3 attempts per
  hang so an unfixable cause (no disk space, network down) never turns into a
  reload loop.
- **Self-updating User-Agent** — the reported Chrome version is derived from the
  bundled Chromium instead of being hardcoded, so it can no longer go stale and
  get the client treated as an outdated browser. It follows Qt WebEngine upgrades
  automatically.
- **"Identify as WhatSie in linked devices"** — linked sessions show up on your
  phone as *"WhatSie for Linux"* (or the matching platform) instead of a generic
  *"Google Chrome (Linux)"*. Applies to devices linked afterwards.
- **"Close emoji/sticker panel when clicking outside"** (opt-in) — WhatsApp Web
  otherwise keeps the expressions panel open until its button is pressed again.
- **Quit actually quits** — since Qt 6.3 the minimize-to-tray veto cancelled the
  quit, so tray *Quit* / <kbd>Ctrl</kbd>+<kbd>Q</kbd> minimised the window
  instead of closing the app when it was visible.
- **Build docs that match reality** — the documented `make build-release` wrapper
  never existed; the build is plain CMake + Ninja (see below).

## Whatsie Key features

- Light and Dark Themes with automatic switching
- Customized Notifications & Native Notifications
- Keyboard Shortcuts
- BuiltIn download manager
- Mute Audio, Disable Notifications
- App Lock feature
- Hardware access permission manager
- Built in Spell Checker (with support for 31 Major languages)
- Other settings that let you control every aspect of WebApp like:
	+ Do not disturb mode
	+ Full view mode, lets you expand the main view to the full width of the window
	+ Ability to switch between Native & Custom notification
	+ Configurable notification popup timeout
	+ Mute all audio from Whatapp
	+ Disabling auto playback of media
	+ Minimize to tray on application start
	+ Toggle to enable single click hide to the system tray
	+ Switching download location
	+ Enable disable app lock on application start
	+ Auto-locking after a certain interval of time
	+ App lock password management
	+ Widget styling
	+ Configurable auto Theme switching based on day night time
	+ Configurable close button action
	+ Global App shortcuts
	+ Permission manager let you toggle camera mic and other hardware level permissions
	+ Configurable page zoom factor, switching based on window state maximized on normal 
	+ Configurable App User Agent
	+ Application Storage management, lets you clean residual cache and persistent data
	+ Close emoji/sticker panel when clicking outside (opt-in)
	+ Identify as WhatSie in linked devices, instead of a generic browser name

## Command line options:
Comes with general CLI support, with a bunch of options that let you interact with already running instances of Whatsie.

Run: `whatsie -h` to see all supported options.

```
Usage: whatsie [options]
Feature rich WhatsApp web client based on Qt WebEngine

Options:
  -h, --help           Displays help on commandline options
  -v, --version        Displays version information.
  -b, --build-info     Shows detailed current build infomation
  -w, --show-window    Show main window of running instance of WhatSie
  -s, --open-settings  Opens Settings dialog in a running instance of WhatSie
  -l, --lock-app       Locks a running instance of WhatSie
  -i, --open-about     Opens About dialog in a running instance of WhatSie
  -t, --toggle-theme   Toggle between dark & light theme in a running instance
                       of WhatSie
  -r, --reload-app     Reload the app in a running instance of WhatSie
  -n, --new-chat       Open new chat prompt in a running instance of WhatSie
```

## Keyboard shortcuts

WhatSie's own shortcuts. The same list is available in the app under
**Settings → Global shortcuts → Show shortcuts**.

| Shortcut | Action |
|---|---|
| <kbd>Ctrl</kbd>+<kbd>N</kbd> | New chat |
| <kbd>Ctrl</kbd>+<kbd>P</kbd> | Settings |
| <kbd>Ctrl</kbd>+<kbd>T</kbd> | Toggle light/dark theme |
| <kbd>Ctrl</kbd>+<kbd>L</kbd> | Lock the app |
| <kbd>Ctrl</kbd>+<kbd>W</kbd> | Minimize to tray |
| <kbd>Ctrl</kbd>+<kbd>Q</kbd> | Quit |
| <kbd>F5</kbd> | Reload |
| <kbd>F11</kbd> | Toggle fullscreen |

> **WhatsApp Web has its own shortcuts too** — for searching, starting a chat,
> marking as unread and so on. Those come from WhatsApp itself, not from
> WhatSie, so they will never show up in the list above; they simply work inside
> the app as they do in a browser. See WhatsApp's own keyboard-shortcuts help
> for that list.

## Build from Source (Linux)

### Requirements
 - git, cmake >= 3.24, ninja-build
 - Qt6 >= 6.10 (qt6-base-dev, qt6-webengine-dev, qt6-positioning-dev)
 - C++17 compiler (GCC 7+, Clang 5+)
 - libx11-dev

### Install Dependencies

**Ubuntu/Debian:**
```bash
sudo apt-get install cmake ninja-build qt6-base-dev qt6-webengine-dev \
    qt6-positioning-dev libx11-dev build-essential
```

**Fedora:**
```bash
sudo dnf install cmake ninja-build qt6-qtbase-devel qt6-qtwebengine-devel \
    qt6-qttools-devel libX11-devel gcc-c++
```

**Arch Linux:**
```bash
sudo pacman -S cmake ninja qt6-base qt6-webengine qt6-positioning
```

### Build & Run

The project uses CMake (with the Ninja generator) and bundles `libnotify-qt`
as a git submodule, so remember to initialise submodules after cloning.

```bash
git clone https://github.com/shakaran/whatsie.git
cd whatsie
git submodule update --init --recursive

# Configure and build (Release)
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel

# Run
./build/whatsie
```

### Install (Optional)

The install prefix is baked in at configure time, so set
`CMAKE_INSTALL_PREFIX` when configuring, then install.

```bash
# Install into your home (no sudo, ~/.local/bin is usually on PATH)
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_INSTALL_PREFIX="$HOME/.local"
cmake --build build --parallel
cmake --install build

# OR install system-wide to /usr (needs sudo)
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_INSTALL_PREFIX=/usr
cmake --build build --parallel
sudo cmake --install build
```

### Common Build Commands

```bash
# Debug build
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build --parallel

# Rebuild incrementally (after editing sources)
cmake --build build --parallel

# Build with a fixed number of jobs
cmake --build build -j8

# Clean build artifacts
rm -rf build

# Show version / build info
./build/whatsie --version
./build/whatsie --build-info
```

### Troubleshooting

| Problem | Solution |
|---------|----------|
| CMake not found | `sudo apt install cmake` |
| Qt6 not found | `sudo apt install qt6-base-dev qt6-webengine-dev` (or `export CMAKE_PREFIX_PATH=/usr/lib/cmake/Qt6`) |
| Ninja not found | `sudo apt install ninja-build` |
| `notify-qt` submodule missing | `git submodule update --init --recursive` |
| Permission denied on install | Reconfigure with `-DCMAKE_INSTALL_PREFIX=$HOME/.local` (no sudo) |

For detailed build instructions, see [`DOCS/BUILD_QUICK_REFERENCE.md`](DOCS/BUILD_QUICK_REFERENCE.md)
and [`DOCS/CMAKE_MIGRATION.md`](DOCS/CMAKE_MIGRATION.md).

## Build from Source (Windows)

### Requirements
 - Windows 10 or later
 - Visual Studio 2022 with the "Desktop development with C++" workload
   (MSVC is required — Qt WebEngine does not support MinGW)
 - Qt 6.10+ for MSVC 64-bit, with the Qt WebEngine, Qt WebChannel and
   Qt Positioning modules
 - git, cmake >= 3.24

### Build & Run

```bat
git clone https://github.com/shakaran/whatsie.git
cd whatsie
cmake -G "Visual Studio 17 2022" -A x64 -B build -DCMAKE_PREFIX_PATH=C:\Qt\6.10.0\msvc2022_64
cmake --build build --config Release
C:\Qt\6.10.0\msvc2022_64\bin\windeployqt.exe build\Release\whatsie.exe
build\Release\whatsie.exe
```

For detailed instructions, see [DOCS/WINDOWS_BUILD.md](DOCS/WINDOWS_BUILD.md).

Every push is also compile-checked on Windows by the `Windows Build` GitHub
Actions workflow, which uploads a ready-to-run build as a workflow artifact.

## Install Whatsie on Linux Desktop

> **Note:** this fork is not published to any store yet — build it from source
> (see above). The packages below distribute the **upstream** project by Keshav
> Bhatt, not this fork.

### On any snapd supported Linux distributions

 `snap install whatsie`

### On any Arch based Linux distribution
Using Arch User Repository (AUR), [AUR package for Whatsie](https://aur.archlinux.org/packages/whatsie-git) is maintained by [M0Rf30](https://github.com/M0Rf30)

 `yay -S whatsie-git`

## Screenshots (could be old)

![WhatSie Light Theme](https://github.com/shakaran/whatsie/blob/main/screenshots/1.jpg?raw=true)
![WhatSie Dark Theme](https://github.com/shakaran/whatsie/blob/main/screenshots/2.jpg?raw=true)
![WhatSie Setting module](https://github.com/shakaran/whatsie/blob/main/screenshots/4.jpg?raw=true)
![WhatSie App Lock screen](https://github.com/shakaran/whatsie/blob/main/screenshots/3.jpg?raw=true)
![WhatSie Shortcuts & Permissions](https://github.com/shakaran/whatsie/blob/main/screenshots/5.jpg?raw=true)
