# Host-side tests for the English app.
#
# Same split as the other two apps: data/ and game/ carry no LVGL and no
# ESP-IDF, so they compile and run natively, and the view layer is compiled
# against a real LVGL with an offscreen framebuffer. Everything below runs
# before any of it reaches the watch.
#
#   cmake -S tools/english_host_test -B build_english && cmake --build build_english
#   ./build_english/english_logic_test
#   ./build_english/english_sim --out /tmp/engshots
cmake_minimum_required(VERSION 3.16)
project(english_host_test C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

set(REPO_ROOT ${CMAKE_CURRENT_LIST_DIR}/../..)
set(APP_DIR ${REPO_ROOT}/main/apps/app_english)
set(DATA_DIR ${APP_DIR}/data)
set(GAME_DIR ${APP_DIR}/game)

# Test the blob that actually ships. The firmware links this exact file via
# EMBED_FILES (see main/CMakeLists.txt), so pointing the tests at it means they
# cover the packed subset -- not the pipeline's full-word-list scratch copy,
# which can differ whenever --pack-units is used.
set(BLOB_PATH ${REPO_ROOT}/main/assets/english/english_data.bin)

add_executable(english_logic_test
    logic_test.cpp
    ${DATA_DIR}/eng_data.cpp
    ${DATA_DIR}/adpcm.cpp
    ${GAME_DIR}/session.cpp
)
target_include_directories(english_logic_test PRIVATE ${DATA_DIR} ${GAME_DIR})
target_compile_definitions(english_logic_test PRIVATE
    ENGLISH_BLOB_PATH="${BLOB_PATH}")
target_compile_options(english_logic_test PRIVATE -Wall -Wextra)

# --------------------------------------------------------------------------
# english_sim: real LVGL 9.5 host simulator for the view layer.
#
# This one carries more weight than the arithmetic sim, because the English
# app draws bitmaps. The device has a 1 MB image cache and the host has none
# (LV_CACHE_DEF_SIZE is 0 in the sim's lv_conf.h), so this build is where an
# I4 palette or stride mistake shows up as a visibly wrong picture rather than
# as something that only misbehaves on hardware.
#
#   ./build_english/english_sim --out /tmp/engshots
# --------------------------------------------------------------------------
set(SIM_DIR ${CMAKE_CURRENT_SOURCE_DIR}/sim)
set(HANZI_SIM_DIR ${REPO_ROOT}/tools/hanzi_host_test/sim)  # lv_conf.h + stubs
set(VIEW_DIR ${APP_DIR}/view)
set(LVGL_DIR ${REPO_ROOT}/components/lvgl)
set(FONT_DIR ${REPO_ROOT}/main/assets/fonts)

file(GLOB_RECURSE LVGL_SOURCES ${LVGL_DIR}/src/*.c)

add_executable(english_sim
    ${SIM_DIR}/sim_main.cpp
    ${VIEW_DIR}/unit.cpp
    ${VIEW_DIR}/card.cpp
    ${VIEW_DIR}/quiz.cpp
    ${VIEW_DIR}/result.cpp
    ${DATA_DIR}/eng_data.cpp
    ${DATA_DIR}/adpcm.cpp
    ${GAME_DIR}/session.cpp
    ${FONT_DIR}/lv_font_hanzi_ui_24.c
    ${FONT_DIR}/lv_font_hanzi_pinyin_44.c
    ${LVGL_SOURCES}
)

target_include_directories(english_sim PRIVATE
    ${SIM_DIR}
    ${HANZI_SIM_DIR}
    ${HANZI_SIM_DIR}/stubs
    ${VIEW_DIR}
    ${DATA_DIR}
    ${GAME_DIR}
    ${LVGL_DIR}
    ${LVGL_DIR}/src
    ${REPO_ROOT}/components
)
target_compile_definitions(english_sim PRIVATE
    LV_CONF_INCLUDE_SIMPLE
    ENGLISH_BLOB_PATH="${BLOB_PATH}")
target_compile_options(english_sim PRIVATE -Wall -O2)
