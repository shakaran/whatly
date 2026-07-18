# Submitting Whatly to Flathub

Flathub hosts apps from a per-app manifest in the `flathub/flathub` repo. The
submission is a **pull request you open** (Flathub requires it to come from the
app owner's account); the manifest to submit is `net.shakaran.whatly.yml` in
this folder. Reference: <https://docs.flathub.org/docs/for-app-authors/submission>.

## 1. Test the build locally first

```bash
flatpak install -y flathub org.kde.Platform//6.9 org.kde.Sdk//6.9 \
    io.qt.qtwebengine.BaseApp//6.9
flatpak-builder --user --install --force-clean --disable-rofiles-fuse \
    build-fh packaging/flathub/net.shakaran.whatly.yml
flatpak run net.shakaran.whatly
```

Then run Flathub's linter (it is what the PR's CI runs):

```bash
flatpak install -y flathub org.flatpak.Builder
flatpak run --command=flatpak-builder-lint org.flatpak.Builder \
    manifest packaging/flathub/net.shakaran.whatly.yml
flatpak run --command=flatpak-builder-lint org.flatpak.Builder \
    builddir build-fh   # after a build with --repo
```

Fix anything it flags before opening the PR.

## 2. Open the submission PR

1. Fork <https://github.com/flathub/flathub> to your account.
2. Create a branch **named exactly `net.shakaran.whatly`** (Flathub keys the
   submission on the app-id branch name).
3. Add `net.shakaran.whatly.yml` (this file's sibling) at the **repo root** of
   that branch.
4. Open a PR from that branch against `flathub/flathub` `master`, with
   **"Allow edits from maintainers"** enabled.
5. The `flathub-infra` bot builds it and comments; address any linter findings.
   Once merged, Whatly gets its own `flathub/net.shakaran.whatly` repo and you
   maintain it there (bump `tag`/`commit` per release).

## 3. Per-release upkeep (after acceptance)

Each new tag: update `tag:` and `commit:` (the app source and, if it moved, the
submodule) in the `flathub/net.shakaran.whatly` repo and open a PR there.
`flatpak-external-data-checker` can automate the bumps.

## Notes / known compliance points

- **Metainfo:** `dist/linux/net.shakaran.whatly.appdata.xml` carries the id,
  name, summary, description, `content_rating`, `launchable`, developer, URLs,
  `project_license`/`metadata_license`, `releases` and `screenshots`. Flathub
  prefers the file be named `…metainfo.xml`; the CMake install can be renamed if
  the linter insists (both are currently accepted).
- **Screenshots** point at `raw.githubusercontent.com/shakaran/whatly/main/…`.
  For long-term stability, consider pinning them to the release tag path.
- **Qt floor** is lowered to 6.9 for the KDE runtime via `-DQT_VERSION_MINOR=9`
  (the code's true requirement is 6.8).
