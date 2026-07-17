cmake_minimum_required(VERSION 3.24)
project(whatly LANGUAGES CXX)

# Set the C++ standard
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Set version
set(PROJECT_VERSION 6.0.0)
set(PROJECT_DESCRIPTION "Feature rich WhatsApp web client based on Qt WebEngine")

# Get Git information
execute_process(
    COMMAND git rev-parse --short HEAD
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    OUTPUT_VARIABLE GIT_HASH
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET
)

execute_process(
    COMMAND git rev-parse --abbrev-ref HEAD
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    OUTPUT_VARIABLE GIT_BRANCH
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET
)

string(TIMESTAMP BUILD_TIMESTAMP "%Y-%m-%d %H:%M:%S")

# Set Qt version - Require Qt 6.10 minimum by default.
#
# The floor is overridable (-DQT_VERSION_MINOR=9) so a packaging environment
# pinned to a slightly older Qt — notably the Flatpak KDE runtime — can still
# build. 6.8 is the true API floor (see the note below); 6.9 is what the Flatpak
# manifest targets. Do not lower it past 8.
set(QT_VERSION_MAJOR 6)
if (NOT DEFINED QT_VERSION_MINOR)
    set(QT_VERSION_MINOR 10)
endif()

# Set CMake policies
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

# Find required packages.
# Qt ${QT_VERSION_MAJOR}.${QT_VERSION_MINOR} is a hard floor for two independent reasons, so it cannot simply
# be lowered to match an older distro Qt:
#   * the code uses QWebEnginePermission / QWebEnginePage::permissionRequested,
#     which only exist from Qt 6.8;
#   * the bundled Chromium must be recent enough that WhatsApp Web does not
#     reject the client as an outdated browser (the User-Agent is derived from
#     it, so it always reports the truth).
# Several distros (Debian 13, Ubuntu 24.04, Linux Mint 22.x) still ship Qt 6.4,
# which is why this check fails for them — hence the explicit guidance below.
find_package(Qt${QT_VERSION_MAJOR} ${QT_VERSION_MAJOR}.${QT_VERSION_MINOR} QUIET COMPONENTS
    Core
    Gui
    Widgets
    WebEngineWidgets
    WebChannel
    Positioning
    Network
    Svg
)

if (NOT Qt${QT_VERSION_MAJOR}_FOUND)
    set(_qt_found_note "")
    if (Qt${QT_VERSION_MAJOR}_VERSION)
        set(_qt_found_note " (found ${Qt${QT_VERSION_MAJOR}_VERSION})")
    endif()
    message(FATAL_ERROR
        "Qt ${QT_VERSION_MAJOR}.${QT_VERSION_MINOR} or newer is required${_qt_found_note}.\n"
        "Many distributions still package Qt 6.4, which is too old: the code needs "
        "QWebEnginePermission (Qt 6.8+), and WhatsApp Web refuses to load in the "
        "older Chromium those builds bundle.\n"
        "Install a newer Qt (for example with the official Qt online installer, "
        "selecting the Qt WebEngine and Qt Positioning modules) and point CMake at "
        "it:\n"
        "    cmake -S . -B build -DCMAKE_PREFIX_PATH=/path/to/Qt/6.10.0/gcc_64")
endif()

# Optional: without the Qt Linguist tools the .ts files cannot be compiled and
# the interface simply stays English, so a missing one must not fail the build.
# This is searched after the check above because a failed component search
# resets Qt${QT_VERSION_MAJOR}_FOUND.
find_package(Qt${QT_VERSION_MAJOR} QUIET COMPONENTS LinguistTools)

# libnotify-qt is a D-Bus (org.freedesktop.Notifications) client and is only
# used on Linux; other platforms fall back to QSystemTrayIcon notifications.
if (UNIX AND NOT APPLE)
    find_package(notify-qt${QT_VERSION_MAJOR} QUIET CONFIG)
    if (notify-qt${QT_VERSION_MAJOR}_FOUND)
        add_library(notify-qt ALIAS notify-qt${QT_VERSION_MAJOR})
    elseif (EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/src/libnotify-qt/CMakeLists.txt")
        # The bundled submodule declares cmake_minimum_required(VERSION 4.0), so
        # building it needs a newer CMake than this project itself does. Say so
        # here instead of letting the error surface from inside the submodule,
        # and point out that a system notify-qt package avoids it entirely.
        if (CMAKE_VERSION VERSION_LESS 4.0)
            message(FATAL_ERROR
                "The bundled src/libnotify-qt submodule requires CMake 4.0 or newer "
                "(you have ${CMAKE_VERSION}).\n"
                "Either install the notify-qt${QT_VERSION_MAJOR} package from your distribution — the "
                "submodule is then not built and CMake ${CMAKE_MINIMUM_REQUIRED_VERSION} is enough — "
                "or upgrade CMake.")
        endif()
        add_subdirectory(src/libnotify-qt)
    else()
        message(FATAL_ERROR
            "notify-qt${QT_VERSION_MAJOR} package not found and the src/libnotify-qt submodule "
            "is missing.\n"
            "Please either install notify-qt${QT_VERSION_MAJOR} "
            "or initialize/update the src/libnotify-qt git submodule.")
    endif()
endif()

# The extra hardening flags are opt-out. A distribution builds with its own
# hardening policy and its own optimisation flags, and forcing these on top
# fights that — which is exactly what a Gentoo maintainer reported. Packagers
# can pass -DWHATLY_HARDENING=OFF to leave the toolchain's own flags in place;
# for a normal source build they stay on.
option(WHATLY_HARDENING "Add stack-protector/FORTIFY hardening compile flags" ON)

# Compiler flags
if(MSVC)
    # Source files contain UTF-8 characters; MSVC needs to be told explicitly.
    add_compile_options(/utf-8)
else()
    if(WHATLY_HARDENING)
        add_compile_options(
            -fstack-protector-strong
            -fstack-clash-protection
            -D_FORTIFY_SOURCE=3
            -D_GLIBCXX_ASSERTIONS
        )

        # Architecture-specific hardening.
        if(CMAKE_SYSTEM_PROCESSOR MATCHES "aarch64")
            add_compile_options(-mbranch-protection=standard)
        endif()
    endif()

    # Release and Debug specific flags
    add_compile_options($<$<CONFIG:Release>:-O2>)
    add_compile_options($<$<CONFIG:Debug>:-O1>)
endif()

add_compile_options($<$<CONFIG:Release>:-DQT_NO_DEBUG_OUTPUT>)

# Installation prefix
if(NOT CMAKE_INSTALL_PREFIX)
    set(CMAKE_INSTALL_PREFIX /usr)
endif()

set(BINDIR ${CMAKE_INSTALL_PREFIX}/bin)
set(DATADIR ${CMAKE_INSTALL_PREFIX}/share)

# Main application source files
set(SOURCES
    src/about.cpp
    src/appprofile.cpp
    src/automatictheme.cpp
    src/common.cpp
    src/debuglog.cpp
    src/downloadmanagerwidget.cpp
    src/downloadwidget.cpp
    src/linkeddevicename.cpp
    src/lock.cpp
    src/main.cpp
    src/identicons.cpp
    src/mainwindow.cpp
    src/mainwindow_accounts.cpp
    src/mainwindow_lock.cpp
    src/mainwindow_tray.cpp
    src/mainwindow_webengine.cpp
    src/permissiondialog.cpp
    src/rateapp.cpp
    src/settingswidget.cpp
    src/sunclock.cpp
    src/theme.cpp
    src/utils.cpp
    src/webenginepage.cpp
    src/webenginenotifproxy.cpp
    src/webengineprofilemanager.cpp
    src/chattheme.cpp
    src/dictionaries.cpp
    src/chatwallpaper.cpp
    src/customcss.cpp
    src/globalshortcut.cpp
    src/privacyblur.cpp
    src/webfont.cpp
    src/mutedstatus.cpp
    src/scheduledmessages.cpp
    src/scheduledmessagesdialog.cpp
    src/webtweaks.cpp
    src/webview.cpp
    src/widgets/elidedlabel/elidedlabel.cpp
    src/widgets/scrolltext/scrolltext.cpp
    src/widgets/MoreApps/moreapps.cpp
)

set(HEADERS
    src/about.h
    src/appprofile.h
    src/autolockeventfilter.h
    src/automatictheme.h
    src/common.h
    src/debuglog.h
    src/def.h
    src/downloadmanagerwidget.h
    src/downloadwidget.h
    src/linkeddevicename.h
    src/lock.h
    src/identicons.h
    src/mainwindow.h
    src/notificationpopup.h
    src/permissiondialog.h
    src/rateapp.h
    src/requestinterceptor.h
    src/settingsmanager.h
    src/settingswidget.h
    src/sunclock.hpp
    src/theme.h
    src/utils.h
    src/webenginepage.h
    src/webengineprofilemanager.h
    src/chattheme.h
    src/dictionaries.h
    src/chatwallpaper.h
    src/customcss.h
    src/privacyblur.h
    src/webfont.h
    src/mutedstatus.h
    src/scheduledmessages.h
    src/scheduledmessagesdialog.h
    src/pagebridge.h
    src/webtweaks.h
    src/webview.h
    src/webenginenotifproxy.h
    src/widgets/elidedlabel/elidedlabel.h
    src/widgets/scrolltext/scrolltext.h
    src/widgets/MoreApps/moreapps.h
)

set(UI_FILES
    src/about.ui
    src/automatictheme.ui
    src/certificateerrordialog.ui
    src/downloadmanagerwidget.ui
    src/downloadwidget.ui
    src/lock.ui
    src/passworddialog.ui
    src/permissiondialog.ui
    src/rateapp.ui
    src/settingswidget.ui
    src/widgets/MoreApps/moreapps.ui
)

set(RESOURCES
    src/icons.qrc
)

# SingleApplication library
set(SINGLEAPP_SOURCES
    src/singleapplication/singleapplication.cpp
    src/singleapplication/singleapplication_p.cpp
)

set(SINGLEAPP_HEADERS
    src/singleapplication/SingleApplication
    src/singleapplication/singleapplication.h
    src/singleapplication/singleapplication_p.h
)

# Create the main executable
add_executable(whatly
    ${SOURCES}
    ${HEADERS}
    ${UI_FILES}
    ${RESOURCES}
    ${SINGLEAPP_SOURCES}
    ${SINGLEAPP_HEADERS}
)

# Translations. The .ts files existed but were never compiled or loaded, so the
# tr() strings always fell back to English and the Italian translation shipped
# dead. Compile them to .qm and embed them, so main.cpp can load one for the
# system locale.
if (Qt${QT_VERSION_MAJOR}LinguistTools_FOUND)
    qt_add_translations(whatly
        TS_FILES
            src/i18n/ar_SA.ts
            src/i18n/de_DE.ts
            src/i18n/es_ES.ts
            src/i18n/fr_FR.ts
            src/i18n/hi_IN.ts
            src/i18n/id_ID.ts
            src/i18n/it_IT.ts
            src/i18n/ja_JP.ts
            src/i18n/ko_KR.ts
            src/i18n/nl_NL.ts
            src/i18n/pl_PL.ts
            src/i18n/pt_BR.ts
            src/i18n/ru_RU.ts
            src/i18n/tr_TR.ts
            src/i18n/zh_CN.ts
        RESOURCE_PREFIX "/i18n"
    )
else()
    message(WARNING
        "Qt Linguist tools not found: translations will not be built and the "
        "interface will be English only. Install them (qt6-tools-dev on Debian "
        "and Ubuntu) to include the translations.")
endif()

# Include directories
target_include_directories(whatly PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/src/singleapplication
    ${CMAKE_CURRENT_SOURCE_DIR}/src/widgets/elidedlabel
    ${CMAKE_CURRENT_SOURCE_DIR}/src/widgets/scrolltext
    ${CMAKE_CURRENT_SOURCE_DIR}/src/widgets/MoreApps
    ${CMAKE_CURRENT_BINARY_DIR}
)

# Link libraries
target_link_libraries(whatly PRIVATE
    Qt${QT_VERSION_MAJOR}::Core
    Qt${QT_VERSION_MAJOR}::Gui
    Qt${QT_VERSION_MAJOR}::Widgets
    Qt${QT_VERSION_MAJOR}::WebEngineWidgets
    Qt${QT_VERSION_MAJOR}::WebChannel
    Qt${QT_VERSION_MAJOR}::Positioning
    Qt${QT_VERSION_MAJOR}::Network
    Qt${QT_VERSION_MAJOR}::Svg
)

# Linux-only libraries: D-Bus notifications and X11 (Caps Lock detection, and
# the X11-only global raise-window shortcut in globalshortcut.cpp — xcb is used
# to read the key events Qt delivers on the xcb platform).
# Qt DBus is needed directly to build the freedesktop "image-data" notification
# hint with the byte order the spec requires (see MainWindow::notificationImageHint).
if(UNIX AND NOT APPLE)
    find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS DBus)
    target_link_libraries(whatly PRIVATE
        notify-qt
        X11
        xcb
        Qt${QT_VERSION_MAJOR}::DBus
    )
endif()

# Windows executable setup: GUI subsystem (no console) and icon/version resource
if(WIN32)
    string(REPLACE "." "," PROJECT_VERSION_RC ${PROJECT_VERSION})
    configure_file(dist/windows/whatly.rc.in ${CMAKE_CURRENT_BINARY_DIR}/whatly.rc @ONLY)
    target_sources(whatly PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/whatly.rc)
    set_target_properties(whatly PROPERTIES WIN32_EXECUTABLE TRUE)
endif()

# Define compiler definitions
target_compile_definitions(whatly PRIVATE
    QAPPLICATION_CLASS=QApplication
    QT_DEPRECATED_WARNINGS
    GIT_HASH="${GIT_HASH}"
    GIT_BRANCH="${GIT_BRANCH}"
    BUILD_TIMESTAMP="${BUILD_TIMESTAMP}"
    VERSIONSTR="${PROJECT_VERSION}"
)

# Check for WebEngine spellchecker support
# Note: This check is simplified; you may need to enhance it based on your build environment
message(STATUS "Qt6 WebEngine spellchecker support: enabled (required)")

# Install the executable
install(TARGETS whatly DESTINATION ${BINDIR})

# Linux-only installation files (freedesktop.org conventions)
if(UNIX AND NOT APPLE)

# Install desktop file
install(FILES dist/linux/net.shakaran.whatly.desktop
    DESTINATION ${DATADIR}/applications
)

# Install appdata file
install(FILES dist/linux/net.shakaran.whatly.appdata.xml
    DESTINATION ${DATADIR}/metainfo
)

# Install license
install(FILES LICENSE
    DESTINATION ${DATADIR}/licenses/whatly
)

# Install icons
install(FILES dist/linux/hicolor/16x16/apps/net.shakaran.whatly.png
    DESTINATION ${DATADIR}/icons/hicolor/16x16/apps
)
install(FILES dist/linux/hicolor/32x32/apps/net.shakaran.whatly.png
    DESTINATION ${DATADIR}/icons/hicolor/32x32/apps
)
install(FILES dist/linux/hicolor/64x64/apps/net.shakaran.whatly.png
    DESTINATION ${DATADIR}/icons/hicolor/64x64/apps
)
install(FILES dist/linux/hicolor/128x128/apps/net.shakaran.whatly.png
    DESTINATION ${DATADIR}/icons/hicolor/128x128/apps
)
install(FILES dist/linux/hicolor/256x256/apps/net.shakaran.whatly.png
    DESTINATION ${DATADIR}/icons/hicolor/256x256/apps
)
install(FILES dist/linux/hicolor/scalable/apps/net.shakaran.whatly.svg
    DESTINATION ${DATADIR}/icons/hicolor/scalable/apps
)
install(FILES dist/linux/hicolor/symbolic/apps/net.shakaran.whatly-symbolic.svg
    DESTINATION ${DATADIR}/icons/hicolor/symbolic/apps
)

endif()

# ── Spell-check dictionaries ──────────────────────────────────────────────────
#
# Qt WebEngine's spell checker is Chromium's, and Chromium reads its own .bdic
# format — it cannot read hunspell's .dic/.aff at all. Pointing it at the system
# hunspell directory therefore finds nothing, which is exactly why the language
# dropdown was always empty and no word was ever underlined.
#
# The hunspell sources in src/dictionaries are converted here, once, with the
# tool Qt ships for it, and installed as .bdic. QTWEBENGINE_DICTIONARIES_PATH is
# pointed at the result at startup (see main.cpp).
option(WHATLY_SPELLCHECK "Build the spell-check dictionaries" ON)

if(WHATLY_SPELLCHECK)

find_program(QWEBENGINE_CONVERT_DICT
    NAMES qwebengine_convert_dict
    HINTS "${QT6_INSTALL_PREFIX}/libexec" /usr/lib/qt6/libexec
          /usr/lib/x86_64-linux-gnu/qt6/libexec /usr/lib64/qt6/libexec
)

file(GLOB WHATLY_HUNSPELL_DICTS "${CMAKE_CURRENT_SOURCE_DIR}/src/dictionaries/*.dic")

if(NOT QWEBENGINE_CONVERT_DICT)
    # Not a warning to be ignored: without this tool there are no dictionaries,
    # and the spell checker silently does nothing. That is how it shipped broken.
    message(FATAL_ERROR
        "qwebengine_convert_dict was not found.\n"
        "It converts the hunspell dictionaries into the .bdic format Qt WebEngine "
        "requires; without it the spell checker cannot work at all.\n"
        "It ships with Qt WebEngine's dev tools:\n"
        "  Debian/Ubuntu:  sudo apt install qt6-webengine-dev-tools\n"
        "  Fedora:         sudo dnf install qt6-qtwebengine-devel\n"
        "  Arch:           sudo pacman -S qt6-webengine\n"
        "Or pass its location explicitly: -DQWEBENGINE_CONVERT_DICT=/path/to/qwebengine_convert_dict\n"
        "To build without a spell checker instead, pass -DWHATLY_SPELLCHECK=OFF."
    )
endif()

set(WHATLY_DICT_DIR "${CMAKE_CURRENT_BINARY_DIR}/qtwebengine_dictionaries")
file(MAKE_DIRECTORY "${WHATLY_DICT_DIR}")

set(WHATLY_BDICS "")
foreach(dict ${WHATLY_HUNSPELL_DICTS})
    get_filename_component(dict_name "${dict}" NAME_WE)
    set(bdic "${WHATLY_DICT_DIR}/${dict_name}.bdic")
    add_custom_command(
        OUTPUT "${bdic}"
        COMMAND "${QWEBENGINE_CONVERT_DICT}" "${dict}" "${bdic}"
        DEPENDS "${dict}" "${CMAKE_CURRENT_SOURCE_DIR}/src/dictionaries/${dict_name}.aff"
        COMMENT "Converting dictionary ${dict_name} to .bdic"
        VERBATIM
    )
    list(APPEND WHATLY_BDICS "${bdic}")
endforeach()

add_custom_target(whatly_dictionaries ALL DEPENDS ${WHATLY_BDICS})
add_dependencies(whatly whatly_dictionaries)

list(LENGTH WHATLY_BDICS WHATLY_DICT_COUNT)

if(UNIX AND NOT APPLE)
    install(FILES ${WHATLY_BDICS}
        DESTINATION ${DATADIR}/whatly/qtwebengine_dictionaries
    )
endif()

else()
    set(WHATLY_DICT_COUNT 0)
    target_compile_definitions(whatly PRIVATE WHATLY_NO_SPELLCHECK)
endif()

# Summary message
message(STATUS "")
message(STATUS "========== Whatly Build Configuration ==========")
message(STATUS "Version:                  ${PROJECT_VERSION}")
message(STATUS "Install prefix:           ${CMAKE_INSTALL_PREFIX}")
message(STATUS "Build type:               ${CMAKE_BUILD_TYPE}")
message(STATUS "Git Hash:                 ${GIT_HASH}")
message(STATUS "Git Branch:               ${GIT_BRANCH}")
message(STATUS "Build Timestamp:          ${BUILD_TIMESTAMP}")
message(STATUS "Flatpak Build:            ${FLATPAK_BUILD}")
message(STATUS "================================================")
message(STATUS "")

