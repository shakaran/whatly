#
# spec file for package whatly (openSUSE / Open Build Service)
#
# This is tuned for openSUSE Tumbleweed, which ships Qt 6.10+. Leap does not
# have a new enough Qt WebEngine and is not supported. The Fedora/COPR spec
# lives in packaging/rpm/whatly.spec; keep both in sync when bumping versions.
#

Name:           whatly
Version:        7.0.0
Release:        0
Summary:        Feature-rich WhatsApp Web client based on Qt WebEngine
License:        MIT
URL:            https://github.com/shakaran/whatly
# Produced by the accompanying _service file (obs_scm with submodules=enable),
# so the bundled libnotify-qt submodule is present in the tarball.
Source0:        %{name}-%{version}.tar.gz

BuildRequires:  cmake >= 3.24
BuildRequires:  ninja
BuildRequires:  gcc-c++
BuildRequires:  cmake(Qt6Core) >= 6.10.0
BuildRequires:  cmake(Qt6Widgets)
BuildRequires:  cmake(Qt6WebEngineWidgets)
BuildRequires:  cmake(Qt6WebChannel)
BuildRequires:  cmake(Qt6Positioning)
BuildRequires:  cmake(Qt6DBus)
BuildRequires:  cmake(Qt6Svg)
BuildRequires:  cmake(Qt6LinguistTools)
BuildRequires:  pkgconfig(x11)
BuildRequires:  pkgconfig(xcb)

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
%{_datadir}/metainfo/net.shakaran.whatly.appdata.xml
%{_datadir}/whatly/

%changelog
