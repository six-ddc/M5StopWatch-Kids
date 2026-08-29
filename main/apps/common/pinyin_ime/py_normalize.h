/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once
#include <cstddef>

// Toned-pinyin normalisation. Pure logic, no LVGL / ESP-IDF headers -- the
// host tests compile this file directly (same discipline as hz_data.h).

namespace pime {

// Converts one toned UTF-8 pinyin syllable to toneless lowercase ASCII
// ("hǎo" -> "hao", "lǜ" -> "lu"). ü folds to u: on a T9 keypad u and v share
// key 8, so distinguishing them buys nothing. Returns the number of
// characters written (NUL excluded, always < cap), or 0 with out[0] == '\0'
// when the input is empty, overlong, or contains a character outside the
// pinyin alphabet -- data problems the caller should surface, not paper over.
size_t pyNormalize(const char* toned, char* out, size_t cap);

// T9 keypad digit ('2'..'9') for a lowercase ASCII letter, or 0 if the
// character is no letter.
char pyDigitOf(char letter);

}  // namespace pime
