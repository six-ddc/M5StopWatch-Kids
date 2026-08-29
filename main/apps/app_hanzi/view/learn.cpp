/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include "view.h"
#include <assets/assets.h>
#include <esp_heap_caps.h>
#include <misc/cache/instance/lv_image_cache.h>
#include <mooncake_log.h>
#include <cstdio>
#include <cstring>

namespace view {

namespace {

constexpr const char* kTag = "AppHanzi";

// The 466 px circle inscribes a 329 px square. 300 leaves 83 px above and
// below the cell, which is what the pinyin and the footer need in order to sit
// clear of it and still stay inside the bezel.
constexpr uint16_t kCanvas = 300;
// Leaves a margin between the glyph and the grid lines, like a real copybook.
constexpr float kGlyphInset = 0.88f;

constexpr size_t kArenaBytes    = 12 * 1024;  // measured peak is 5936 B
constexpr size_t kScratchFloats = kCanvas + 320;

inline uint16_t rgb565(uint8_t r, uint8_t g, uint8_t b)
{
    return static_cast<uint16_t>(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
}

void* allocPsram(size_t bytes)
{
    void* p = heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM);
    if (p != nullptr) {
        // heap_caps_malloc does not zero: uninitialised PSRAM shows up as
        // random speckle in never-drawn pixels on real hardware.
        std::memset(p, 0, bytes);
    }
    return p;
}

void rootClickedCb(lv_event_t* e)
{
    auto* page = static_cast<LearnPage*>(lv_event_get_user_data(e));
    if (page != nullptr) {
        page->replay();
    }
}

}  // namespace

LearnPage::~LearnPage()
{
    destroy();
}

bool LearnPage::allocate()
{
    const size_t px = static_cast<size_t>(kCanvas) * kCanvas;
    _canvas_buf = static_cast<uint16_t*>(allocPsram(px * sizeof(uint16_t)));
    _base       = static_cast<uint8_t*>(allocPsram(px));
    _base_rest  = static_cast<uint8_t*>(allocPsram(px));
    _stroke     = static_cast<uint8_t*>(allocPsram(px));
    _reveal     = static_cast<uint8_t*>(allocPsram(px));
    _arena_mem  = static_cast<uint8_t*>(allocPsram(kArenaBytes));
    _scratch    = static_cast<float*>(allocPsram(kScratchFloats * sizeof(float)));

    if (_canvas_buf == nullptr || _base == nullptr || _base_rest == nullptr ||
        _stroke == nullptr || _reveal == nullptr || _arena_mem == nullptr ||
        _scratch == nullptr) {
        mclog::tagError(kTag, "PSRAM allocation failed");
        release();
        return false;
    }
    _arena = hz::Arena(_arena_mem, kArenaBytes);
    return true;
}

void LearnPage::release()
{
    for (void* p : {static_cast<void*>(_canvas_buf), static_cast<void*>(_base),
                    static_cast<void*>(_base_rest), static_cast<void*>(_stroke),
                    static_cast<void*>(_reveal),
                    static_cast<void*>(_arena_mem), static_cast<void*>(_scratch)}) {
        if (p != nullptr) {
            heap_caps_free(p);
        }
    }
    _canvas_buf = nullptr;
    _base       = nullptr;
    _base_rest  = nullptr;
    _stroke     = nullptr;
    _reveal     = nullptr;
    _arena_mem  = nullptr;
    _scratch    = nullptr;
    _arena      = hz::Arena();
}

bool LearnPage::create(lv_obj_t* parent, const hz::DataSource* source)
{
    if (source == nullptr || !source->valid() || !allocate()) {
        return false;
    }
    _src = source;

    hz::Buffers bufs;
    bufs.canvas = _canvas_buf;
    bufs.base   = _base;
    bufs.rest   = _base_rest;
    bufs.stroke = _stroke;
    bufs.reveal = _reveal;
    bufs.size   = kCanvas;

    hz::Palette pal;
    // Ink on black rather than paper white: it matches the rest of the system
    // UI, and on this AMOLED the background costs no light at all.
    pal.paper = rgb565(0, 0, 0);
    pal.ink   = rgb565(242, 240, 234);
    pal.ghost = 30;
    pal.guide = 38;
    if (!_comp.bind(bufs, pal)) {
        release();
        return false;
    }

    _root = lv_obj_create(parent);
    lv_obj_remove_style_all(_root);
    lv_obj_set_size(_root, LV_PCT(100), LV_PCT(100));
    lv_obj_center(_root);
    lv_obj_clear_flag(_root, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(_root, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(_root, LV_OPA_COVER, 0);

    // The whole page replays the animation, not just the 300 px grid it is
    // drawn in. Nothing else here is clickable, so there is no reason to make
    // a child land inside the square to get the stroke order again.
    lv_obj_add_flag(_root, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(_root, rootClickedCb, LV_EVENT_CLICKED, this);

    _canvas = lv_canvas_create(_root);
    if (_canvas == nullptr) {
        release();
        return false;
    }
    lv_canvas_set_buffer(_canvas, _canvas_buf, kCanvas, kCanvas, LV_COLOR_FORMAT_RGB565);
    lv_obj_center(_canvas);

    _pinyin = lv_label_create(_root);
    lv_obj_set_style_text_font(_pinyin, &lv_font_hanzi_pinyin_44, 0);
    lv_obj_set_style_text_color(_pinyin, lv_color_hex(0xF2F0EA), 0);
    lv_obj_align(_pinyin, LV_ALIGN_TOP_MID, 0, 18);

    _ready = true;
    return showCharacter(0);
}

void LearnPage::destroy()
{
    if (_root != nullptr) {
        lv_obj_delete(_root);
        _root   = nullptr;
        _canvas = nullptr;
        _pinyin = nullptr;
    }
    release();
    _src   = nullptr;
    _ready = false;
}

void LearnPage::setHidden(bool hidden)
{
    if (_root == nullptr) {
        return;
    }
    if (hidden) {
        lv_obj_add_flag(_root, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_clear_flag(_root, LV_OBJ_FLAG_HIDDEN);
    }
}

void LearnPage::updateLabels()
{
    if (_src == nullptr || _pinyin == nullptr) {
        return;
    }
    lv_label_set_text(_pinyin, _src->pinyinAt(_order));
}

bool LearnPage::rebuild(bool decode_again)
{
    if (!decode_again && _char.stroke_count > 0 && _comp.restoreBase()) {
        // Same character, fresh run: the grid and ghost are already correct.
        const uint32_t t = GetHAL().millis();
        _comp.repaintAll();
        if (_canvas != nullptr) {
            dropCanvasCache();
            lv_obj_invalidate(_canvas);
        }
        hz::AnimConfig cfg;
        _anim.begin(&_char, cfg);
        const uint32_t took = GetHAL().millis() - t;
        if (took > 20) {
            mclog::tagWarn(kTag, "restore {}ms", took);
        }
        return true;
    }

    _arena.reset();
    _char = hz::Character{};

    hz::Transform tf;
    tf.scale = (static_cast<float>(kCanvas) / static_cast<float>(_src->coordScale())) *
               kGlyphInset;
    tf.ox = 0.5f * (static_cast<float>(kCanvas) - _src->coordScale() * tf.scale);
    tf.oy = tf.ox;

    if (!_src->decode(_order, tf, _char, _arena)) {
        mclog::tagError(kTag, "decode failed for order {}{}", _order,
                        _arena.overflowed() ? " (arena overflow)" : "");
        return false;
    }

    const uint32_t t0 = GetHAL().millis();
    hz::Rasterizer raster(_scratch, kScratchFloats);
    _comp.resetBase(true);
    const uint32_t t1 = GetHAL().millis();
    _comp.addGhost(_char, raster);
    const uint32_t t2 = GetHAL().millis();
    _comp.snapshotBase();
    _comp.repaintAll();
    const uint32_t t3 = GetHAL().millis();
    if (t3 - t0 > 20) {
        mclog::tagWarn(kTag, "rebuild {}ms (reset {} ghost {} repaint {})", t3 - t0, t1 - t0,
                       t2 - t1, t3 - t2);
    }
    if (_canvas != nullptr) {
        dropCanvasCache();
        lv_obj_invalidate(_canvas);
    }

    hz::AnimConfig cfg;
    _anim.begin(&_char, cfg);
    updateLabels();
    return true;
}

bool LearnPage::showCharacter(uint16_t order)
{
    if (!_ready || _src == nullptr || order >= _src->charCount()) {
        return false;
    }
    // Roll back on failure: _order feeds saveProgress() and the browse page, so
    // leaving it pointing at a character that never rendered would persist a
    // broken position to NVS and resurrect it on the next boot.
    const uint16_t previous = _order;
    _order                  = order;
    if (!rebuild()) {
        _order = previous;
        return false;
    }
    return true;
}

void LearnPage::next()
{
    if (_src != nullptr && _src->charCount() > 0) {
        showCharacter(static_cast<uint16_t>((_order + 1) % _src->charCount()));
    }
}

void LearnPage::previous()
{
    if (_src != nullptr && _src->charCount() > 0) {
        showCharacter(static_cast<uint16_t>((_order + _src->charCount() - 1) %
                                            _src->charCount()));
    }
}

void LearnPage::replay()
{
    if (!_ready) {
        return;
    }
    rebuild(false);
    GetHAL().vibrate(20);
}

void LearnPage::dropCanvasCache()
{
    if (_canvas == nullptr) {
        return;
    }
    // A canvas is drawn as an image and therefore goes through LVGL's image
    // cache. Writing the buffer behind LVGL's back leaves a stale entry there,
    // so the widget keeps painting the previous frame -- the animation appears
    // frozen while the redraw cost is still paid. lv_canvas_set_buffer() drops
    // the entry for us; direct writes have to do it themselves.
    lv_image_cache_drop(lv_canvas_get_image(_canvas));
}

void LearnPage::invalidateRect(const hz::Rect& area)
{
    if (_canvas == nullptr || !area.valid()) {
        return;
    }
    dropCanvasCache();
    lv_area_t coords;
    lv_obj_get_coords(_canvas, &coords);
    lv_area_t dirty;
    dirty.x1 = coords.x1 + area.x;
    dirty.y1 = coords.y1 + area.y;
    dirty.x2 = dirty.x1 + area.w - 1;
    dirty.y2 = dirty.y1 + area.h - 1;
    // One merged rectangle per frame: every invalidated area costs a full
    // render pass in LVGL.
    lv_obj_invalidate_area(_canvas, &dirty);
}

void LearnPage::update(uint32_t dt_ms)
{
    if (!_ready || _char.stroke_count == 0) {
        return;
    }

    const hz::Phase before = _anim.phase();
    const bool changed     = _anim.tick(dt_ms);

    if (before == hz::Phase::Done && _anim.phase() == hz::Phase::Intro) {
        // Looped back to the start: restore the grid + ghost snapshot rather
        // than rebuilding it, which would stall the frame for ~100 ms.
        _comp.restoreBase();
        _comp.repaintAll();
        dropCanvasCache();
        lv_obj_invalidate(_canvas);
    }

    if (_anim.strokeJustStarted()) {
        hz::Rasterizer raster(_scratch, kScratchFloats);
        _comp.beginStroke(_char, _anim.strokeIndex(), raster);
    }
    // hasActiveStroke(), not phase()==Reveal: tick() flips the phase to Pause
    // inside the very call that finishes a stroke, so testing the phase
    // afterwards would skip the final advance and let land() snap the last few
    // pixels of the tail into place instead of brushing them on.
    if (changed && _anim.hasActiveStroke()) {
        invalidateRect(_comp.advance(_char.strokes[_anim.strokeIndex()], _anim.revealFrom(),
                                     _anim.revealTo()));
    }
    if (_anim.strokeJustLanded()) {
        invalidateRect(_comp.land());
        // No haptic per stroke: the vibrator is driven over the shared I2C bus
        // (set duty + read back to verify), and at eight strokes a character
        // that traffic starves the button polling in the main loop, which is
        // what caps the animation's frame rate. The end-of-character cue is
        // enough feedback anyway.
    }
    if (_anim.charJustCompleted()) {
        // Audio synthesis is not free, so it happens outside the LVGL lock.
        _char_completed = true;
    }
}

bool LearnPage::takeCharCompleted()
{
    const bool value = _char_completed;
    _char_completed  = false;
    return value;
}

}  // namespace view
