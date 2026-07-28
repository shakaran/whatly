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

## Publish to the AUR

The AUR is a git remote; publishing needs an AUR account with an SSH key
registered (this is a manual, credentialed step):

```sh
git clone ssh://aur@aur.archlinux.org/whatly.git aur-whatly
cp whatly/PKGBUILD whatly/.SRCINFO aur-whatly/
cd aur-whatly && git add PKGBUILD .SRCINFO
git commit -m "whatly 6.7.2" && git push
```

Repeat for `whatly-bin`. On each release, bump `pkgver` (and the AppImage
`sha256sums` for `whatly-bin`), regenerate `.SRCINFO`, and push.
