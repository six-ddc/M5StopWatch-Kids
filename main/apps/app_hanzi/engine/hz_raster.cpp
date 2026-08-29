/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include "hz_raster.h"
#include <cmath>
#include <cstring>

namespace hz {

namespace {

// Vertical subsamples per output row. Four is the usual quality/cost knee: the
// horizontal term is already exact (fractional span coverage), so this only
// has to smooth near-horizontal edges.
constexpr uint8_t kSubsamples = 4;
constexpr float kSubWeight    = 1.0f / static_cast<float>(kSubsamples);

inline void insertSorted(float* values, uint16_t count, float v)
{
    uint16_t i = count;
    while (i > 0 && values[i - 1] > v) {
        values[i] = values[i - 1];
        i--;
    }
    values[i] = v;
}

}  // namespace

bool polygonBounds(const Point* poly, uint16_t count, int16_t clip_w, int16_t clip_h,
                   int16_t& x0, int16_t& y0, uint16_t& w, uint16_t& h)
{
    if (poly == nullptr || count == 0) {
        return false;
    }
    float minx = poly[0].x, maxx = poly[0].x;
    float miny = poly[0].y, maxy = poly[0].y;
    for (uint16_t i = 1; i < count; i++) {
        minx = poly[i].x < minx ? poly[i].x : minx;
        maxx = poly[i].x > maxx ? poly[i].x : maxx;
        miny = poly[i].y < miny ? poly[i].y : miny;
        maxy = poly[i].y > maxy ? poly[i].y : maxy;
    }

    int32_t ix0 = static_cast<int32_t>(std::floor(minx));
    int32_t iy0 = static_cast<int32_t>(std::floor(miny));
    int32_t ix1 = static_cast<int32_t>(std::ceil(maxx));
    int32_t iy1 = static_cast<int32_t>(std::ceil(maxy));

    if (ix0 < 0) ix0 = 0;
    if (iy0 < 0) iy0 = 0;
    if (ix1 > clip_w) ix1 = clip_w;
    if (iy1 > clip_h) iy1 = clip_h;
    if (ix1 <= ix0 || iy1 <= iy0) {
        return false;
    }

    x0 = static_cast<int16_t>(ix0);
    y0 = static_cast<int16_t>(iy0);
    w  = static_cast<uint16_t>(ix1 - ix0);
    h  = static_cast<uint16_t>(iy1 - iy0);
    return true;
}

Rasterizer::Rasterizer(float* scratch, size_t float_count)
    : _scratch(scratch), _capacity(float_count)
{
}

bool Rasterizer::fill(const Point* poly, uint16_t count, const Mask& mask)
{
    if (poly == nullptr || count < 3 || mask.data == nullptr || mask.empty()) {
        return false;
    }
    const size_t need = static_cast<size_t>(mask.w) + count;
    if (_scratch == nullptr || _capacity < need) {
        return false;
    }

    float* row       = _scratch;
    float* crossings = _scratch + mask.w;

    for (uint16_t y = 0; y < mask.h; y++) {
        std::memset(row, 0, sizeof(float) * mask.w);
        bool touched = false;

        for (uint8_t s = 0; s < kSubsamples; s++) {
            const float sy = static_cast<float>(mask.y0 + y) +
                             (static_cast<float>(s) + 0.5f) * kSubWeight;

            // Half-open [ymin, ymax) edge test: a vertex shared by two edges is
            // counted exactly once, which is what keeps parity consistent.
            uint16_t n = 0;
            for (uint16_t i = 0; i < count; i++) {
                const Point& a = poly[i];
                const Point& b = poly[(i + 1) % count];
                const float ay = a.y, by = b.y;
                if ((ay <= sy && by > sy) || (by <= sy && ay > sy)) {
                    const float t = (sy - ay) / (by - ay);
                    insertSorted(crossings, n, a.x + t * (b.x - a.x));
                    n++;
                }
            }
            if (n < 2) {
                continue;
            }

            for (uint16_t i = 0; i + 1 < n; i += 2) {
                float xa = crossings[i] - static_cast<float>(mask.x0);
                float xb = crossings[i + 1] - static_cast<float>(mask.x0);
                if (xb <= 0.0f || xa >= static_cast<float>(mask.w) || xb <= xa) {
                    continue;
                }
                if (xa < 0.0f) xa = 0.0f;
                if (xb > static_cast<float>(mask.w)) xb = static_cast<float>(mask.w);

                const int32_t first = static_cast<int32_t>(xa);
                const int32_t last  = static_cast<int32_t>(std::ceil(xb)) - 1;
                for (int32_t px = first; px <= last && px < mask.w; px++) {
                    if (px < 0) {
                        continue;
                    }
                    const float left  = static_cast<float>(px);
                    const float right = left + 1.0f;
                    const float lo    = xa > left ? xa : left;
                    const float hi    = xb < right ? xb : right;
                    if (hi > lo) {
                        row[px] += (hi - lo) * kSubWeight;
                        touched = true;
                    }
                }
            }
        }

        if (!touched) {
            continue;
        }
        uint8_t* dst = mask.row(y);
        for (uint16_t x = 0; x < mask.w; x++) {
            if (row[x] <= 0.0f) {
                continue;
            }
            int32_t v = static_cast<int32_t>(row[x] * 255.0f + 0.5f);
            if (v > 255) {
                v = 255;
            }
            // OR-combine so callers can accumulate several strokes in one mask.
            if (v > dst[x]) {
                dst[x] = static_cast<uint8_t>(v);
            }
        }
    }
    return true;
}

}  // namespace hz
