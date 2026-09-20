cmake_minimum_required(VERSION 3.21)

project(system-overlay
    VERSION 1.4.1
    DESCRIPTION "Lightweight KDE Plasma performance overlay (CPU/GPU/RAM/VRAM/NET/FPS HUD)"
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_AUTOMOC ON)

include(GNUInstallDirs)

find_package(Qt6 6.6 REQUIRED COMPONENTS Core Gui Widgets DBus)

option(WITH_LAYERSHELL "Use KDE LayerShellQt for the Wayland overlay" ON)
option(INSTALL_AUTOSTART "Install XDG autostart entry into /etc/xdg/autostart (off by default)" OFF)

if(WITH_LAYERSHELL)
    find_package(LayerShellQt 6.0 CONFIG)
    if(NOT LayerShellQt_FOUND)
        message(FATAL_ERROR
            " LayerShellQt was not found, but it is required for the Wayland overlay.\n"
            " On CachyOS / Arch Linux install it with:\n"
            "     sudo pacman -S layer-shell-qt\n"
            " If you really want to build without it (plain always-on-top window on Wayland,\n"
            " NOT recommended), reconfigure with -DWITH_LAYERSHELL=OFF."
        )
    endif()
endif()

add_executable(system-overlay
    src/main.cpp
    src/config/Config.cpp
    src/metrics/MetricManager.cpp
    src/metrics/CpuMetrics.cpp
    src/metrics/MemoryMetrics.cpp
    src/metrics/NetMetrics.cpp
    src/metrics/FpsTracker.cpp
    src/sensors/HwmonScanner.cpp
    src/gpu/GpuDiscovery.cpp
    src/gpu/AmdGpuBackend.cpp
    src/gpu/IntelGpuBackend.cpp
    src/gpu/NvidiaGpuBackend.cpp
    src/gpu/GpuMetrics.cpp
    src/overlay/OverlayWindow.cpp
    src/overlay/WaylandOverlay.cpp
    src/overlay/X11Overlay.cpp
    src/overlay/OverlayController.cpp
    src/overlay/TrayIcon.cpp
)

target_compile_definitions(system-overlay PRIVATE
    SYSTEM_OVERLAY_VERSION="${PROJECT_VERSION}"
    QT_NO_CAST_FROM_ASCII
    QT_NO_KEYWORDS
)

target_compile_options(system-overlay PRIVATE
    -Wall -Wextra -Wpedantic
)

target_include_directories(system-overlay PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)

target_link_libraries(system-overlay PRIVATE Qt6::Core Qt6::Gui Qt6::Widgets Qt6::DBus)

if(WITH_LAYERSHELL)
    target_link_libraries(system-overlay PRIVATE LayerShellQt::Interface)
    target_compile_definitions(system-overlay PRIVATE SYSTEM_OVERLAY_WITH_LAYERSHELL=1)
else()
    message(WARNING
        " Building without LayerShellQt: on Wayland the overlay will be a plain\n"
        " always-on-top window. It will NOT reliably stay above fullscreen windows\n"
        " under KWin. This is not the intended mode of operation."
    )
endif()

configure_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/resources/system-overlay.desktop.in
    ${CMAKE_CURRENT_BINARY_DIR}/system-overlay.desktop
    @ONLY
)

install(TARGETS system-overlay RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/system-overlay.desktop
        DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)

if(INSTALL_AUTOSTART)
    install(FILES ${CMAKE_CURRENT_BINARY_DIR}/system-overlay.desktop
            DESTINATION ${CMAKE_INSTALL_SYSCONFDIR}/xdg/autostart)
endif()

# Optional uninstall helper: cmake --build build --target uninstall
# (uses build/install_manifest.txt written by 'cmake --install')
if(NOT TARGET uninstall)
    add_custom_target(uninstall
        COMMAND test -f ${CMAKE_BINARY_DIR}/install_manifest.txt
        COMMAND xargs -r rm -v < ${CMAKE_BINARY_DIR}/install_manifest.txt
        COMMENT "Removing files listed in install_manifest.txt"
        VERBATIM
        USES_TERMINAL
    )
endif()
