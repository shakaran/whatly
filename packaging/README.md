# Packaging

Whatly ships as a **snap** (the primary, published channel — see `snap/`) plus
these additional formats. All build the same CMake project; only the wrapper
differs. Flatpak and AppImage are also built automatically on every `v*` tag by
the **Release Artifacts** workflow and attached to the GitHub release.

> **Why `snap/` and `debian/` live at the repo root instead of here.** It is not
> a choice — their tools only look in fixed locations. `snapcraft` reads
> `snap/snapcraft.yaml` (or root `snapcraft.yaml`, or `build-aux/snap/`), never
> `packaging/snap/`; and `dpkg-buildpackage`/`debhelper` require `debian/` at the
> source-package root. The formats whose tooling takes an explicit manifest path
> (Flatpak, AppImage, RPM) are the ones that can — and do — live under
> `packaging/`.

| Format | Where | CI | Local build |
|--------|-------|----|-------------|
| Snap | `snap/snapcraft.yaml` | — | `tools/release-snap.sh` |
| Flatpak | `packaging/flatpak/` | ✅ attaches `whatly.flatpak` | `packaging/flatpak/build-flatpak.sh` |
| AppImage + zsync | `packaging/appimage/` | ✅ attaches `.AppImage` + `.zsync` | `packaging/appimage/build-appimage.sh` |
| Debian `.deb` | `debian/` | — | `dpkg-buildpackage -b -us -uc` |
| Fedora / COPR | `packaging/rpm/whatly.spec` | — | `rpmbuild` / COPR |
| openSUSE Tumbleweed | `packaging/obs/` | ✅ attaches native `.rpm` | `rpmbuild` / OBS |
| Gentoo | `packaging/gentoo/` | — | `emerge` (overlay) |

## Two shared gotchas

- **Qt version.** Whatly's CMake floor is 6.10, but the real API requirement is
  6.8 (`QWebEnginePermission`). The floor is overridable with
  `-DQT_VERSION_MINOR=9` for packaging environments pinned to a slightly older
  Qt — the Flatpak build uses this to target the KDE 6.9 runtime. Don't go below
  8.
- **The `libnotify-qt` submodule** is not in a plain `git archive` / GitHub
  source tarball. Build from a checkout with submodules initialised
  (`git submodule update --init`), or from a tarball that bundles them. The
  Flatpak/AppImage CI checks out with `submodules: recursive`.

## Flatpak

```bash
packaging/flatpak/build-flatpak.sh            # build + install the local tree
packaging/flatpak/build-flatpak.sh --release  # build the tag + a whatly.flatpak bundle
```

The script installs `flatpak-builder`, the flathub remote and the KDE 6.9
runtime + QtWebEngine BaseApp, then builds. The manifest lowers the Qt floor to
6.9 to match that runtime (see the note above); if a KDE runtime for Qt 6.10 is
available, bump the three runtime versions in the manifest and drop the
`-DQT_VERSION_MINOR=9` config-opt.

## AppImage

```bash
packaging/appimage/build-appimage.sh
```

Downloads `linuxdeploy` + the Qt plugin, bundles Qt WebEngine and bakes in
`.zsync` update information pointing at the GitHub releases, so AppImageUpdate
pulls only changed chunks. Build on the oldest glibc you want to support (the CI
uses ubuntu-22.04). Needs a Qt ≥ 6.9 in the environment.

## Debian `.deb`

Proper `debhelper` + CMake packaging in `debian/`. On a host with the build-deps
(Qt 6.10 dev packages — `dpkg-checkbuilddeps` passes on the dev host):

```bash
git submodule update --init
dpkg-buildpackage -b -us -uc      # -> ../whatly_6.0.0_amd64.deb
sudo apt install ../whatly_6.0.0_amd64.deb
```

Not built in CI: the ubuntu runners ship Qt 6.4, too old for the system build.

## Fedora / COPR

```bash
rpmbuild -ba packaging/rpm/whatly.spec      # local
# or add the spec + a submodule-bundled tarball to a COPR project and let it build
```

## openSUSE Tumbleweed

Tumbleweed ships Qt 6.10+, so it builds against the distro Qt with no bundling.
The Release Artifacts workflow builds a native `.rpm` in an
`opensuse/tumbleweed` container and attaches it to each release. To publish it
continuously from your own Open Build Service project, use the spec + source
service in `packaging/obs/` (see that directory's README). Leap is not
supported (its Qt WebEngine is too old).

## Gentoo

An overlay with the `net-im/whatly` ebuild (versioned + `-9999` live) lives in
`packaging/gentoo/`. It builds from source against the system Qt and can enable
the proprietary H.264/AAC codecs via `dev-qt/qtwebengine[proprietary-codecs]`.
See that directory's README for adding it as a local overlay or submitting to
GURU.
