cmake_minimum_required(VERSION 3.16)
project(hanzi_host_test CXX C)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)

# Builds the real engine sources, not a copy: the point of this harness is that
# a bug found here is a bug in the firmware code.
set(REPO_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/../..)
set(ENGINE_DIR ${REPO_ROOT}/main/apps/app_hanzi/engine)

# Test the blob that actually ships. The firmware links this exact file via
# EMBED_FILES (see main/CMakeLists.txt), so pointing the tests at it means they
# cover what actually reaches the watch, not the pipeline's scratch copy.
set(HANZI_BLOB_PATH ${REPO_ROOT}/main/assets/hanzi/hanzi_data.bin)

add_executable(hanzi_host_test
    main.cpp
    ${ENGINE_DIR}/hz_data.cpp
    ${ENGINE_DIR}/hz_raster.cpp
)

target_include_directories(hanzi_host_test PRIVATE ${ENGINE_DIR})
target_compile_definitions(hanzi_host_test PRIVATE
    HANZI_BLOB_PATH="${HANZI_BLOB_PATH}")
target_compile_options(hanzi_host_test PRIVATE -Wall -Wextra -O2)

# Invariant tests for the T9 pinyin engine, fed by the shipped blob: a
# differential check against a naive reference plus the round-trip guarantee
# that every reading finds its own character.
set(PIME_DIR ${REPO_ROOT}/main/apps/common/pinyin_ime)

add_executable(hanzi_logic_test
    logic_test.cpp
    ${ENGINE_DIR}/hz_data.cpp
    ${PIME_DIR}/py_normalize.cpp
    ${PIME_DIR}/t9_engine.cpp
)

target_include_directories(hanzi_logic_test PRIVATE ${ENGINE_DIR} ${PIME_DIR})
target_compile_definitions(hanzi_logic_test PRIVATE
    HANZI_BLOB_PATH="${HANZI_BLOB_PATH}")
target_compile_options(hanzi_logic_test PRIVATE -Wall -Wextra -O2)

add_executable(anim_host_test
    anim_main.cpp
    ${ENGINE_DIR}/hz_data.cpp
    ${ENGINE_DIR}/hz_raster.cpp
    ${ENGINE_DIR}/hz_anim.cpp
    ${ENGINE_DIR}/hz_compose.cpp
)

target_include_directories(anim_host_test PRIVATE ${ENGINE_DIR})
target_compile_definitions(anim_host_test PRIVATE
    HANZI_BLOB_PATH="${HANZI_BLOB_PATH}")
target_compile_options(anim_host_test PRIVATE -Wall -Wextra -O2)

# --------------------------------------------------------------------------
# hanzi_sim: real LVGL 9.5 host simulator for AppHanzi's view layer.
#
# Compiles the actual browse.cpp / learn.cpp against a real LVGL, an offscreen
# 466x466 RGB565 framebuffer, and stub replacements for the ESP-IDF/HAL/log
# headers those files pull in (sim/stubs). A rendering or layout bug found
# here is a bug in the firmware view code, not a simulator artifact.
# --------------------------------------------------------------------------
set(SIM_DIR ${CMAKE_CURRENT_SOURCE_DIR}/sim)
set(VIEW_DIR ${REPO_ROOT}/main/apps/app_hanzi/view)
set(LVGL_DIR ${REPO_ROOT}/components/lvgl)
set(FONT_DIR ${REPO_ROOT}/main/assets/fonts)

file(GLOB_RECURSE LVGL_SOURCES ${LVGL_DIR}/src/*.c)

add_executable(hanzi_sim
    ${SIM_DIR}/sim_main.cpp
    ${VIEW_DIR}/browse.cpp
    ${VIEW_DIR}/learn.cpp
    ${VIEW_DIR}/search.cpp
    ${PIME_DIR}/ime_view.cpp
    ${PIME_DIR}/t9_engine.cpp
    ${PIME_DIR}/py_normalize.cpp
    ${ENGINE_DIR}/hz_data.cpp
    ${ENGINE_DIR}/hz_raster.cpp
    ${ENGINE_DIR}/hz_anim.cpp
    ${ENGINE_DIR}/hz_compose.cpp
    ${FONT_DIR}/lv_font_hanzi_ui_24.c
    ${FONT_DIR}/lv_font_hanzi_pinyin_44.c
    ${FONT_DIR}/lv_font_pinyin_latin_32.c
    ${LVGL_SOURCES}
)

# The stub directory must stay ahead of the real main/ tree: <hal/hal.h>,
# <esp_heap_caps.h> and <assets/assets.h> have to resolve to the stubs, while
# <apps/common/pinyin_ime/...> resolves to the real sources under main/.
target_include_directories(hanzi_sim PRIVATE
    ${SIM_DIR}
    ${SIM_DIR}/stubs
    ${VIEW_DIR}
    ${ENGINE_DIR}
    ${REPO_ROOT}/main
    ${LVGL_DIR}
    ${LVGL_DIR}/src
    ${REPO_ROOT}/components
)
target_compile_definitions(hanzi_sim PRIVATE
    LV_CONF_INCLUDE_SIMPLE
    HANZI_BLOB_PATH="${HANZI_BLOB_PATH}")
target_compile_options(hanzi_sim PRIVATE -Wall -O2)
