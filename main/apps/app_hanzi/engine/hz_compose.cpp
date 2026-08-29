/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include "hz_compose.h"
#include <cmath>
#include <cstring>

namespace hz {

namespace {

// Width, in progress units, of the soft ramp at the brush tip. composite() and
// advance() must agree on this or the dirty rectangle will clip the ramp.
constexpr int kSoftEdge = 3;

// Tian-zi-ge proportions, as a fraction of the cell.
constexpr float kGridMargin = 0.045f;
constexpr int kGridDash     = 7;  // on/off run length of the centre cross, px

inline uint8_t maxU8(uint8_t a, uint8_t b)
{
    return a > b ? a : b;
}

inline uint8_t minU8(uint8_t a, uint8_t b)
{
    return a < b ? a : b;
}

uint16_t lerp565(uint16_t from, uint16_t to, int t)
{
    const int r0 = (from >> 11) & 0x1F, g0 = (from >> 5) & 0x3F, b0 = from & 0x1F;
    const int r1 = (to >> 11) & 0x1F, g1 = (to >> 5) & 0x3F, b1 = to & 0x1F;
    const int r = r0 + ((r1 - r0) * t) / 255;
    const int g = g0 + ((g1 - g0) * t) / 255;
    const int b = b0 + ((b1 - b0) * t) / 255;
    return static_cast<uint16_t>((r << 11) | (g << 5) | b);
}

// Walks the median polyline to the point at a given arc length.
bool pointAtLength(const Stroke& st, float s, float& out_x, float& out_y)
{
    if (st.median_count < 2) {
        return false;
    }
    if (s <= 0.0f) {
        out_x = st.median[0].x;
        out_y = st.median[0].y;
        return true;
    }
    float walked = 0.0f;
    for (uint16_t i = 1; i < st.median_count; i++) {
        const Point& a = st.median[i - 1];
        const Point& b = st.median[i];
        const float dx = b.x - a.x;
        const float dy = b.y - a.y;
        const float seg = std::sqrt(dx * dx + dy * dy);
        if (seg <= 0.0f) {
            continue;
        }
        if (walked + seg >= s) {
            const float t = (s - walked) / seg;
            out_x = a.x + dx * t;
            out_y = a.y + dy * t;
            return true;
        }
        walked += seg;
    }
    out_x = st.median[st.median_count - 1].x;
    out_y = st.median[st.median_count - 1].y;
    return true;
}

}  // namespace

void Rect::unite(int16_t ax, int16_t ay, int16_t bx, int16_t by)
{
    if (bx <= ax || by <= ay) {
        return;
    }
    if (!valid()) {
        x = ax;
        y = ay;
        w = static_cast<uint16_t>(bx - ax);
        h = static_cast<uint16_t>(by - ay);
        return;
    }
    const int16_t x1 = static_cast<int16_t>(x + w);
    const int16_t y1 = static_cast<int16_t>(y + h);
    const int16_t nx = ax < x ? ax : x;
    const int16_t ny = ay < y ? ay : y;
    const int16_t mx = bx > x1 ? bx : x1;
    const int16_t my = by > y1 ? by : y1;
    x = nx;
    y = ny;
    w = static_cast<uint16_t>(mx - nx);
    h = static_cast<uint16_t>(my - ny);
}

void Rect::clip(int16_t limit_w, int16_t limit_h)
{
    if (!valid()) {
        return;
    }
    int32_t x1 = x + w;
    int32_t y1 = y + h;
    int32_t x0 = x < 0 ? 0 : x;
    int32_t y0 = y < 0 ? 0 : y;
    if (x1 > limit_w) x1 = limit_w;
    if (y1 > limit_h) y1 = limit_h;
    if (x1 <= x0 || y1 <= y0) {
        clear();
        return;
    }
    x = static_cast<int16_t>(x0);
    y = static_cast<int16_t>(y0);
    w = static_cast<uint16_t>(x1 - x0);
    h = static_cast<uint16_t>(y1 - y0);
}

bool Compositor::bind(const Buffers& buffers, const Palette& palette)
{
    if (buffers.canvas == nullptr || buffers.base == nullptr ||
        buffers.stroke == nullptr || buffers.reveal == nullptr || buffers.size == 0) {
        return false;
    }
    _bufs = buffers;
    _pal  = palette;
    buildLut();
    _stroke_bounds.clear();
    return true;
}

void Compositor::setPalette(const Palette& palette)
{
    _pal = palette;
    buildLut();
}

void Compositor::buildLut()
{
    for (int i = 0; i < 256; i++) {
        _lut[i] = lerp565(_pal.paper, _pal.ink, i);
    }
}

void Compositor::resetBase(bool draw_grid)
{
    const size_t px = static_cast<size_t>(_bufs.size) * _bufs.size;
    std::memset(_bufs.base, 0, px);
    std::memset(_bufs.reveal, 0, px);
    _stroke_bounds.clear();
    _stroke_active = false;
    _progress_now  = 0;
    if (!draw_grid) {
        return;
    }

    const int n      = _bufs.size;
    const int margin = static_cast<int>(n * kGridMargin);
    const int lo     = margin;
    const int hi     = n - margin - 1;
    const uint8_t a  = _pal.guide;

    for (int i = lo; i <= hi; i++) {
        _bufs.base[static_cast<size_t>(lo) * n + i] = a;
        _bufs.base[static_cast<size_t>(hi) * n + i] = a;
        _bufs.base[static_cast<size_t>(i) * n + lo] = a;
        _bufs.base[static_cast<size_t>(i) * n + hi] = a;
    }

    // Dashed centre cross: the guide children are taught to aim at.
    const int mid = n / 2;
    for (int i = lo; i <= hi; i++) {
        if (((i - lo) / kGridDash) % 2 != 0) {
            continue;
        }
        _bufs.base[static_cast<size_t>(mid) * n + i] = a;
        _bufs.base[static_cast<size_t>(i) * n + mid] = a;
    }
}

bool Compositor::addGhost(const Character& ch, Rasterizer& raster)
{
    const int n = _bufs.size;

    // Per stroke, touch only that stroke's bounding box: clear it, rasterise
    // into it, fold it into base. Doing the blend here rather than in one pass
    // at the end saves two full-canvas memsets and a full-canvas walk, which on
    // PSRAM is the bulk of the cost. max() makes overlapping strokes idempotent.
    for (uint16_t s = 0; s < ch.stroke_count; s++) {
        const Stroke& st = ch.strokes[s];
        Mask mask;
        if (!polygonBounds(st.outline, st.outline_count, static_cast<int16_t>(n),
                           static_cast<int16_t>(n), mask.x0, mask.y0, mask.w, mask.h)) {
            continue;
        }
        for (int y = mask.y0; y < mask.y0 + mask.h; y++) {
            std::memset(_bufs.stroke + static_cast<size_t>(y) * n + mask.x0, 0, mask.w);
        }

        mask.data   = _bufs.stroke + static_cast<size_t>(mask.y0) * n + mask.x0;
        mask.stride = static_cast<uint16_t>(n);
        if (!raster.fill(st.outline, st.outline_count, mask)) {
            return false;
        }

        for (int y = mask.y0; y < mask.y0 + mask.h; y++) {
            const size_t row      = static_cast<size_t>(y) * n;
            const uint8_t* srow   = _bufs.stroke + row;
            uint8_t* brow         = _bufs.base + row;
            for (int x = mask.x0; x < mask.x0 + mask.w; x++) {
                if (srow[x] == 0) {
                    continue;
                }
                const uint8_t faint =
                    static_cast<uint8_t>((static_cast<uint16_t>(srow[x]) * _pal.ghost) / 255);
                brow[x] = maxU8(brow[x], faint);
            }
        }
        for (int y = mask.y0; y < mask.y0 + mask.h; y++) {
            std::memset(_bufs.stroke + static_cast<size_t>(y) * n + mask.x0, 0, mask.w);
        }
    }

    _stroke_bounds.clear();
    return true;
}

void Compositor::snapshotBase()
{
    if (_bufs.rest == nullptr) {
        return;
    }
    std::memcpy(_bufs.rest, _bufs.base, static_cast<size_t>(_bufs.size) * _bufs.size);
}

bool Compositor::restoreBase()
{
    if (_bufs.rest == nullptr) {
        return false;
    }
    const size_t px = static_cast<size_t>(_bufs.size) * _bufs.size;
    // Rebuilding grid + ghost means decoding the character and rasterising
    // every stroke again -- ~100 ms on this board. A replay only needs the
    // pixels back, so copy them.
    std::memcpy(_bufs.base, _bufs.rest, px);
    std::memset(_bufs.reveal, 0, px);
    _stroke_bounds.clear();
    _stroke_active = false;
    _progress_now  = 0;
    return true;
}

void Compositor::repaintAll()
{
    Rect all;
    all.x = 0;
    all.y = 0;
    all.w = _bufs.size;
    all.h = _bufs.size;
    composite(all);
}

void Compositor::composite(const Rect& area)
{
    Rect r = area;
    r.clip(static_cast<int16_t>(_bufs.size), static_cast<int16_t>(_bufs.size));
    if (!r.valid()) {
        return;
    }
    const int n = _bufs.size;
    for (int y = r.y; y < r.y + r.h; y++) {
        const size_t row = static_cast<size_t>(y) * n;
        const uint8_t* base   = _bufs.base + row;
        const uint8_t* stroke = _bufs.stroke + row;
        const uint8_t* reveal = _bufs.reveal + row;
        uint16_t* dst         = _bufs.canvas + row;
        for (int x = r.x; x < r.x + r.w; x++) {
            uint8_t cur = 0;
            if (_stroke_active && stroke[x] != 0) {
                // reveal[] holds the pixel's position along the stroke. One
                // bucket of softness at the leading edge keeps the advancing
                // tip from looking stair-stepped.
                const int behind = static_cast<int>(_progress_now) - static_cast<int>(reveal[x]);
                if (behind >= 0) {
                    cur = stroke[x];
                } else if (behind >= -kSoftEdge) {
                    cur = static_cast<uint8_t>((stroke[x] * (kSoftEdge + 1 + behind)) /
                                               (kSoftEdge + 1));
                }
            }
            dst[x] = _lut[maxU8(base[x], cur)];
        }
    }
}

bool Compositor::beginStroke(const Character& ch, uint16_t index, Rasterizer& raster)
{
    if (index >= ch.stroke_count) {
        return false;
    }
    const int n      = _bufs.size;
    const Stroke& st = ch.strokes[index];

    Mask mask;
    if (!polygonBounds(st.outline, st.outline_count, static_cast<int16_t>(n),
                       static_cast<int16_t>(n), mask.x0, mask.y0, mask.w, mask.h)) {
        return false;
    }

    // Clearing the incoming stroke's box is what matters: anything left over
    // inside it would be min()-ed with the fresh outline and show up as ink
    // before the brush ever reaches it. The previous box is folded in only to
    // keep the planes tidy. Outside this union any residue is harmless, since
    // those strokes have already been burnt into base at full coverage.
    Rect clear_area = _stroke_bounds;
    clear_area.unite(mask.x0, mask.y0, static_cast<int16_t>(mask.x0 + mask.w),
                     static_cast<int16_t>(mask.y0 + mask.h));
    clear_area.clip(static_cast<int16_t>(n), static_cast<int16_t>(n));
    if (clear_area.valid()) {
        for (int y = clear_area.y; y < clear_area.y + clear_area.h; y++) {
            const size_t row = static_cast<size_t>(y) * n + clear_area.x;
            std::memset(_bufs.stroke + row, 0, clear_area.w);
            std::memset(_bufs.reveal + row, 0, clear_area.w);
        }
    }

    mask.data   = _bufs.stroke + static_cast<size_t>(mask.y0) * n + mask.x0;
    mask.stride = static_cast<uint16_t>(n);
    if (!raster.fill(st.outline, st.outline_count, mask)) {
        return false;
    }

    _stroke_bounds.x = mask.x0;
    _stroke_bounds.y = mask.y0;
    _stroke_bounds.w = mask.w;
    _stroke_bounds.h = mask.h;

    _progress_now = 0;
    _stroke_active = buildProgressMap(st);
    return _stroke_active;
}

// For every pixel covered by the stroke, find the closest point on the median
// polyline and record how far along the stroke that point is. Pixels are also
// tallied into buckets so advance() knows which region a step uncovers without
// rescanning the whole stroke.
bool Compositor::buildProgressMap(const Stroke& stroke)
{
    for (Rect& b : _bucket_bounds) {
        b.clear();
    }
    if (stroke.median_count < 2 || stroke.median_length <= 0.0f || !_stroke_bounds.valid()) {
        return false;
    }
    const int n = _bufs.size;

    for (int y = _stroke_bounds.y; y < _stroke_bounds.y + _stroke_bounds.h; y++) {
        const size_t row      = static_cast<size_t>(y) * n;
        const uint8_t* srow   = _bufs.stroke + row;
        uint8_t* prow         = _bufs.reveal + row;
        const float py        = static_cast<float>(y) + 0.5f;
        for (int x = _stroke_bounds.x; x < _stroke_bounds.x + _stroke_bounds.w; x++) {
            if (srow[x] == 0) {
                continue;
            }
            const float px = static_cast<float>(x) + 0.5f;

            float best_d2 = 3.4e38f;
            float best_s  = 0.0f;
            float walked  = 0.0f;
            for (uint16_t i = 1; i < stroke.median_count; i++) {
                const Point& a = stroke.median[i - 1];
                const Point& b = stroke.median[i];
                const float dx = b.x - a.x;
                const float dy = b.y - a.y;
                const float span = dx * dx + dy * dy;
                if (span <= 0.0f) {
                    continue;
                }
                float t = ((px - a.x) * dx + (py - a.y) * dy) / span;
                if (t < 0.0f) t = 0.0f;
                if (t > 1.0f) t = 1.0f;
                const float qx = px - (a.x + t * dx);
                const float qy = py - (a.y + t * dy);
                const float d2 = qx * qx + qy * qy;
                if (d2 < best_d2) {
                    best_d2 = d2;
                    best_s  = walked + t * std::sqrt(span);
                }
                walked += std::sqrt(span);
            }

            int q = static_cast<int>((best_s / stroke.median_length) * 255.0f + 0.5f);
            if (q < 0) q = 0;
            if (q > 255) q = 255;
            prow[x] = static_cast<uint8_t>(q);

            Rect& bucket = _bucket_bounds[(q * kBuckets) / 256];
            bucket.unite(static_cast<int16_t>(x), static_cast<int16_t>(y),
                         static_cast<int16_t>(x + 1), static_cast<int16_t>(y + 1));
        }
    }
    return true;
}

Rect Compositor::advance(const Stroke& stroke, float from_len, float to_len)
{
    Rect dirty;
    if (!_stroke_active || stroke.median_length <= 0.0f || to_len < from_len) {
        return dirty;
    }

    const float inv = 255.0f / stroke.median_length;
    int from_q      = static_cast<int>(from_len * inv);
    int to_q        = static_cast<int>(to_len * inv + 0.5f);
    if (from_q < 0) from_q = 0;
    if (to_q > 255) to_q = 255;
    if (to_q < from_q) to_q = from_q;

    // The leading edge is soft for kSoftEdge units *ahead* of the written
    // length, so those pixels change too and must be inside the dirty area.
    const int lo = (from_q > kSoftEdge ? from_q - kSoftEdge : 0);
    const int hi = (to_q + kSoftEdge < 255 ? to_q + kSoftEdge : 255);
    for (int b = (lo * kBuckets) / 256; b <= (hi * kBuckets) / 256 && b < kBuckets; b++) {
        const Rect& r = _bucket_bounds[b];
        if (r.valid()) {
            dirty.unite(r.x, r.y, static_cast<int16_t>(r.x + r.w),
                        static_cast<int16_t>(r.y + r.h));
        }
    }

    _progress_now = static_cast<uint8_t>(to_q);
    dirty.clip(static_cast<int16_t>(_bufs.size), static_cast<int16_t>(_bufs.size));
    composite(dirty);
    return dirty;
}

Rect Compositor::land()
{
    Rect area = _stroke_bounds;
    if (!area.valid()) {
        return area;
    }
    const int n = _bufs.size;
    for (int y = area.y; y < area.y + area.h; y++) {
        const size_t row      = static_cast<size_t>(y) * n;
        uint8_t* base         = _bufs.base + row;
        const uint8_t* stroke_row = _bufs.stroke + row;
        for (int x = area.x; x < area.x + area.w; x++) {
            base[x] = maxU8(base[x], stroke_row[x]);
        }
    }
    _stroke_active = false;
    composite(area);
    return area;
}

bool Compositor::renderStatic(const Character& ch, Rasterizer& raster, const Mask& target)
{
    // The glyph must already be transformed into the target's own coordinate
    // space, so the target starts at the origin. polygonBounds clamps to
    // [0, w) x [0, h); allowing a non-zero origin here would let a bound land
    // left of the buffer and write out of bounds.
    if (target.data == nullptr || target.empty() || target.x0 != 0 || target.y0 != 0) {
        return false;
    }
    for (uint16_t s = 0; s < ch.stroke_count; s++) {
        const Stroke& st = ch.strokes[s];
        Mask piece;
        if (!polygonBounds(st.outline, st.outline_count, static_cast<int16_t>(target.w),
                           static_cast<int16_t>(target.h), piece.x0, piece.y0, piece.w,
                           piece.h)) {
            continue;
        }
        piece.data   = target.data + static_cast<size_t>(piece.y0) * target.stride + piece.x0;
        piece.stride = target.stride;
        if (!raster.fill(st.outline, st.outline_count, piece)) {
            return false;
        }
    }
    return true;
}

}  // namespace hz
