/*
 * Host-sim stub for main/assets/assets.h.
 *
 * Only the fonts the view layers actually use; their real .c sources are
 * compiled into whichever sim binary needs them (see the CMakeLists in
 * tools/hanzi_host_test and tools/math_host_test), so this merely provides the
 * declarations those .cpp files expect from <assets/assets.h>.
 *
 * Shared by both sims: a declaration nobody references costs nothing.
 */
#pragma once
#include <lvgl.h>

LV_FONT_DECLARE(lv_font_hanzi_ui_24);
LV_FONT_DECLARE(lv_font_hanzi_pinyin_44);
LV_FONT_DECLARE(lv_font_pinyin_latin_32);
LV_FONT_DECLARE(lv_font_pinyin_tone_20);
LV_FONT_DECLARE(lv_font_digit_96);
LV_FONT_DECLARE(lv_font_digit_64);
