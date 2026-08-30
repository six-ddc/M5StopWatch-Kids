/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once
#include <apps/common/pinyin_ime/picker_view.h>
#include <hal/hal.h>
#include <cstdint>
#include <functional>
#include <memory>
#include "../engine/hz_anim.h"
#include "../engine/hz_compose.h"
#include "../engine/hz_data.h"

namespace view {

// Grid of characters for one lesson. Cells are rendered with the stroke engine
// into A8 coverage images rather than a large font, which keeps the whole
// character set out of flash.
class BrowsePage {
public:
    using SelectCallback = std::function<void(uint16_t order)>;
    using SearchCallback = std::function<void()>;

    ~BrowsePage();

    bool create(lv_obj_t* parent, const hz::DataSource* source, SelectCallback on_select,
                SearchCallback on_search = nullptr);
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

    // Invoked from the LVGL click handlers; not part of the page's own API.
    void handleCellClicked(uint8_t index);
    void handleSearchClicked();

private:
    static constexpr uint8_t kCells = 9;

    bool allocate();
    void release();
    void refresh();

    const hz::DataSource* _src = nullptr;
    SelectCallback _on_select;
    SearchCallback _on_search;

    lv_obj_t* _root   = nullptr;
    lv_obj_t* _title  = nullptr;
    lv_obj_t* _footer = nullptr;

    struct Cell {
        lv_obj_t* image = nullptr;
        uint8_t* buffer = nullptr;
        lv_image_dsc_t dsc{};
        uint16_t order = 0;
        bool occupied  = false;
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
    // `reading` (optional) overrides the displayed pinyin with the reading
    // the character was reached under (heteronyms searched by a secondary
    // reading should show that reading, not the primary). next()/previous()
    // clear the override.
    bool showCharacter(uint16_t order, const char* reading = nullptr);
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

    uint16_t _order           = 0;
    bool _ready               = false;
    bool _char_completed      = false;
    char _pinyin_override[16] = {};
};

// Pinyin lookup, the app's landing page: the reusable three-wheel syllable
// picker wired to the stroke engine. The page itself is thin glue -- it owns
// the GlyphPainter that renders candidates with hz::Compositor, wires the
// horizontal-swipe gesture that leads to the textbook browse mode, and
// forwards everything else to pime::PickerView.
class SearchPage {
public:
    using BrowseCallback = std::function<void()>;

    // Defined in search.cpp, where HanziPainter is a complete type (the
    // implicit versions would instantiate unique_ptr's deleter here).
    SearchPage();
    ~SearchPage();

    bool create(lv_obj_t* parent, const hz::DataSource* source, const pime::CandidateSource* engine,
                BrowseCallback on_browse = nullptr);
    void destroy();

    // Invoked from the LVGL click handler; not part of the page's own API.
    void handleBrowseClicked();
    void setHidden(bool hidden);

    // Dials the wheels onto this character's syllable, so opening the page
    // resumes at the last-learned character.
    void showCharacter(uint16_t order);

    // Physical-key routing (one detent down/up on the character wheel);
    // call with the LVGL lock held.
    void nextCandidatePage();
    void previousCandidatePage();
    // One-shot: the selection band was tapped; `order` is the dialled
    // character and `reading` (optional) the toned reading it was picked
    // under.
    bool takePick(uint16_t& order, char* reading = nullptr, size_t cap = 0);

    // The host sim drives input through this.
    pime::PickerView& picker()
    {
        return _picker;
    }

private:
    class HanziPainter;
    std::unique_ptr<HanziPainter> _painter;
    pime::PickerView _picker;
    BrowseCallback _on_browse;
};

}  // namespace view
