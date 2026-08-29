# Host-side tests for the arithmetic game.
#
# The game/ layer is deliberately free of LVGL and ESP-IDF, so it compiles and
# runs straight on the host -- every invariant below is checked before any of
# it reaches the watch.
#
#   cmake -S tools/math_host_test -B build_math && cmake --build build_math
#   ./build_math/math_logic_test
cmake_minimum_required(VERSION 3.16)
project(math_host_test C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

set(REPO_ROOT ${CMAKE_CURRENT_LIST_DIR}/../..)
set(GAME_DIR ${REPO_ROOT}/main/apps/app_math/game)

add_executable(math_logic_test
    logic_test.cpp
    ${GAME_DIR}/problem.cpp
    ${GAME_DIR}/session.cpp
    ${GAME_DIR}/economy.cpp
)
target_include_directories(math_logic_test PRIVATE ${GAME_DIR})
target_compile_options(math_logic_test PRIVATE -Wall -Wextra)

# --------------------------------------------------------------------------
# math_sim: real LVGL 9.5 host simulator for AppMath's view layer.
#
# Compiles the actual quiz.cpp / result.cpp against a real LVGL, an offscreen
# 466x466 RGB565 framebuffer, and the same HAL/log/asset stubs the stroke-order
# sim uses. A layout bug found here is a bug in the firmware view code.
#
#   ./build_math/math_sim --out /tmp/mathshots
# --------------------------------------------------------------------------
set(SIM_DIR ${CMAKE_CURRENT_SOURCE_DIR}/sim)
set(HANZI_SIM_DIR ${REPO_ROOT}/tools/hanzi_host_test/sim)  # lv_conf.h + stubs
set(VIEW_DIR ${REPO_ROOT}/main/apps/app_math/view)
set(LVGL_DIR ${REPO_ROOT}/components/lvgl)
set(FONT_DIR ${REPO_ROOT}/main/assets/fonts)

file(GLOB_RECURSE LVGL_SOURCES ${LVGL_DIR}/src/*.c)

add_executable(math_sim
    ${SIM_DIR}/sim_main.cpp
    ${VIEW_DIR}/quiz.cpp
    ${VIEW_DIR}/result.cpp
    ${VIEW_DIR}/map.cpp
    ${GAME_DIR}/problem.cpp
    ${GAME_DIR}/session.cpp
    ${GAME_DIR}/economy.cpp
    ${FONT_DIR}/lv_font_hanzi_ui_24.c
    ${FONT_DIR}/lv_font_digit_96.c
    ${FONT_DIR}/lv_font_digit_64.c
    ${LVGL_SOURCES}
)

target_include_directories(math_sim PRIVATE
    ${SIM_DIR}
    ${HANZI_SIM_DIR}
    ${HANZI_SIM_DIR}/stubs
    ${VIEW_DIR}
    ${GAME_DIR}
    ${LVGL_DIR}
    ${LVGL_DIR}/src
    ${REPO_ROOT}/components
)
target_compile_definitions(math_sim PRIVATE LV_CONF_INCLUDE_SIMPLE)
target_compile_options(math_sim PRIVATE -Wall -O2)
