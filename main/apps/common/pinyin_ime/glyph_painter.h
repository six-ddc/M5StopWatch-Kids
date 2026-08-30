/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once
#include <cstdint>

// Shared by every pinyin input view (picker, dial, T9 keypad): they all
// render candidate characters the same way, so they share one painter
// contract and one host-side implementation.

namespace pime {

// Draws one candidate into an A8 coverage buffer (stride == w, zeroed by the
// caller) and captions it. The hanzi app backs this with the stroke engine;
// a different host could use a font or bitmaps.
class GlyphPainter {
public:
    virtual ~GlyphPainter()                                                  = default;
    virtual bool paint(uint16_t id, uint8_t* buffer, uint16_t w, uint16_t h) = 0;
    // Toned reading(s) of `id`, space-separated, primary first; nullptr for
    // no caption. Each view shows the reading matching its current input.
    virtual const char* caption(uint16_t id) const = 0;
};

}  // namespace pime
