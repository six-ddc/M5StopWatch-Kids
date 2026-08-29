/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once
#include <hal/hal.h>
#include <cstdint>
#include <functional>
#include "../engine/hz_anim.h"
#include "../engine/hz_compose.h"
#include "../engine/hz_data.h"

namespace view {

// Grid of characters for one lesson. Cells are rendered with the stroke engine
// into A8 coverage images rather than a large font, which keeps 1037 glyphs out
// of flash.
class BrowsePage {
public:
    using SelectCallback = std::function<void(uint16_t order)>;

    ~BrowsePage();

    bool create(lv_obj_t* parent, const hz::DataSource* source, SelectCallback on_select);
    void destroy();

    void setHidden(bool hidden);
    void showLesson(uint16_t lesson, uint16_t page = 0);
    void nextPage();
    void previousPage();
    // Jumps to the page containing a teaching-order index.
    void focusCharacter(uint16_t order);

    uint16_t lesson() const
    {
        return _lesson;
    }

    // Invoked from the LVGL click handler; not part of the page's own API.
    void handleCellClicked(uint8_t index);

private:
    static constexpr uint8_t kCells = 9;

    bool allocate();
    void release();
    void refresh();

    const hz::DataSource* _src = nullptr;
    SelectCallback _on_select;

    lv_obj_t* _root   = nullptr;
    lv_obj_t* _title  = nullptr;
    lv_obj_t* _footer = nullptr;

    struct Cell {
        lv_obj_t* image = nullptr;
        uint8_t* buffer = nullptr;
        lv_image_dsc_t dsc{};
        uint16_t order  = 0;
        bool occupied   = false;
    };
    Cell _cells[kCells];

    uint8_t* _arena_mem = nullptr;
    float* _scratch     = nullptr;
    hz::Arena _arena;

    uint16_t _lesson = 0;
    uint16_t _page   = 0;
};

// The writing page: one tian-zi-ge canvas playing the stroke order of a single
// character. All pixel buffers live in PSRAM and are owned by this object.
class LearnPage {
public:
    ~LearnPage();

    bool create(lv_obj_t* parent, const hz::DataSource* source);
    void destroy();

    void setHidden(bool hidden);
    bool showCharacter(uint16_t order);
    void next();
    void previous();
    void replay();

    // Advances playback and redraws what changed. Must be called with the LVGL
    // lock held: it writes into the live canvas buffer.
    void update(uint32_t dt_ms);

    uint16_t order() const
    {
        return _order;
    }
    bool ready() const
    {
        return _ready;
    }
    // One-shot flag set when a character finishes. Consumed outside the LVGL
    // lock so the caller can play a sound without stalling the UI thread.
    bool takeCharCompleted();


private:
    bool allocate();
    void release();
    bool rebuild(bool decode_again = true);
    void updateLabels();
    void dropCanvasCache();
    void invalidateRect(const hz::Rect& area);

    const hz::DataSource* _src = nullptr;
    lv_obj_t* _root            = nullptr;
    lv_obj_t* _canvas          = nullptr;
    lv_obj_t* _pinyin          = nullptr;
    uint16_t* _canvas_buf      = nullptr;
    uint8_t* _base             = nullptr;
    uint8_t* _base_rest        = nullptr;
    uint8_t* _stroke           = nullptr;
    uint8_t* _reveal           = nullptr;
    uint8_t* _arena_mem        = nullptr;
    float* _scratch            = nullptr;

    hz::Arena _arena;
    hz::Character _char;
    hz::Compositor _comp;
    hz::Animator _anim;

    uint16_t _order       = 0;
    bool _ready           = false;
    bool _char_completed  = false;
};

}  // namespace view
