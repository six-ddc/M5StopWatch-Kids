/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once
#include <lvgl.h>

LV_FONT_DECLARE(lv_font_hanzi_ui_24);
LV_FONT_DECLARE(lv_font_hanzi_pinyin_44);
// Toneless lowercase latin + '.', for the T9 keyboard's pinyin chips.
LV_FONT_DECLARE(lv_font_pinyin_latin_32);
// Lowercase latin + toned vowels, for candidate captions: at 20 px every
// toned syllable ("chuáng" is the widest at 70 px) fits one chip line.
LV_FONT_DECLARE(lv_font_pinyin_tone_20);
// Digits only, for the arithmetic game: the equation and the answer cards.
LV_FONT_DECLARE(lv_font_digit_96);
LV_FONT_DECLARE(lv_font_digit_64);

LV_IMG_DECLARE(icon_hanzi);
LV_IMG_DECLARE(icon_math);
LV_IMG_DECLARE(icon_english);
LV_IMG_DECLARE(icon_indicator_left);
LV_IMG_DECLARE(icon_indicator_right);
