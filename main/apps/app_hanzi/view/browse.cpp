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

// A 3x3 grid 320 px across keeps the corner cells inside the 466 px circle:
// their outermost glyph corner sits ~214 px from the centre, radius is 233.
constexpr int16_t kGrid      = 320;
constexpr int16_t kCellPitch = kGrid / 3;
constexpr int16_t kGlyph     = 88;
constexpr uint8_t kColumns   = 3;

constexpr size_t kArenaBytes    = 12 * 1024;
constexpr size_t kScratchFloats = kGlyph + 320;

void cellClickedCb(lv_event_t* e);
void searchGestureCb(lv_event_t* e);

}  // namespace

BrowsePage::~BrowsePage()
{
    destroy();
}

bool BrowsePage::allocate()
{
    const size_t glyph_px = static_cast<size_t>(kGlyph) * kGlyph;
    for (Cell& c : _cells) {
        c.buffer = static_cast<uint8_t*>(heap_caps_malloc(glyph_px, MALLOC_CAP_SPIRAM));
        if (c.buffer == nullptr) {
            return false;
        }
        std::memset(c.buffer, 0, glyph_px);
    }
    _arena_mem = static_cast<uint8_t*>(heap_caps_malloc(kArenaBytes, MALLOC_CAP_SPIRAM));
    _scratch   = static_cast<float*>(heap_caps_malloc(kScratchFloats * sizeof(float), MALLOC_CAP_SPIRAM));
    if (_arena_mem == nullptr || _scratch == nullptr) {
        return false;
    }
    std::memset(_arena_mem, 0, kArenaBytes);
    std::memset(_scratch, 0, kScratchFloats * sizeof(float));
    _arena = hz::Arena(_arena_mem, kArenaBytes);
    return true;
}

void BrowsePage::release()
{
    for (Cell& c : _cells) {
        if (c.buffer != nullptr) {
            lv_image_cache_drop(&c.dsc);
            heap_caps_free(c.buffer);
            c.buffer = nullptr;
        }
    }
    if (_arena_mem != nullptr) {
        heap_caps_free(_arena_mem);
        _arena_mem = nullptr;
    }
    if (_scratch != nullptr) {
        heap_caps_free(_scratch);
        _scratch = nullptr;
    }
    _arena = hz::Arena();
}

bool BrowsePage::create(lv_obj_t* parent, const hz::DataSource* source, SelectCallback on_select,
                        SearchCallback on_search)
{
    if (source == nullptr || !source->valid() || !allocate()) {
        release();
        return false;
    }
    _src       = source;
    _on_select = std::move(on_select);
    _on_search = std::move(on_search);

    _root = lv_obj_create(parent);
    lv_obj_remove_style_all(_root);
    lv_obj_set_size(_root, LV_PCT(100), LV_PCT(100));
    lv_obj_center(_root);
    lv_obj_clear_flag(_root, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(_root, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(_root, LV_OPA_COVER, 0);

    _title = lv_label_create(_root);
    lv_obj_set_style_text_font(_title, &lv_font_hanzi_ui_24, 0);
    lv_obj_set_style_text_color(_title, lv_color_hex(0xF0EFEA), 0);
    lv_obj_set_style_text_align(_title, LV_TEXT_ALIGN_CENTER, 0);
    // The circle narrows to ~242 px at the label's top edge. Pinning the height
    // to one line is what makes LONG_DOT elide instead of wrapping.
    lv_obj_set_width(_title, 230);
    lv_obj_set_height(_title, 34);
    lv_label_set_long_mode(_title, LV_LABEL_LONG_DOT);
    lv_obj_align(_title, LV_ALIGN_TOP_MID, 0, 34);

    _footer = lv_label_create(_root);
    lv_obj_set_style_text_font(_footer, &lv_font_hanzi_ui_24, 0);
    lv_obj_set_style_text_color(_footer, lv_color_hex(0x8A8A88), 0);
    lv_obj_align(_footer, LV_ALIGN_BOTTOM_MID, 0, -34);

    if (_on_search) {
        // Mode switch: a horizontal swipe anywhere on the page goes to the
        // pinyin search (phone-style panel gesture; a corner button was
        // tried and looked like clutter). Cell taps are unaffected -- the
        // gesture only fires past LVGL's movement threshold, and the
        // handler swallows the rest of the press.
        //
        // LVGL delivers a gesture to the first ancestor of the pressed
        // object WITHOUT the bubble flag -- with the flag set everywhere
        // (the default) the walk runs past the screen and the event is
        // dropped. Clearing it here makes this root the gesture target.
        lv_obj_clear_flag(_root, LV_OBJ_FLAG_GESTURE_BUBBLE);
        lv_obj_add_event_cb(_root, searchGestureCb, LV_EVENT_GESTURE, this);
    }

    for (uint8_t i = 0; i < kCells; i++) {
        Cell& c = _cells[i];
        std::memset(&c.dsc, 0, sizeof(c.dsc));
        c.dsc.header.magic  = LV_IMAGE_HEADER_MAGIC;
        c.dsc.header.cf     = LV_COLOR_FORMAT_A8;
        c.dsc.header.w      = kGlyph;
        c.dsc.header.h      = kGlyph;
        c.dsc.header.stride = kGlyph;
        c.dsc.data_size     = static_cast<uint32_t>(kGlyph) * kGlyph;
        c.dsc.data          = c.buffer;

        c.image = lv_image_create(_root);
        lv_image_set_src(c.image, &c.dsc);
        // A8 carries coverage only; recolour paints it in ink.
        lv_obj_set_style_image_recolor(c.image, lv_color_hex(0xF2F0EA), 0);
        lv_obj_set_style_image_recolor_opa(c.image, LV_OPA_COVER, 0);
        lv_obj_align(c.image, LV_ALIGN_CENTER, static_cast<int16_t>((i % kColumns - 1) * kCellPitch),
                     static_cast<int16_t>((i / kColumns - 1) * kCellPitch));
        lv_obj_add_flag(c.image, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(c.image, cellClickedCb, LV_EVENT_CLICKED, this);
        lv_obj_set_user_data(c.image, reinterpret_cast<void*>(static_cast<uintptr_t>(i)));
    }

    showLesson(0, 0);
    return true;
}

void BrowsePage::destroy()
{
    if (_root != nullptr) {
        lv_obj_delete(_root);
        _root   = nullptr;
        _title  = nullptr;
        _footer = nullptr;
        for (Cell& c : _cells) {
            c.image = nullptr;
        }
    }
    release();
    _src = nullptr;
}

void BrowsePage::setHidden(bool hidden)
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

void BrowsePage::refresh()
{
    if (_src == nullptr || _root == nullptr) {
        return;
    }
    const hz::DataSource::Lesson lsn = _src->lessonAt(_lesson);
    const uint16_t pages             = static_cast<uint16_t>((lsn.char_count + kCells - 1) / kCells);
    // A zero-character lesson would leave _page untouched and make the
    // `char_count - _page * kCells` below wrap around as unsigned.
    _page                = (pages == 0) ? 0 : (_page >= pages ? static_cast<uint16_t>(pages - 1) : _page);
    const uint16_t first = static_cast<uint16_t>(lsn.first_char + _page * kCells);

    lv_label_set_text(_title, lsn.title);
    char footer[48];
    if (pages > 1) {
        std::snprintf(footer, sizeof(footer), "%u/%u  %u/%u", _lesson + 1, _src->lessonCount(), _page + 1, pages);
    } else {
        std::snprintf(footer, sizeof(footer), "%u/%u", _lesson + 1, _src->lessonCount());
    }
    lv_label_set_text(_footer, footer);

    const size_t glyph_px = static_cast<size_t>(kGlyph) * kGlyph;
    hz::Transform tf;
    tf.scale = (static_cast<float>(kGlyph) / static_cast<float>(_src->coordScale())) * 0.94f;
    tf.ox    = 0.5f * (kGlyph - _src->coordScale() * tf.scale);
    tf.oy    = tf.ox;

    // Lessons are often shorter than nine characters, so the grid adapts:
    // a four-character lesson reads as a centred 2x2, not as 3 + 1 pushed to
    // the top of a circular screen.
    const uint16_t remaining = static_cast<uint16_t>(lsn.char_count - _page * kCells);
    const uint8_t count      = static_cast<uint8_t>(remaining < kCells ? remaining : kCells);
    const uint8_t cols       = count > 4 ? 3 : (count > 1 ? 2 : 1);
    const uint8_t rows       = static_cast<uint8_t>((count + cols - 1) / cols);
    const int16_t pitch      = cols >= 3 ? kCellPitch : 128;

    hz::Rasterizer raster(_scratch, kScratchFloats);
    for (uint8_t i = 0; i < kCells; i++) {
        Cell& c              = _cells[i];
        const uint16_t index = static_cast<uint16_t>(first + i);
        c.occupied           = i < count && index < _src->charCount();

        std::memset(c.buffer, 0, glyph_px);
        if (!c.occupied) {
            lv_obj_add_flag(c.image, LV_OBJ_FLAG_HIDDEN);
            lv_image_cache_drop(&c.dsc);
            continue;
        }
        lv_obj_clear_flag(c.image, LV_OBJ_FLAG_HIDDEN);
        c.order = index;

        const uint8_t row       = static_cast<uint8_t>(i / cols);
        const uint8_t col       = static_cast<uint8_t>(i % cols);
        const uint8_t row_width = static_cast<uint8_t>((count - row * cols) < cols ? (count - row * cols) : cols);
        lv_obj_align(c.image, LV_ALIGN_CENTER, static_cast<int16_t>((col - (row_width - 1) * 0.5f) * pitch),
                     static_cast<int16_t>((row - (rows - 1) * 0.5f) * pitch));

        _arena.reset();
        hz::Character ch;
        if (_src->decode(index, tf, ch, _arena)) {
            hz::Mask mask;
            mask.data   = c.buffer;
            mask.x0     = 0;
            mask.y0     = 0;
            mask.w      = kGlyph;
            mask.h      = kGlyph;
            mask.stride = kGlyph;
            hz::Compositor::renderStatic(ch, raster, mask);
        } else {
            mclog::tagWarn(kTag, "browse: decode failed for {}", index);
        }
        // The buffer is reused in place, so the cached decoded image must go.
        lv_image_cache_drop(&c.dsc);
        lv_obj_invalidate(c.image);
    }
}

void BrowsePage::showLesson(uint16_t lesson, uint16_t page)
{
    if (_src == nullptr || _src->lessonCount() == 0) {
        return;
    }
    _lesson = static_cast<uint16_t>(lesson % _src->lessonCount());
    _page   = page;
    refresh();
}

void BrowsePage::nextPage()
{
    if (_src == nullptr) {
        return;
    }
    const hz::DataSource::Lesson lsn = _src->lessonAt(_lesson);
    const uint16_t pages             = static_cast<uint16_t>((lsn.char_count + kCells - 1) / kCells);
    if (_page + 1 < pages) {
        _page++;
        refresh();
    } else {
        showLesson(static_cast<uint16_t>(_lesson + 1), 0);
    }
}

void BrowsePage::previousPage()
{
    if (_src == nullptr) {
        return;
    }
    if (_page > 0) {
        _page--;
        refresh();
        return;
    }
    const uint16_t prev              = static_cast<uint16_t>((_lesson + _src->lessonCount() - 1) % _src->lessonCount());
    const hz::DataSource::Lesson lsn = _src->lessonAt(prev);
    const uint16_t pages             = static_cast<uint16_t>((lsn.char_count + kCells - 1) / kCells);
    showLesson(prev, pages > 0 ? static_cast<uint16_t>(pages - 1) : 0);
}

void BrowsePage::focusCharacter(uint16_t order)
{
    if (_src == nullptr) {
        return;
    }
    const int32_t lesson = _src->lessonOfChar(order);
    if (lesson < 0) {
        return;
    }
    const hz::DataSource::Lesson lsn = _src->lessonAt(static_cast<uint16_t>(lesson));
    const uint16_t offset            = static_cast<uint16_t>(order - lsn.first_char);
    showLesson(static_cast<uint16_t>(lesson), static_cast<uint16_t>(offset / kCells));
}

void BrowsePage::handleCellClicked(uint8_t index)
{
    if (index >= kCells || !_cells[index].occupied || !_on_select) {
        return;
    }
    GetHAL().vibrate(20);
    _on_select(_cells[index].order);
}

void BrowsePage::handleSearchClicked()
{
    if (!_on_search) {
        return;
    }
    GetHAL().vibrate(20);
    _on_search();
}

namespace {

void cellClickedCb(lv_event_t* e)
{
    auto* page = static_cast<BrowsePage*>(lv_event_get_user_data(e));
    auto* obj  = static_cast<lv_obj_t*>(lv_event_get_target(e));
    if (page == nullptr || obj == nullptr) {
        return;
    }
    const auto index = static_cast<uint8_t>(reinterpret_cast<uintptr_t>(lv_obj_get_user_data(obj)));
    page->handleCellClicked(index);
}

void searchGestureCb(lv_event_t* e)
{
    auto* page      = static_cast<BrowsePage*>(lv_event_get_user_data(e));
    lv_indev_t* dev = lv_indev_active();
    if (page == nullptr || dev == nullptr) {
        return;
    }
    const lv_dir_t dir = lv_indev_get_gesture_dir(dev);
    if (dir != LV_DIR_LEFT && dir != LV_DIR_RIGHT) {
        return;
    }
    // Swallow the rest of the press so the cell under the finger does not
    // also fire a click on release.
    lv_indev_wait_release(dev);
    page->handleSearchClicked();
}

}  // namespace

}  // namespace view
