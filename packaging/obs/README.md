# openSUSE / Open Build Service package

This directory holds the files to build Whatly on the
[Open Build Service](https://build.opensuse.org/) (OBS) and ship a native rpm
to openSUSE users.

- `whatly.spec` — the openSUSE spec (Tumbleweed; needs Qt 6.10+).
- `_service` — an OBS source service that clones the tagged source **with git
  submodules**, so the bundled `libnotify-qt` is included (the plain GitHub
  archive omits it).

> Leap is not supported: its Qt WebEngine is too old for current WhatsApp Web.

## One-time setup (your OBS home project)

```bash
# Install the OBS client and log in once:
sudo zypper install osc
osc login          # uses your build.opensuse.org account

# Create/checkout a package in your home project:
osc checkout home:YOURNAME
cd home:YOURNAME
osc mkpac whatly
cd whatly

# Copy these two files in:
cp /path/to/whatly/packaging/obs/whatly.spec .
cp /path/to/whatly/packaging/obs/_service .

# Fetch the source tarball (with submodules) via the service, then commit:
osc service manualrun
osc addremove
osc commit -m "whatly 7.3.1"
```

OBS then builds the rpm for the repositories you enable on the package
(add openSUSE Tumbleweed under the project's "Repositories"). Users install with:

```bash
sudo zypper addrepo https://download.opensuse.org/repositories/home:/YOURNAME/openSUSE_Tumbleweed/home:YOURNAME.repo
sudo zypper refresh
sudo zypper install whatly
```

## Bumping the version

1. Edit `Version:` in `whatly.spec`.
2. Edit `<revision>` in `_service` to the new tag (e.g. `v6.9.0`).
3. `osc service manualrun && osc addremove && osc commit -m "whatly 6.9.0"`.

## MP4 / H.264 video

The OBS build uses openSUSE's Qt WebEngine, which does not include the
proprietary H.264/AAC codecs, so sending MP4 video will not work; photos and
WebM/VP9 video do. See [`../../docs/MEDIA_CODECS.md`](../../docs/MEDIA_CODECS.md)
for why and for how to get codec support with a local build.

> Status: this spec/service is provided ready-to-use but has not yet been proofed
> on a live OBS instance; expect to iterate on package names during the first
> build.
