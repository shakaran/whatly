Name:           whatly
Version:        6.8.3
Release:        1%{?dist}
Summary:        Feature-rich WhatsApp Web client based on Qt WebEngine

License:        MIT
URL:            https://github.com/shakaran/whatly
# The GitHub archive does NOT include the libnotify-qt submodule. For COPR,
# point Source0 at a tarball that bundles submodules (e.g. produced by
# `git archive` after `git submodule update --init`, or a release asset), or
# ensure a system notify-qt6 is available so CMake skips the submodule.
Source0:        %{url}/archive/v%{version}/%{name}-%{version}.tar.gz

BuildRequires:  cmake >= 3.24
BuildRequires:  ninja-build
BuildRequires:  gcc-c++
BuildRequires:  cmake(Qt6Core)
BuildRequires:  cmake(Qt6Widgets)
BuildRequires:  cmake(Qt6WebEngineWidgets)
BuildRequires:  cmake(Qt6WebChannel)
BuildRequires:  cmake(Qt6Positioning)
BuildRequires:  cmake(Qt6DBus)
BuildRequires:  cmake(Qt6Svg)
BuildRequires:  qt6-qttools-devel
BuildRequires:  qt6-qtwebengine-devel
BuildRequires:  libX11-devel
BuildRequires:  libxcb-devel
Requires:       qt6-qtwebengine

%description
Whatly gives WhatsApp Web a native desktop window with system-tray integration,
desktop notifications, chat themes, a privacy blur, an app lock, a multi-language
spell checker and multiple accounts. It is an MIT-licensed fork of WhatSie and is
not affiliated with WhatsApp or Meta.

%prep
%autosetup -n %{name}-%{version}

%build
%cmake -GNinja -DCMAKE_BUILD_TYPE=Release
%cmake_build

%install
%cmake_install

%files
%license LICENSE
%doc README.md CHANGELOG.md
%{_bindir}/whatly
%{_datadir}/applications/net.shakaran.whatly.desktop
%{_datadir}/icons/hicolor/*/apps/net.shakaran.whatly.*
%{_metainfodir}/net.shakaran.whatly.appdata.xml
%{_datadir}/whatly/

%changelog
* Fri Jul 31 2026 Ángel Guzmán Maeso <angel@guzmanmaeso.com> - 6.8.3-1
- Drag and drop reads real paths directly and only uses the FileTransfer portal
  in the Flatpak sandbox (#34); bundle the Qt Wayland platform plugin so the
  AppImage/.deb run natively on Wayland (#36); no more HD-quality dialog loop on
  sub-HD attachments (#34); chat themes recolour the message-options fade (#35).
  See CHANGELOG.md.

* Thu Jul 30 2026 Ángel Guzmán Maeso <angel@guzmanmaeso.com> - 6.8.2-1
- Add spell-check dictionaries without repacking: Whatly mirrors the bundled
  dictionaries into a writable user folder and picks up any .bdic dropped there
  (#24). See CHANGELOG.md.

* Thu Jul 30 2026 Ángel Guzmán Maeso <angel@guzmanmaeso.com> - 6.8.1-1
- Open WhatsApp group invite links (#186); sound for new-message notifications
  with a "Play sound" toggle (#120); the call "Move to new window" popout opens
  in its own Whatly window and voice/video calls work out of the box; each
  account tab shows the WhatsApp Web version in its tooltip. See CHANGELOG.md.

* Tue Jul 28 2026 Ángel Guzmán Maeso <angel@guzmanmaeso.com> - 6.8.0-1
- Collapse the chat list to a strip of profile pictures (#25); "Restart now"
  button and tray-brings-window-to-front (#27); optional account strip and a
  hide-the-title-bar option (#26); Esperanto as a 16th language (#29); drag and
  drop attachments now work in the Flatpak via the FileTransfer portal (#32); no
  more endless render-crash dialog loop (#28); AUR packaging (#30); see
  CHANGELOG.md
* Tue Jul 28 2026 Ángel Guzmán Maeso <angel@guzmanmaeso.com> - 6.7.2-1
- Drag and drop files onto the window to send them as attachments, any file type
  (#285); see CHANGELOG.md
* Tue Jul 28 2026 Ángel Guzmán Maeso <angel@guzmanmaeso.com> - 6.7.1-1
- Windows releases now ship a proper .msi installer alongside the portable zip;
  softened the monochrome tray icon outline so it is not noticeable on a dark
  panel (#14); see CHANGELOG.md
* Mon Jul 27 2026 Ángel Guzmán Maeso <angel@guzmanmaeso.com> - 6.7.0-1
- Lower idle memory: V8 starts in optimize-for-size mode by default, with a
  Settings toggle and a JS heap limit (#15); monochrome tray icon now applies
  with no unread messages (#14); adding a second account is discoverable
  (account bar + tray "Add account…"); never strand the window when the tray
  icon is hidden (#13); Windows build/start-up fixes (#18, #19); the test suite
  no longer writes into the real settings (#23); see CHANGELOG.md
* Sun Jul 26 2026 Ángel Guzmán Maeso <angel@guzmanmaeso.com> - 6.6.0-1
- Send from the command line and a local HTTP API (text, media, templates; to a
  number or a contact/group by name); Cloud API backend and webhooks; auto-reply
  rules; multi-window account tabs with detachable windows and a grid view (#10);
  zoom buttons in WhatsApp's sidebar; fix a start-up crash on newer distros by
  bundling the NSS PKCS#11 modules (#11, #12); see CHANGELOG.md
* Fri Jul 24 2026 Ángel Guzmán Maeso <angel@guzmanmaeso.com> - 6.5.0-1
- Recover from a start-up GPU/GL crash (safe-rendering fallback + Chromium
  logging), settings reorganised into collapsible sections, emoji skin-tone
  fix, HD photos on receive, on-screen notification popup, consistent download
  paths, Windows tray hide-when-frontmost; see CHANGELOG.md
* Mon Jul 20 2026 Ángel Guzmán Maeso <angel@guzmanmaeso.com> - 6.4.0-1
- Command palette, Do-Not-Disturb + keyword rules, recurring schedules and
  reminders, update checker, storage/shortcut management, profile backup,
  lock-on-screen-lock, screen sharing, focus mode, saved replies; see
  CHANGELOG.md
* Mon Jul 20 2026 Ángel Guzmán Maeso <angel@guzmanmaeso.com> - 6.3.0-1
- Performance/privacy engine controls, network proxy, portal notifications,
  custom JS addons + per-account CSS/JS, grid view, setup wizard, optional
  custom window frame; see CHANGELOG.md
* Sat Jul 19 2026 Ángel Guzmán Maeso <angel@guzmanmaeso.com> - 6.2.1-1
- Automatic-theme crash fix, graceful SIGTERM, quieter terminal, About icons,
  and a unit-test suite; see CHANGELOG.md
* Sat Jul 18 2026 Ángel Guzmán Maeso <angel@guzmanmaeso.com> - 6.2.0-1
- Taskbar unread badge, interface font size, auto-reload on crash, HiDPI scaling,
  dictionary-subset build option, optional SignPath signing; see CHANGELOG.md
* Sat Jul 18 2026 Ángel Guzmán Maeso <angel@guzmanmaeso.com> - 6.1.1-1
- High-resolution native notification icon (issue #2); see CHANGELOG.md
* Thu Jul 16 2026 Ángel Guzmán Maeso <angel@guzmanmaeso.com> - 6.0.0-1
- Initial Whatly package (rebrand of WhatSie); see CHANGELOG.md
