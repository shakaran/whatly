# Unit tests (QtTest). Built only when -DWHATLY_TESTS=ON. Kept lightweight: they
# link just the code under test and its direct dependencies, not the whole app
# (no MainWindow, no WebEngine instances), so they build and run fast and
# headless (offscreen).
find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Test Widgets Gui Svg
             WebEngineCore WebEngineWidgets WebChannel Positioning Network)
if(UNIX AND NOT APPLE)
    find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS DBus)
endif()

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC_SEARCH_PATHS ${CMAKE_SOURCE_DIR}/src)

# Optional gcov instrumentation for coverage reports (see tools/coverage.sh).
option(WHATLY_COVERAGE "Instrument the tests for gcov/gcovr coverage" OFF)
if(WHATLY_COVERAGE)
    add_compile_options(--coverage -O0 -g)
    add_link_options(--coverage)
endif()

set(S ${CMAKE_SOURCE_DIR}/src)

# Shared build-stamp macros the app sources expect.
set(TEST_DEFS VERSIONSTR="test" GIT_HASH="test" GIT_BRANCH="test"
              BUILD_TIMESTAMP="test")

# ── UI-asset test (widgets: About + RateApp) ────────────────────────────────
add_executable(tst_ui_assets
    tst_ui_assets.cpp
    ${S}/about.cpp ${S}/rateapp.cpp ${S}/utils.cpp ${S}/debuglog.cpp
    ${S}/appprofile.cpp ${S}/icons.qrc
)
target_include_directories(tst_ui_assets PRIVATE ${S} ${CMAKE_BINARY_DIR})
target_compile_definitions(tst_ui_assets PRIVATE ${TEST_DEFS})
target_link_libraries(tst_ui_assets PRIVATE
    Qt${QT_VERSION_MAJOR}::Test Qt${QT_VERSION_MAJOR}::Widgets
    Qt${QT_VERSION_MAJOR}::Gui Qt${QT_VERSION_MAJOR}::Svg)
add_test(NAME ui_assets COMMAND tst_ui_assets)
set_tests_properties(ui_assets PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

# ── Logic test (headless helpers, script generators, queue, sun, etc.) ───────
add_executable(tst_logic
    tst_logic.cpp
    ${S}/utils.cpp ${S}/debuglog.cpp ${S}/appprofile.cpp ${S}/common.cpp
    ${S}/sunclock.cpp ${S}/identicons.cpp ${S}/theme.cpp ${S}/dictionaries.cpp
    ${S}/scheduledmessages.cpp
    ${S}/webfont.cpp ${S}/chattheme.cpp ${S}/mutedstatus.cpp ${S}/privacyblur.cpp
    ${S}/chatwallpaper.cpp ${S}/customcss.cpp ${S}/customjs.cpp ${S}/commandpalette.cpp ${S}/updatechecker.cpp ${S}/storageinfo.cpp ${S}/shortcuts.cpp ${S}/backup.cpp ${S}/webtweaks.cpp
    ${S}/linkeddevicename.cpp ${S}/performance.cpp ${S}/notificationrules.cpp
    ${S}/networkproxy.cpp ${S}/autostart.cpp ${S}/portalnotification.cpp
    ${S}/setupwizard.cpp
    ${S}/icons.qrc
)
target_include_directories(tst_logic PRIVATE ${S} ${CMAKE_BINARY_DIR})
target_compile_definitions(tst_logic PRIVATE ${TEST_DEFS})
target_link_libraries(tst_logic PRIVATE
    Qt${QT_VERSION_MAJOR}::Test Qt${QT_VERSION_MAJOR}::Widgets
    Qt${QT_VERSION_MAJOR}::Gui Qt${QT_VERSION_MAJOR}::Svg
    Qt${QT_VERSION_MAJOR}::WebEngineCore Qt${QT_VERSION_MAJOR}::Network)
if(UNIX AND NOT APPLE)
    target_link_libraries(tst_logic PRIVATE Qt${QT_VERSION_MAJOR}::DBus)
endif()
add_test(NAME logic COMMAND tst_logic)
set_tests_properties(logic PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

# ── SettingsWidget in-process test ──────────────────────────────────────────
# SettingsWidget is coupled to MainWindow (a qobject_cast), so it links the whole
# app's sources (minus main.cpp). Constructing it and changing every control's
# value fires the settings slots without pressing the modal/file-dialog buttons.
set(_app_srcs ${SOURCES} ${HEADERS} ${UI_FILES} ${SINGLEAPP_SOURCES}
              ${SINGLEAPP_HEADERS} ${RESOURCES})
list(FILTER _app_srcs EXCLUDE REGEX "(^|/)main\\.cpp$")
list(TRANSFORM _app_srcs PREPEND "${CMAKE_SOURCE_DIR}/")

add_executable(tst_settings tst_settings.cpp ${_app_srcs})
target_include_directories(tst_settings PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/singleapplication
    ${CMAKE_SOURCE_DIR}/src/widgets/elidedlabel
    ${CMAKE_SOURCE_DIR}/src/widgets/scrolltext
    ${CMAKE_SOURCE_DIR}/src/widgets/MoreApps
    ${CMAKE_BINARY_DIR})
target_compile_definitions(tst_settings PRIVATE ${TEST_DEFS} QAPPLICATION_CLASS=QApplication)
target_link_libraries(tst_settings PRIVATE
    Qt${QT_VERSION_MAJOR}::Test Qt${QT_VERSION_MAJOR}::Core Qt${QT_VERSION_MAJOR}::Gui
    Qt${QT_VERSION_MAJOR}::Widgets Qt${QT_VERSION_MAJOR}::WebEngineWidgets
    Qt${QT_VERSION_MAJOR}::WebChannel Qt${QT_VERSION_MAJOR}::Positioning
    Qt${QT_VERSION_MAJOR}::Network Qt${QT_VERSION_MAJOR}::Svg)
if(UNIX AND NOT APPLE)
    target_link_libraries(tst_settings PRIVATE notify-qt X11 xcb Qt${QT_VERSION_MAJOR}::DBus)
endif()
add_test(NAME settings COMMAND tst_settings)
set_tests_properties(settings PROPERTIES ENVIRONMENT
    "QT_QPA_PLATFORM=offscreen;QTWEBENGINE_CHROMIUM_FLAGS=--no-sandbox --disable-gpu")
