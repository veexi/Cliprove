cmake_minimum_required(VERSION 3.24)
project(ClipTool VERSION 0.2.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

find_package(Qt6 6.6 REQUIRED COMPONENTS Core DBus Gui Widgets Sql)
find_package(Qt6 6.6 QUIET COMPONENTS Quick Qml)
find_package(KF6Config QUIET)
find_package(KF6GlobalAccel QUIET)
find_package(PlasmaQuick QUIET)
find_package(KWayland QUIET)
find_package(PkgConfig REQUIRED)
pkg_check_modules(WAYLAND REQUIRED IMPORTED_TARGET wayland-client)
pkg_check_modules(XCB REQUIRED IMPORTED_TARGET xcb xcb-xtest xcb-keysyms)
pkg_check_modules(EI REQUIRED IMPORTED_TARGET libei-1.0 liboeffis-1.0)
find_program(WAYLAND_SCANNER wayland-scanner REQUIRED)

set(PROTOCOL_XML "${CMAKE_SOURCE_DIR}/protocols/ext-data-control-v1.xml")
set(PROTOCOL_HEADER "${CMAKE_BINARY_DIR}/generated/ext-data-control-v1-client-protocol.h")
set(PROTOCOL_CODE "${CMAKE_BINARY_DIR}/generated/ext-data-control-v1-protocol.c")

file(MAKE_DIRECTORY "${CMAKE_BINARY_DIR}/generated")

add_custom_command(
    OUTPUT "${PROTOCOL_HEADER}" "${PROTOCOL_CODE}"
    COMMAND "${WAYLAND_SCANNER}" client-header "${PROTOCOL_XML}" "${PROTOCOL_HEADER}"
    COMMAND "${WAYLAND_SCANNER}" private-code "${PROTOCOL_XML}" "${PROTOCOL_CODE}"
    DEPENDS "${PROTOCOL_XML}"
    VERBATIM
)

add_executable(cliptool
    src/main.cpp
    src/core/AppSettings.h
    src/core/ClipboardTypes.h
    src/core/HistoryStore.h
    src/core/HistoryStore.cpp
    src/platform/ClipboardBackend.h
    src/platform/WaylandDataControlBackend.h
    src/platform/WaylandDataControlBackend.cpp
    src/platform/PasteBackend.h
    src/platform/PortalEisPasteBackend.h
    src/platform/PortalEisPasteBackend.cpp
    src/platform/X11ClipboardBackend.h
    src/platform/X11ClipboardBackend.cpp
    src/platform/X11PasteBackend.h
    src/platform/X11PasteBackend.cpp
    src/ui/MainWindow.h
    src/ui/MainWindow.cpp
    src/ui/HistoryItemDelegate.h
    src/ui/HistoryItemDelegate.cpp
    src/ui/ToggleSwitch.h
    src/ui/ToggleSwitch.cpp
    src/ui/SettingsDialog.h
    src/ui/SettingsDialog.cpp
    src/ui/Theme.h
    src/ui/Theme.cpp
    resources/resources.qrc
    "${PROTOCOL_CODE}"
)

target_include_directories(cliptool PRIVATE
    "${CMAKE_SOURCE_DIR}/src"
    "${CMAKE_BINARY_DIR}/generated"
)

target_link_libraries(cliptool PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    Qt6::Sql
    PkgConfig::WAYLAND
    PkgConfig::XCB
    PkgConfig::EI
)

target_compile_options(cliptool PRIVATE -Wall -Wextra -Wpedantic)
install(TARGETS cliptool RUNTIME DESTINATION bin)

add_executable(cliprove-paste-daemon
    src/paste/PasteDaemon.cpp
    src/platform/PasteBackend.h
    src/platform/PortalEisPasteBackend.h
    src/platform/PortalEisPasteBackend.cpp
)
target_include_directories(cliprove-paste-daemon PRIVATE "${CMAKE_SOURCE_DIR}/src")
target_link_libraries(cliprove-paste-daemon PRIVATE
    Qt6::Core
    Qt6::DBus
    PkgConfig::EI
)
target_compile_options(cliprove-paste-daemon PRIVATE -Wall -Wextra -Wpedantic)
install(TARGETS cliprove-paste-daemon RUNTIME DESTINATION bin)

if(TARGET Plasma::PlasmaQuick AND TARGET Plasma::KWaylandClient AND TARGET KF6::GlobalAccel AND TARGET Qt6::Quick AND TARGET Qt6::Qml)
    add_executable(cliprove-popup
        src/popup/CliprovePopup.cpp
        src/popup/PasteController.cpp
        src/popup/PasteController.h
        src/platform/ClipboardBackend.h
        src/platform/WaylandDataControlBackend.h
        src/platform/WaylandDataControlBackend.cpp
        "${PROTOCOL_CODE}"
        resources/popup.qrc
    )
    target_link_libraries(cliprove-popup PRIVATE
        PkgConfig::WAYLAND
        Qt6::Core
        Qt6::DBus
        Qt6::Gui
        Qt6::Qml
        Qt6::Quick
        Plasma::PlasmaQuick
        Plasma::KWaylandClient
        KF6::GlobalAccel
    )
    target_compile_options(cliprove-popup PRIVATE -Wall -Wextra -Wpedantic)
    target_include_directories(cliprove-popup PRIVATE "${CMAKE_SOURCE_DIR}/src" "${CMAKE_BINARY_DIR}/generated")
    install(TARGETS cliprove-popup RUNTIME DESTINATION bin)
endif()

include(CTest)
if(BUILD_TESTING)
    find_package(Qt6 REQUIRED COMPONENTS Test)
    add_executable(paste-controller-test
        tests/PasteControllerTest.cpp
        src/popup/PasteController.cpp
        src/popup/PasteController.h
    )
    target_include_directories(paste-controller-test PRIVATE "${CMAKE_SOURCE_DIR}/src")
    target_link_libraries(paste-controller-test PRIVATE Qt6::Gui Qt6::DBus Qt6::Test)
    add_test(NAME paste-controller-test COMMAND dbus-run-session -- $<TARGET_FILE:paste-controller-test> -platform offscreen)

    add_executable(history-store-test
        tests/HistoryStoreTest.cpp
        src/core/ClipboardTypes.h
        src/core/HistoryStore.h
        src/core/HistoryStore.cpp
    )
    target_include_directories(history-store-test PRIVATE "${CMAKE_SOURCE_DIR}/src")
    target_link_libraries(history-store-test PRIVATE Qt6::Core Qt6::Gui Qt6::Sql)
    add_test(NAME history-store-test COMMAND history-store-test)

    add_executable(main-window-flow-test
        tests/MainWindowFlowTest.cpp
        src/core/ClipboardTypes.h
        src/core/HistoryStore.h
        src/core/HistoryStore.cpp
        src/platform/ClipboardBackend.h
        src/platform/PasteBackend.h
        src/ui/MainWindow.h
        src/ui/MainWindow.cpp
        src/ui/HistoryItemDelegate.h
        src/ui/HistoryItemDelegate.cpp
        src/ui/ToggleSwitch.h
        src/ui/ToggleSwitch.cpp
        src/ui/SettingsDialog.h
        src/ui/SettingsDialog.cpp
        src/ui/Theme.h
        src/ui/Theme.cpp
        resources/resources.qrc
    )
    target_include_directories(main-window-flow-test PRIVATE "${CMAKE_SOURCE_DIR}/src")
    target_link_libraries(main-window-flow-test PRIVATE
        Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Sql)
    add_test(NAME main-window-flow-test COMMAND main-window-flow-test)
endif()
