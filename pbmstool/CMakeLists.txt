cmake_minimum_required(VERSION 3.14)
project(pbmstool LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

option(PBMS_BUILD_GUI "Build the Qt graphical interface (pbmstool-gui) when Qt is available" ON)

# ── Shared BMS core (protocol, serial port, BMS commands, parameter helpers) ──
add_library(pbmscore STATIC
    src/protocol.cpp
    src/serial.cpp
    src/bms.cpp
    src/params.cpp
)
target_include_directories(pbmscore PUBLIC src)
target_compile_options(pbmscore PRIVATE -Wall -Wextra)

# ── Command-line tool + ncurses TUI ──
find_package(Curses REQUIRED)

add_executable(pbmstool
    src/tui.cpp
    src/main.cpp
)
target_include_directories(pbmstool PRIVATE ${CURSES_INCLUDE_DIR})
target_link_libraries(pbmstool PRIVATE pbmscore ${CURSES_LIBRARIES})
target_compile_options(pbmstool PRIVATE -Wall -Wextra)
install(TARGETS pbmstool RUNTIME DESTINATION bin)

# ── Qt GUI (optional) ──
if(PBMS_BUILD_GUI)
    find_package(QT NAMES Qt6 Qt5 COMPONENTS Widgets QUIET)
    if(QT_FOUND)
        find_package(Qt${QT_VERSION_MAJOR} COMPONENTS Widgets REQUIRED)
        message(STATUS "Building pbmstool-gui with Qt ${QT_VERSION_MAJOR}")

        add_executable(pbmstool-gui
            gui/main.cpp
            gui/mainwindow.cpp
            gui/mainwindow.h
            gui/worker.cpp
            gui/worker.h
            gui/cellbars.cpp
            gui/cellbars.h
        )
        set_target_properties(pbmstool-gui PROPERTIES AUTOMOC ON)
        target_link_libraries(pbmstool-gui PRIVATE pbmscore Qt${QT_VERSION_MAJOR}::Widgets)
        target_compile_options(pbmstool-gui PRIVATE -Wall -Wextra)

        install(TARGETS pbmstool-gui RUNTIME DESTINATION bin)
        install(FILES gui/pbmstool-gui.desktop DESTINATION share/applications)
    else()
        message(STATUS "Qt Widgets not found - skipping pbmstool-gui "
                       "(install it with: sudo apt install qtbase5-dev)")
    endif()
endif()
