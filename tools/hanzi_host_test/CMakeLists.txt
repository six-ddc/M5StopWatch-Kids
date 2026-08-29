cmake_minimum_required(VERSION 3.16)
project(hanzi_host_test CXX C)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)

# Builds the real engine sources, not a copy: the point of this harness is that
# a bug found here is a bug in the firmware code.
set(ENGINE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../../main/apps/app_hanzi/engine)

add_executable(hanzi_host_test
    main.cpp
    ${ENGINE_DIR}/hz_data.cpp
    ${ENGINE_DIR}/hz_raster.cpp
)

target_include_directories(hanzi_host_test PRIVATE ${ENGINE_DIR})
target_compile_options(hanzi_host_test PRIVATE -Wall -Wextra -O2)

add_executable(anim_host_test
    anim_main.cpp
    ${ENGINE_DIR}/hz_data.cpp
    ${ENGINE_DIR}/hz_raster.cpp
    ${ENGINE_DIR}/hz_anim.cpp
    ${ENGINE_DIR}/hz_compose.cpp
)

target_include_directories(anim_host_test PRIVATE ${ENGINE_DIR})
target_compile_options(anim_host_test PRIVATE -Wall -Wextra -O2)

# --------------------------------------------------------------------------
# hanzi_sim: real LVGL 9.5 host simulator for AppHanzi's view layer.
#
# Compiles the actual browse.cpp / learn.cpp against a real LVGL, an offscreen
# 466x466 RGB565 framebuffer, and stub replacements for the ESP-IDF/HAL/log
# headers those files pull in (sim/stubs). A rendering or layout bug found
# here is a bug in the firmware view code, not a simulator artifact.
# --------------------------------------------------------------------------
set(REPO_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/../..)
set(SIM_DIR ${CMAKE_CURRENT_SOURCE_DIR}/sim)
set(VIEW_DIR ${REPO_ROOT}/main/apps/app_hanzi/view)
set(LVGL_DIR ${REPO_ROOT}/components/lvgl)
set(FONT_DIR ${REPO_ROOT}/main/assets/fonts)

file(GLOB_RECURSE LVGL_SOURCES ${LVGL_DIR}/src/*.c)

add_executable(hanzi_sim
    ${SIM_DIR}/sim_main.cpp
    ${VIEW_DIR}/browse.cpp
    ${VIEW_DIR}/learn.cpp
    ${ENGINE_DIR}/hz_data.cpp
    ${ENGINE_DIR}/hz_raster.cpp
    ${ENGINE_DIR}/hz_anim.cpp
    ${ENGINE_DIR}/hz_compose.cpp
    ${FONT_DIR}/lv_font_hanzi_ui_24.c
    ${FONT_DIR}/lv_font_hanzi_pinyin_44.c
    ${LVGL_SOURCES}
)

target_include_directories(hanzi_sim PRIVATE
    ${SIM_DIR}
    ${SIM_DIR}/stubs
    ${VIEW_DIR}
    ${ENGINE_DIR}
    ${LVGL_DIR}
    ${LVGL_DIR}/src
    ${REPO_ROOT}/components
)
target_compile_definitions(hanzi_sim PRIVATE LV_CONF_INCLUDE_SIMPLE)
target_compile_options(hanzi_sim PRIVATE -Wall -O2)
