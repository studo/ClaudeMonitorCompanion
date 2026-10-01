# Claude Monitor Companion — desktop build (Linux).
#
# Compiles the ESP32 firmware's own sources, unmodified, against a small
# Arduino/ESP-IDF stand-in (shim/) and desktop versions of the three hardware
# drivers (src/). The ESP32 build does not see any of this: PlatformIO only
# looks in ../src and ../include.
#
#   sudo apt install build-essential cmake pkg-config libsdl3-dev
#   cmake -S Code/desktop -B Code/desktop/build -DCMAKE_BUILD_TYPE=Release
#   cmake --build Code/desktop/build -j

cmake_minimum_required(VERSION 3.20)
project(claude-monitor-companion VERSION 1.1 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE RelWithDebInfo)
endif()

find_package(SDL3 3.2 REQUIRED CONFIG COMPONENTS SDL3)
find_package(Threads REQUIRED)

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
target_link_libraries(cmc_core PUBLIC SDL3::SDL3 Threads::Threads)

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
endif()
