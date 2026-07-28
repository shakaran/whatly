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

CI does it. `.github/workflows/aur.yml` build-tests the source package in an
Arch container on every change here, and on each GitHub release it stamps the
version (and the AppImage checksum for `whatly-bin`), regenerates `.SRCINFO`, and
pushes both packages to the AUR.

That last step is a no-op until the AUR is set up once, by the maintainer only
(it needs a credential the CI cannot create):

1. Create an AUR account at https://aur.archlinux.org/.
2. Generate an SSH key (`ssh-keygen -t ed25519 -C aur`) and add the **public**
   key to the AUR account (My Account → SSH Public Key).
3. Add the **private** key as the GitHub repo secret **`AUR_SSH_PRIVATE_KEY`**
   (Settings → Secrets and variables → Actions). Never commit it.

The first successful push creates the packages on the AUR under that account.
After that, releases publish automatically.

### Manual fallback

```sh
git clone ssh://aur@aur.archlinux.org/whatly.git aur-whatly
cp whatly/PKGBUILD whatly/.SRCINFO aur-whatly/
cd aur-whatly && git add PKGBUILD .SRCINFO && git commit -m "whatly 6.7.2" && git push
```
