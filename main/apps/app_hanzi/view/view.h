/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once
#include <apps/common/pinyin_ime/dial_view.h>
#include <apps/common/pinyin_ime/ime_view.h>
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

// Pinyin lookup, the app's landing page, hosting three interchangeable input
// modes over one stroke-engine-backed GlyphPainter: the three-wheel syllable
// picker, the 26-letter dial and the T9 keypad. A wide horizontal swipe
// cycles the modes (left = next, right = previous) and the current input
// state rides along as a toneless letter prefix plus the selected
// candidate's id. The page is thin glue: it routes the app-facing API to
// whichever mode is live, so the app never learns which input paradigm the
// child prefers -- it only persists the choice.
class SearchPage {
public:
    enum class Mode : uint8_t {
        Picker = 0,
        Dial   = 1,
        Keypad = 2,
    };
    static constexpr uint8_t kModeCount = 3;

    // Defined in search.cpp, where HanziPainter is a complete type (the
    // implicit versions would instantiate unique_ptr's deleter here).
    SearchPage();
    ~SearchPage();

    bool create(lv_obj_t* parent, const hz::DataSource* source, const pime::T9Engine* engine);
    void destroy();
    void setHidden(bool hidden);

    // Dials the current mode onto this character's syllable, so opening the
    // page resumes at the last-learned character.
    void showCharacter(uint16_t order);

    // Physical-key routing (character wheel detent on the picker, candidate
    // page on the other modes); call with the LVGL lock held.
    void nextCandidatePage();
    void previousCandidatePage();
    // One-shot: a candidate was confirmed in whichever mode; `order` is the
    // character and `reading` (optional) the toned reading it was picked
    // under.
    bool takePick(uint16_t& order, char* reading = nullptr, size_t cap = 0);

    // Mode restore/persist plumbing. setMode() switches instantly (state
    // still carries over) and does not mark the mode dirty -- it restores
    // the NVS value at onOpen. takeModeDirty() is the one-shot the app
    // drains from onRunning, where writing NVS is allowed.
    void setMode(uint8_t mode);
    uint8_t mode() const
    {
        return static_cast<uint8_t>(_mode);
    }
    bool takeModeDirty(uint8_t& mode);

    // Invoked from the LVGL gesture callback; not part of the page's own
    // API. `dir` is +1 for the next mode (swipe left), -1 for the previous.
    void handleModeSwipe(int8_t dir);
    // True while the live mode owns the current press (wheel steering, ring
    // scrubbing, a resting finger on a pad key) or a slide is in flight; the
    // gesture callback asks before switching.
    bool gestureBusy() const;

    // The host sim drives input through these.
    pime::PickerView& picker()
    {
        return _picker;
    }
    pime::DialView& dial()
    {
        return _dial;
    }
    pime::ImeView& keypad()
    {
        return _ime;
    }

private:
    class HanziPainter;

    lv_obj_t* modeRoot(Mode m);
    void applyVisibility();
    void carryState(Mode from, Mode to);
    void switchMode(Mode to, int8_t dir, bool animate);

    static void slideExecCb(void* obj, int32_t v);
    static void slideOutDoneCb(lv_anim_t* anim);
    static void slideInDoneCb(lv_anim_t* anim);

    std::unique_ptr<HanziPainter> _painter;
    pime::PickerView _picker;
    pime::DialView _dial;
    pime::ImeView _ime;
    Mode _mode       = Mode::Picker;
    bool _mode_dirty = false;
    bool _sliding    = false;
    bool _hidden     = false;
};

}  // namespace view
