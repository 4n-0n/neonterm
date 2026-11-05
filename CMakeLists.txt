cmake_minimum_required(VERSION 3.21)
project(neonterm VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_INCLUDE_CURRENT_DIR ON)

find_package(Qt6 6.2 REQUIRED COMPONENTS Core Gui Widgets OpenGL OpenGLWidgets Multimedia)

option(NT_BUILD_TESTS "Build the test executables" ON)

add_library(nt_core STATIC
    src/core/Screen.cpp
    src/core/VtParser.cpp
    src/core/Pty.cpp
    src/core/Session.cpp
)
target_include_directories(nt_core PUBLIC src)
target_link_libraries(nt_core PUBLIC Qt6::Core Qt6::Gui)

add_library(nt_ui STATIC
    src/ui/TermSurface.cpp
    src/ui/GlTermView.cpp
    src/ui/TermView.cpp
    src/ui/HeaderBar.cpp
    src/ui/StatusLine.cpp
    src/ui/TermWindow.cpp
    src/ui/Theme.cpp
    src/ui/Demo.cpp
    src/ui/LcarsView.cpp
    src/ui/MediaOverlay.cpp
    src/ui/MediaStore.cpp
    src/ui/NeonFont.cpp
)
target_link_libraries(nt_ui PUBLIC nt_core Qt6::Widgets Qt6::OpenGL Qt6::OpenGLWidgets Qt6::Multimedia)

add_executable(neonterm src/main.cpp)
target_link_libraries(neonterm PRIVATE nt_ui)

# show: the shell side of the media protocol
add_executable(show src/tools/show.cpp)
target_link_libraries(show PRIVATE Qt6::Core Qt6::Gui)

include(GNUInstallDirs)
install(TARGETS neonterm show RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})

# The desktop entry needs an absolute Exec, because plasmashell's PATH is not the
# login shell's and ~/.local/bin is not in it. The path is only known at install
# time, so the file is written then, with DESTDIR applied to the file location
# but not to the command it launches.
install(CODE [[
  file(READ "${CMAKE_CURRENT_SOURCE_DIR}/packaging/neonterm.desktop.in" _txt)
  string(REPLACE "@BINDIR@" "${CMAKE_INSTALL_PREFIX}/bin" _txt "${_txt}")
  file(WRITE "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/share/applications/neonterm.desktop" "${_txt}")
]])

foreach(_size 16 24 32 48 64 128 256 512)
  install(FILES assets/icons/neonterm-${_size}.png
          DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/${_size}x${_size}/apps
          RENAME neonterm.png)
endforeach()

if(NT_BUILD_TESTS)
    find_package(Qt6 QUIET COMPONENTS Test)
    if(Qt6Test_FOUND)
        enable_testing()
        foreach(t test_screen test_session test_view test_font test_media)
            add_executable(${t} tests/${t}.cpp)
            target_link_libraries(${t} PRIVATE nt_ui Qt6::Test)
            add_test(NAME ${t} COMMAND ${t})
            set_tests_properties(${t} PROPERTIES
                ENVIRONMENT "QT_QPA_PLATFORM=offscreen;NEONTERM_CONFIG_DIR=${CMAKE_BINARY_DIR}/testcfg;SHOW_BIN=$<TARGET_FILE:show>")
        endforeach()
    endif()
endif()
