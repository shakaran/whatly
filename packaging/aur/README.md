# AUR packaging

PKGBUILDs for the [Arch User Repository](https://aur.archlinux.org/) (issue #30).

- **`whatly/`** — builds from the tagged release source with CMake/Qt 6 (pulls
  the `libnotify-qt` submodule over git). The idiomatic source package.
- **`whatly-bin/`** — repackages the prebuilt release AppImage (bundled Qt) under
  `/opt/whatly`, no compilation. Faster to install; `sha256sums` is pinned per
  release.

They **conflict** with each other and with the old community `whatsie-git`
package is unrelated (different upstream name).

## Validate before publishing

These have not been built on Arch here. On an Arch box:

```sh
cd whatly        # or whatly-bin
makepkg -si      # build + install, check it runs
makepkg --printsrcinfo > .SRCINFO   # regenerate if PKGBUILD changed
namcap PKGBUILD  # lint
```

## Publishing

CI handles it. `.github/workflows/aur.yml` build-tests the source package in an
Arch container on every change here, and on each GitHub release it stamps the
version (and the AppImage checksum for `whatly-bin`), regenerates `.SRCINFO`, and
pushes both packages to the AUR. The push authenticates with the
`AUR_SSH_PRIVATE_KEY` repo secret.

### Manual fallback

```sh
git clone ssh://aur@aur.archlinux.org/whatly.git aur-whatly
cp whatly/PKGBUILD whatly/.SRCINFO aur-whatly/
cd aur-whatly && git add PKGBUILD .SRCINFO && git commit -m "whatly 6.7.2" && git push
```
