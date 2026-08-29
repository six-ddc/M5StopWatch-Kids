/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include <esp_heap_caps.h>
#include <mooncake_log.h>
#include <cstring>
#include "view.h"

namespace view {

namespace {

constexpr const char* kTag = "AppHanzi";

constexpr size_t kArenaBytes = 12 * 1024;

void browseGestureCb(lv_event_t* e)
{
    auto* page      = static_cast<view::SearchPage*>(lv_event_get_user_data(e));
    lv_indev_t* dev = lv_indev_active();
    if (page == nullptr || dev == nullptr) {
        return;
    }
    const lv_dir_t dir = lv_indev_get_gesture_dir(dev);
    if (dir != LV_DIR_LEFT && dir != LV_DIR_RIGHT) {
        return;
    }
    // Swallow the rest of the press so the key under the finger does not
    // also fire a click on release.
    lv_indev_wait_release(dev);
    page->handleBrowseClicked();
}

}  // namespace

// Renders candidate glyphs the same way the browse grid does: decode into an
// arena, rasterise into the caller's A8 buffer. Captions come straight from
// the blob's pinyin strings.
class SearchPage::HanziPainter : public pime::GlyphPainter {
public:
    ~HanziPainter()
    {
        release();
    }

    bool init(const hz::DataSource* source, uint16_t max_glyph)
    {
        _src            = source;
        _scratch_floats = static_cast<size_t>(max_glyph) + 320;
        _arena_mem      = static_cast<uint8_t*>(heap_caps_malloc(kArenaBytes, MALLOC_CAP_SPIRAM));
        _scratch        = static_cast<float*>(heap_caps_malloc(_scratch_floats * sizeof(float), MALLOC_CAP_SPIRAM));
        if (_arena_mem == nullptr || _scratch == nullptr) {
            release();
            return false;
        }
        std::memset(_arena_mem, 0, kArenaBytes);
        std::memset(_scratch, 0, _scratch_floats * sizeof(float));
        _arena = hz::Arena(_arena_mem, kArenaBytes);
        return true;
    }

    void release()
    {
        if (_arena_mem != nullptr) {
            heap_caps_free(_arena_mem);
            _arena_mem = nullptr;
        }
        if (_scratch != nullptr) {
            heap_caps_free(_scratch);
            _scratch = nullptr;
        }
        _arena = hz::Arena();
        _src   = nullptr;
    }

    bool paint(uint16_t id, uint8_t* buffer, uint16_t w, uint16_t h) override
    {
        if (_src == nullptr || _scratch == nullptr || id >= _src->charCount()) {
            return false;
        }
        hz::Transform tf;
        tf.scale = (static_cast<float>(w) / static_cast<float>(_src->coordScale())) * 0.94f;
        tf.ox    = 0.5f * (w - _src->coordScale() * tf.scale);
        tf.oy    = 0.5f * (h - _src->coordScale() * tf.scale);

        _arena.reset();
        hz::Character ch;
        if (!_src->decode(id, tf, ch, _arena)) {
            mclog::tagWarn(kTag, "search: decode failed for {}", id);
            return false;
        }
        hz::Rasterizer raster(_scratch, _scratch_floats);
        hz::Mask mask;
        mask.data   = buffer;
        mask.x0     = 0;
        mask.y0     = 0;
        mask.w      = w;
        mask.h      = h;
        mask.stride = w;
        hz::Compositor::renderStatic(ch, raster, mask);
        return true;
    }

    const char* caption(uint16_t id) const override
    {
        return _src != nullptr ? _src->pinyinAt(id) : nullptr;
    }

private:
    const hz::DataSource* _src = nullptr;
    uint8_t* _arena_mem        = nullptr;
    float* _scratch            = nullptr;
    size_t _scratch_floats     = 0;
    hz::Arena _arena;
};

SearchPage::SearchPage() = default;

SearchPage::~SearchPage()
{
    destroy();
}

bool SearchPage::create(lv_obj_t* parent, const hz::DataSource* source, const pime::CandidateSource* engine,
                        BrowseCallback on_browse)
{
    if (source == nullptr || !source->valid() || engine == nullptr) {
        return false;
    }
    _on_browse = std::move(on_browse);
    _painter   = std::make_unique<HanziPainter>();
    if (!_painter->init(source, 64) || !_ime.create(parent, engine, _painter.get())) {
        _ime.destroy();
        _painter.reset();
        return false;
    }

    if (_on_browse) {
        // Mode switch: a horizontal swipe anywhere on the page goes to the
        // textbook browse mode (phone-style panel gesture; a corner button
        // was tried and looked like clutter next to the keypad). The bubble
        // flag must be cleared on the root: LVGL delivers a gesture to the
        // first ancestor of the pressed object without it, or drops the
        // event entirely when the walk runs past the screen.
        lv_obj_clear_flag(_ime.root(), LV_OBJ_FLAG_GESTURE_BUBBLE);
        lv_obj_add_event_cb(_ime.root(), browseGestureCb, LV_EVENT_GESTURE, this);
    }
    return true;
}

void SearchPage::handleBrowseClicked()
{
    if (!_on_browse) {
        return;
    }
    GetHAL().vibrate(20);
    _on_browse();
}

void SearchPage::destroy()
{
    _ime.destroy();
    _painter.reset();
}

void SearchPage::setHidden(bool hidden)
{
    _ime.setHidden(hidden);
}

void SearchPage::nextCandidatePage()
{
    _ime.nextCandidatePage();
}

void SearchPage::previousCandidatePage()
{
    _ime.previousCandidatePage();
}

bool SearchPage::takePick(uint16_t& order, char* reading, size_t cap)
{
    return _ime.takePick(order, reading, cap);
}

}  // namespace view
