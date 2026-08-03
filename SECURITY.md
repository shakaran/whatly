# Security Policy

Thanks for helping keep Whatly and its users safe. This document explains how to
report a vulnerability and what to expect.

Whatly is an MIT-licensed, unofficial desktop client for WhatsApp Web built on Qt
WebEngine. It is not affiliated with, endorsed by, or connected to WhatsApp or
Meta. Please read the "Scope" section below before reporting: many things that
look like Whatly issues are actually in WhatsApp Web or in upstream Qt/Chromium.

## Supported versions

Whatly ships as a rolling series of releases. Security fixes land on `main` and
go out in the next release, so only the latest release is supported. If you are
on an older build, please update before reporting.

| Version           | Supported          |
| ----------------- | ------------------ |
| Latest release    | :white_check_mark: |
| Any older release | :x:                |

## Reporting a vulnerability

**Please report privately, not in a public issue.**

Preferred: use GitHub's private vulnerability reporting for this repository.

1. Go to the [Security tab](https://github.com/shakaran/whatly/security).
2. Click **Report a vulnerability**.
3. Fill in the advisory form.

This keeps the report private between you and the maintainer until a fix is
ready, and lets us collaborate on the advisory and a CVE if warranted.

Alternative: if you cannot use GitHub's form, email
**angel@guzmanmaeso.com** with the details. If you want to send encrypted mail,
say so and we will arrange a key.

Please do **not** open a normal public issue or pull request for a suspected
vulnerability, and avoid posting proof-of-concept details publicly until a fix
has shipped.

### What to include

The more of this you can provide, the faster we can act:

- The Whatly version and how it was installed (AppImage, `.deb`, RPM, Flatpak,
  snap, AUR, openSUSE/OBS, Gentoo, Windows `.msi`/zip, or a source build).
- Your OS and desktop environment (and Wayland vs X11 on Linux).
- A clear description of the issue and its impact.
- Step-by-step reproduction, and a proof of concept if you have one.
- Any relevant logs, keeping in mind they may contain personal data (see below).

## What to expect

This is a small, volunteer-maintained project, so timelines are best-effort:

- **Acknowledgement:** within about 5 business days.
- **Assessment:** we will confirm the report, ask for anything missing, and give
  you an initial severity assessment.
- **Fix and disclosure:** we aim to fix confirmed, in-scope issues promptly and
  release a patched build. We follow coordinated disclosure: we will agree a
  disclosure date with you and, with your consent, credit you in the advisory
  and release notes. We do not run a paid bug-bounty program.

If a report turns out to be in WhatsApp Web or in upstream Qt/Chromium, we will
tell you and, where relevant, point you to the right project to report it to.

## Scope

**In scope** (things we can fix here):

- The Whatly application code in this repository (C++/Qt), including the App
  Lock, the injected JavaScript, drag-and-drop attachment handling, the local
  send CLI/HTTP API, notification handling, and settings/credential storage on
  disk.
- Our packaging and CI configuration in this repository (workflows, spec/ebuild
  files, bundling).
- Our helper tools under `tools/`.

**Out of scope** (report these to the relevant upstream project instead):

- WhatsApp Web itself and the WhatsApp/Meta services and protocols.
- Vulnerabilities in Qt, Qt WebEngine, or the bundled Chromium/FFmpeg. Report Qt
  issues to the [Qt Project](https://www.qt.io/) and Chromium issues upstream.
  We will rebase onto fixed Qt releases as they become available.
- Third-party GitHub Actions or dependencies (report to their maintainers; we
  will bump the pinned version once fixed).
- Missing proprietary media codecs in the portable builds. This is a known,
  documented licensing limitation, not a vulnerability
  (see [docs/MEDIA_CODECS.md](docs/MEDIA_CODECS.md)).
- Issues that require an already-compromised machine, physical access, or a
  malicious local user with the ability to read the user's own config, unless
  Whatly makes such an attack meaningfully easier than the platform already does.

## A note on your privacy when reporting

Whatly is a messaging client, so logs and screenshots can contain your chats,
contacts, and phone number. Please redact anything personal before sending it,
and never include another person's messages or number without their consent.
