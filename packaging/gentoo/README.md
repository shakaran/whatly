# Gentoo overlay

This directory is a small Gentoo overlay providing the `net-im/whatly` ebuild.
Gentoo installs from source, so there is no binary release: this is a recipe
Portage uses to build Whatly against your system Qt.

## Use it as a local overlay

```bash
# Point eselect-repository at this directory (or clone the repo and add it):
eselect repository add whatly-overlay git https://github.com/shakaran/whatly.git
# then symlink / configure it to use packaging/gentoo, or simply:
sudo mkdir -p /etc/portage/repos.conf
cat <<'EOF' | sudo tee /etc/portage/repos.conf/whatly.conf
[whatly]
location = /var/db/repos/whatly
sync-type = git
sync-uri = https://github.com/shakaran/whatly.git
EOF
```

Because the overlay lives under `packaging/gentoo/` in the main repo rather than
at the repo root, the simplest route is to copy that subtree into a repo whose
root is the overlay (or maintain a dedicated overlay repo). Then:

```bash
sudo emerge --ask net-im/whatly        # latest tagged version
# or the live build straight from git (pulls the libnotify-qt submodule):
sudo emerge --ask =net-im/whatly-9999
```

## MP4 / H.264 video

Sending MP4 video needs the proprietary codecs in Qt WebEngine. Enable them:

```bash
echo 'dev-qt/qtwebengine proprietary-codecs' | sudo tee -a /etc/portage/package.use/whatly
sudo emerge --ask --changed-use dev-qt/qtwebengine
```

Photos and WebM/VP9 video work without it. See [`../../docs/MEDIA_CODECS.md`](../../docs/MEDIA_CODECS.md).

## Submodule note

The plain GitHub source tarball does **not** include the bundled `libnotify-qt`
git submodule, which the build needs when no system `notify-qt6` is present. The
`-9999` (live) ebuild pulls submodules via `git-r3`, so it always works. For the
versioned ebuild, either build `-9999`, or provide a release tarball that bundles
submodules (produced with `git submodule update --init` then `git archive`).

## Publishing

The maintained way to share this with other Gentoo users is to submit
`net-im/whatly` to [GURU](https://wiki.gentoo.org/wiki/Project:GURU) (the Gentoo
user repository) or to host it as a dedicated overlay. This directory is the
ready-to-adapt source for either.

> Status: this ebuild is provided best-effort and has not yet been proofed on a
> live Gentoo box; test with `pkgcheck`/`repoman` before publishing.
