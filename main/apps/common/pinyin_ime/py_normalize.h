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
// ("hǎo" -> "hao", "lǜ" -> "lv"). ü folds to 'v', the standard IME carrier:
// no pinyin syllable contains an ASCII v, so the letter is free, and folding
// ü into u instead would merge lu/lü (路/绿) -- wrong for a device whose job
// is teaching the distinction. On the T9 keypad the fold is invisible either
// way (u and v share key 8). Returns the number of characters written (NUL
// excluded, always < cap), or 0 with out[0] == '\0' when the input is empty,
// overlong, or contains a character outside the pinyin alphabet -- data
// problems the caller should surface, not paper over.
size_t pyNormalize(const char* toned, char* out, size_t cap);

// Renders a normalised syllable (or prefix) for display: 'v' becomes UTF-8
// "ü", everything else copies through. The children never see the v -- it is
// an internal carrier only. Returns the number of bytes written (NUL
// excluded), or 0 with out[0] == '\0' when the result would not fit.
size_t pyDisplay(const char* plain, char* out, size_t cap);

// T9 keypad digit ('2'..'9') for a lowercase ASCII letter, or 0 if the
// character is no letter.
char pyDigitOf(char letter);

}  // namespace pime
