# Copyright 2026 Ángel Guzmán Maeso
# Distributed under the terms of the MIT License

EAPI=8

inherit cmake xdg

DESCRIPTION="Feature-rich WhatsApp Web desktop client (Qt WebEngine)"
HOMEPAGE="https://github.com/shakaran/whatly"

if [[ ${PV} == 9999 ]]; then
	inherit git-r3
	EGIT_REPO_URI="https://github.com/shakaran/whatly.git"
	# whatly bundles libnotify-qt as a git submodule and builds it when a
	# system notify-qt6 is absent (the usual case on Gentoo), so pull it.
	EGIT_SUBMODULES=( '*' )
else
	# NOTE: the plain GitHub archive does NOT include the libnotify-qt
	# submodule. Use a tarball that bundles submodules (see this overlay's
	# README) or provide a system notify-qt6.
	SRC_URI="https://github.com/shakaran/whatly/archive/refs/tags/v${PV}.tar.gz -> ${P}.tar.gz"
	KEYWORDS="~amd64"
fi

LICENSE="MIT"
SLOT="0"

# whatly needs Qt 6.10+: it uses QWebEnginePermission (6.8+) and WhatsApp Web
# rejects the older Chromium in earlier Qt WebEngine.
RDEPEND="
	>=dev-qt/qtbase-6.10:6[dbus,gui,network,widgets]
	>=dev-qt/qtwebengine-6.10:6[widgets]
	>=dev-qt/qtwebchannel-6.10:6
	>=dev-qt/qtpositioning-6.10:6
	>=dev-qt/qtsvg-6.10:6
	x11-libs/libX11
	x11-libs/libxcb
"
DEPEND="${RDEPEND}
	>=dev-qt/qttools-6.10:6[linguist]
"
BDEPEND="
	>=dev-build/cmake-3.24
	dev-build/ninja
"

src_configure() {
	local mycmakeargs=(
		-DCMAKE_BUILD_TYPE=Release
	)
	cmake_src_configure
}

pkg_postinst() {
	xdg_pkg_postinst
	elog "Sending MP4 (H.264) video needs the proprietary codecs in Qt WebEngine."
	elog "Enable them on your system with:"
	elog "    dev-qt/qtwebengine proprietary-codecs"
	elog "and rebuild dev-qt/qtwebengine. Photos and WebM/VP9 video work without it."
}
