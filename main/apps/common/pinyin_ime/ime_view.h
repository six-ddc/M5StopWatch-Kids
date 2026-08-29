/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once
#include <hal/hal.h>
#include <cstdint>
#include "t9_engine.h"

// Reusable T9 pinyin input widget for the round screen: an eight-key letter
// pad plus delete, a row of pinyin-interpretation chips, and a strip of
// character candidates. It talks to a CandidateSource for lookups and to a
// GlyphPainter for drawing candidates, so it carries no knowledge of the
// stroke engine (or any other glyph source) and any app can host it.
//
// Interaction model (designed for 5-8 year olds): every key press refreshes
// both layers immediately; the best interpretation is pre-selected so a child
// can ignore the chips entirely; a key that would lead nowhere is rejected on
// the spot (buzz + red flash) instead of entering an error state.

namespace pime {

// Draws one candidate into an A8 coverage buffer (stride == w, zeroed by the
// caller) and captions it. The hanzi app backs this with the stroke engine;
// a different host could use a font or bitmaps.
class GlyphPainter {
public:
    virtual ~GlyphPainter()                                                  = default;
    virtual bool paint(uint16_t id, uint8_t* buffer, uint16_t w, uint16_t h) = 0;
    // Toned reading(s) of `id`, space-separated, primary first; nullptr for
    // no caption. The view shows the reading that matches the current
    // interpretation.
    virtual const char* caption(uint16_t id) const = 0;
};

// Page-object conventions follow view::BrowsePage: create() fails clean,
// the object lives for the app's lifetime and toggles with setHidden().
class ImeView {
public:
    ~ImeView();

    bool create(lv_obj_t* parent, const CandidateSource* source, GlyphPainter* painter);
    void destroy();
    void setHidden(bool hidden);
    // Back to the empty state (input, selection and paging cleared).
    void reset();

    // Physical-key routing; call with the LVGL lock held.
    void nextCandidatePage();
    void previousCandidatePage();

    // One-shot pick flag, same contract as the tap flags in the math and
    // english views: the LVGL callback only records the pick, the app
    // consumes it from onRunning() under the lock. `reading` (optional)
    // receives the toned reading the candidate was picked under -- for a
    // heteronym that is the reading the child actually searched, which the
    // host should display rather than the primary one.
    bool takePick(uint16_t& id, char* reading = nullptr, size_t cap = 0);

    // The widget's full-screen root, for hosts that add their own chrome
    // (e.g. a corner button) on top of the keypad page.
    lv_obj_t* root() const
    {
        return _root;
    }

    // Invoked from the LVGL event callbacks (and driven directly by the host
    // sim); not part of the page's own API.
    void handleLetterKey(uint8_t group);  // 0..7 -> keys 2..9
    void handleDeleteShort();
    void handleDeleteLong();
    void handleInterpChip(uint8_t slot);
    void handleCandidate(uint8_t cell);

private:
    static constexpr uint8_t kLetterKeys  = 8;
    static constexpr uint8_t kMaxDigits   = 6;  // longest syllable: zhuang
    static constexpr uint8_t kInterpSlots = 4;
    static constexpr uint8_t kCandCells   = 5;
    static constexpr uint16_t kCandGlyph  = 64;

    bool allocate();
    void release();
    void refresh();  // both layers + chrome visibility
    void refreshInterps();
    void refreshCandidates();
    void rejectCue(lv_obj_t* key);
    // Reading of `id` that matches the selected interpretation (primary
    // reading as fallback), copied into `out`.
    void matchedReading(uint16_t id, char* out, size_t cap) const;

    const CandidateSource* _source = nullptr;
    GlyphPainter* _painter         = nullptr;

    lv_obj_t* _root                  = nullptr;
    lv_obj_t* _hint_a                = nullptr;
    lv_obj_t* _hint_b                = nullptr;
    lv_obj_t* _page_label            = nullptr;
    lv_obj_t* _empty_hint            = nullptr;
    lv_obj_t* _keys[kLetterKeys + 1] = {};  // [kLetterKeys] is delete

    struct InterpChip {
        lv_obj_t* chip  = nullptr;
        lv_obj_t* label = nullptr;
        int8_t more_dir = 0;  // 0: a real chip; +1/-1: "..." paging fwd/back
        uint16_t index  = 0;  // interpretation index when more_dir == 0
    };
    InterpChip _interp_chips[kInterpSlots];

    struct Cand {
        lv_obj_t* chip    = nullptr;
        lv_obj_t* image   = nullptr;
        lv_obj_t* caption = nullptr;
        uint8_t* buffer   = nullptr;
        lv_image_dsc_t dsc{};
        uint16_t id   = 0;
        bool occupied = false;
    };
    Cand _cands[kCandCells];

    char _digits[kMaxDigits + 1]                = {};
    uint8_t _len                                = 0;
    const char* _interps[T9Engine::kMaxInterps] = {};
    uint16_t _interp_total                      = 0;
    uint16_t _interp_base                       = 0;  // window start when there are more than fit
    uint16_t _selected                          = 0;  // absolute index into the interpretation list
    uint16_t _page                              = 0;
    uint16_t _cand_total                        = 0;

    bool _pick_pending     = false;
    uint16_t _pick_id      = 0;
    char _pick_reading[16] = {};
};

}  // namespace pime
