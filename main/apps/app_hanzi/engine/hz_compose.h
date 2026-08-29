/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once
#include <cstdint>
#include "hz_data.h"
#include "hz_raster.h"

// Layer composition for the writing canvas.
//
// Three coverage planes plus one RGB565 output:
//   base    grid + ghost outline + strokes already written
//   stroke  outline of the stroke currently being written
//   reveal  how far the brush has travelled along that stroke
// The visible ink of a pixel is max(base, min(stroke, reveal)). Everything is
// the same ink colour, so combining with max (rather than alpha-over) keeps
// anti-aliased edges from darkening when layers overlap.
//
// Buffers are caller-owned: PSRAM allocation belongs to the view layer, which
// keeps this file host-testable.

namespace hz {

struct Rect {
    int16_t x  = 0;
    int16_t y  = 0;
    uint16_t w = 0;
    uint16_t h = 0;

    bool valid() const
    {
        return w > 0 && h > 0;
    }
    void clear()
    {
        x = y = 0;
        w = h = 0;
    }
    void unite(int16_t ax, int16_t ay, int16_t bx, int16_t by);
    void clip(int16_t limit_w, int16_t limit_h);
};

struct Palette {
    uint16_t paper     = 0xFFFF;  // RGB565 background
    uint16_t ink       = 0x0000;  // RGB565 stroke colour
    uint8_t ghost      = 36;      // coverage of not-yet-written strokes
    uint8_t guide      = 44;      // coverage of the tian-zi-ge lines
};

struct Buffers {
    uint16_t* canvas = nullptr;  // size*size RGB565
    uint8_t* base    = nullptr;  // size*size A8
    uint8_t* rest    = nullptr;  // size*size A8, snapshot of grid + ghost
    uint8_t* stroke  = nullptr;  // size*size A8
    uint8_t* reveal  = nullptr;  // size*size, arc-length progress map (see below)
    uint16_t size    = 0;
};

class Compositor {
public:
    bool bind(const Buffers& buffers, const Palette& palette);
    void setPalette(const Palette& palette);
    uint16_t size() const
    {
        return _bufs.size;
    }

    // Resets base to the guide grid only. Pass draw_grid=false for a bare cell.
    void resetBase(bool draw_grid);
    // Adds the faint full-character outline to base.
    bool addGhost(const Character& ch, Rasterizer& raster);
    // Saves base (grid + ghost) so a replay can return to it without decoding
    // and rasterising the character again.
    void snapshotBase();
    // Restores that snapshot and clears any stroke in progress.
    bool restoreBase();
    // Repaints the whole canvas from the current layers.
    void repaintAll();

    // Rasterises one stroke and builds its progress map: for every pixel of the
    // stroke, how far along the median that pixel sits (0..255 of the stroke's
    // arc length). Writing then means revealing every pixel whose progress is
    // behind the brush, which is what keeps a hooked stroke (the 心 bottom, for
    // one) from filling its outer corner late -- a round brush travelling the
    // median simply cannot reach that corner, however large its radius.
    bool beginStroke(const Character& ch, uint16_t index, Rasterizer& raster);
    // Advances the written arc length and composites what that uncovers.
    Rect advance(const Stroke& stroke, float from_len, float to_len);
    // Burns the stroke set up by beginStroke() into base at full ink and
    // repaints its area.
    Rect land();

    // Composites an arbitrary region (used after external changes).
    void composite(const Rect& area);

    // Renders a whole character statically at full ink, for browse-page cells.
    static bool renderStatic(const Character& ch, Rasterizer& raster, const Mask& target);

private:
    // Progress is quantised to 0..255 over the stroke; the map is bucketed so a
    // frame only has to repaint the buckets it just crossed.
    static constexpr uint8_t kBuckets = 32;

    void buildLut();
    bool buildProgressMap(const Stroke& stroke);

    Buffers _bufs;
    Palette _pal;
    Rect _stroke_bounds;
    Rect _bucket_bounds[kBuckets];
    uint8_t _progress_now = 0;
    bool _stroke_active   = false;
    uint16_t _lut[256]    = {0};
};

}  // namespace hz
