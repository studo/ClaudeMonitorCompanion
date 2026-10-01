# Claude Monitor Companion — desktop build (Linux).
#
# Compiles the ESP32 firmware's own sources, unmodified, against a small
# Arduino/ESP-IDF stand-in (shim/) and desktop versions of the three hardware
# drivers (src/). The ESP32 build does not see any of this: PlatformIO only
# looks in ../src and ../include.
#
#   sudo apt install build-essential cmake pkg-config libsdl3-dev libsystemd-dev
#   cmake -S Code/desktop -B Code/desktop/build -DCMAKE_INSTALL_PREFIX=$HOME/.local
#   cmake --build Code/desktop/build -j
#   cmake --install Code/desktop/build
#
# The prefix is given at configure time, not to `cmake --install --prefix`,
# because the systemd unit and the .desktop file carry the installed path.

cmake_minimum_required(VERSION 3.20)
project(claude-monitor-companion VERSION 1.1 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE RelWithDebInfo)
endif()

find_package(SDL3 3.2 REQUIRED CONFIG COMPONENTS SDL3)
find_package(Threads REQUIRED)
# sd-bus, for the taskbar highlight and notifications. Always present where
# systemd is, which is what the user service needs anyway.
find_package(PkgConfig REQUIRED)
pkg_check_modules(SYSTEMD REQUIRED IMPORTED_TARGET libsystemd)
include(GNUInstallDirs)

set(FIRMWARE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/..)

# Everything the board runs except src/board/, which talks to the ST7789, the
# CST816 and the LEDC peripherals. Globbed so that a new firmware file in one of
# these directories joins the desktop build without anyone remembering to add
# it here.
file(GLOB FIRMWARE_SOURCES CONFIGURE_DEPENDS
  ${FIRMWARE_DIR}/src/*.cpp
  ${FIRMWARE_DIR}/src/app/*.cpp
  ${FIRMWARE_DIR}/src/net/*.cpp
  ${FIRMWARE_DIR}/src/ui/*.cpp
)

set(DESKTOP_SOURCES
  src/arduino.cpp
  src/attention.cpp
  src/bell_sdl.cpp
  src/display_sdl.cpp
  src/preferences.cpp
  src/runtime.cpp
  src/touch_sdl.cpp
  src/webserver.cpp
)

# The firmware plus everything it needs from the desktop, minus main(). The app
# and the tests both link this.
add_library(cmc_core STATIC ${FIRMWARE_SOURCES} ${DESKTOP_SOURCES})

# shim/ first, so <Arduino.h>, <WiFi.h> and "secrets.h" resolve to the desktop
# versions — including when an ESP32 user's include/secrets.h is present.
target_include_directories(cmc_core BEFORE PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}/shim
  ${CMAKE_CURRENT_SOURCE_DIR}/src
  ${FIRMWARE_DIR}/include
)
target_compile_definitions(cmc_core PUBLIC CMC_VERSION="${PROJECT_VERSION}")
target_link_libraries(cmc_core PUBLIC SDL3::SDL3 PkgConfig::SYSTEMD Threads::Threads)

# Warnings for the desktop code. The firmware sources are built the way the
# board builds them, and their warnings are the board's business.
set_source_files_properties(${DESKTOP_SOURCES} src/main_desktop.cpp tests/touch_test.cpp
  PROPERTIES COMPILE_OPTIONS "-Wall;-Wextra"
)

add_executable(claude-monitor-companion src/main_desktop.cpp)
target_link_libraries(claude-monitor-companion PRIVATE cmc_core)

# ------------------------------------------------------------------- tests --

option(BUILD_TESTING "Build the desktop tests" ON)
if(BUILD_TESTING)
  enable_testing()
  add_executable(touch_test tests/touch_test.cpp)
  target_link_libraries(touch_test PRIVATE cmc_core)
  add_test(NAME touch COMMAND touch_test)

  find_package(Python3 COMPONENTS Interpreter)
  if(Python3_FOUND)
    add_test(NAME smoke
      COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/smoke_test.py
              $<TARGET_FILE:claude-monitor-companion>)
  endif()
endif()

# ------------------------------------------------------------------ install --

set(CMC_BINARY ${CMAKE_INSTALL_FULL_BINDIR}/claude-monitor-companion)
configure_file(packaging/claude-monitor-companion.service.in
  ${CMAKE_CURRENT_BINARY_DIR}/claude-monitor-companion.service @ONLY)
configure_file(packaging/claude-monitor-companion.desktop.in
  ${CMAKE_CURRENT_BINARY_DIR}/claude-monitor-companion.desktop @ONLY)

install(TARGETS claude-monitor-companion RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
# share/systemd/user is on systemd's user unit path for both ~/.local and /usr/local.
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/claude-monitor-companion.service
  DESTINATION ${CMAKE_INSTALL_DATADIR}/systemd/user)
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/claude-monitor-companion.desktop
  DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
install(FILES packaging/claude-monitor-companion.svg
  DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/scalable/apps)
install(FILES packaging/claude-monitor-companion.kwinrule
  DESTINATION ${CMAKE_INSTALL_DATADIR}/claude-monitor-companion)
