/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once
#include <cstddef>
#include <cstdint>
#include "hz_data.h"

// Anti-aliased scanline fill producing 8-bit coverage. Host-testable: no LVGL,
// no ESP-IDF.

namespace hz {

// An 8-bit coverage plane positioned in screen space.
struct Mask {
    uint8_t* data   = nullptr;
    int16_t x0      = 0;  // screen coordinate of column 0
    int16_t y0      = 0;  // screen coordinate of row 0
    uint16_t w      = 0;
    uint16_t h      = 0;
    uint16_t stride = 0;

    uint8_t* row(uint16_t y) const
    {
        return data + static_cast<size_t>(y) * stride;
    }
    bool empty() const
    {
        return w == 0 || h == 0;
    }
};

// Tight integer bounds of a polygon, clipped to [0, clip_w) x [0, clip_h).
// Returns false when the polygon is fully outside the clip box.
bool polygonBounds(const Point* poly, uint16_t count, int16_t clip_w, int16_t clip_h,
                   int16_t& x0, int16_t& y0, uint16_t& w, uint16_t& h);

class Rasterizer {
public:
    // scratch must hold at least (max_mask_width + max_polygon_points) floats.
    Rasterizer(float* scratch, size_t float_count);

    // Fills the closed polygon into mask with even-odd parity, ORing coverage
    // over whatever is already there (so several strokes can share a mask).
    // Returns false when the scratch space is too small.
    bool fill(const Point* poly, uint16_t count, const Mask& mask);

private:
    float* _scratch  = nullptr;
    size_t _capacity = 0;
};

}  // namespace hz
