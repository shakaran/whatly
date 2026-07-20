Name:           whatly
Version:        6.3.0
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
* Mon Jul 20 2026 Ángel Guzmán Maeso <angel@guzmanmaeso.com> - 6.3.0-1
- Performance/privacy engine controls, network proxy, portal notifications,
  custom JS addons + per-account CSS/JS, grid view, setup wizard, ARM64
  AppImage; see CHANGELOG.md
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
