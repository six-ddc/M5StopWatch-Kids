/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once
#include <hal/hal.h>
#include <cstdint>
#include "glyph_painter.h"
#include "t9_engine.h"

// Reusable three-wheel pinyin picker for the round screen, in the iOS
// time-picker idiom: unit wheel | suffix wheel | character wheel, one
// horizontal selection band across all three. Reading the band left to right
// spells the syllable: h | ao | 好(hǎo). It talks to a CandidateSource for
// the (unit, suffix, candidate) enumeration and to a GlyphPainter for
// drawing characters, so it carries no knowledge of the stroke engine and
// any app can host it.
//
// Interaction model (designed for 5-8 year olds):
//  * There is no empty state: the wheels always dial a legal syllable with
//    at least one character under it (the enumeration is data-driven).
//  * Any wheel scrolls independently -- drag with inertia, detent snapping,
//    a short buzz per row crossed. Changing an outer wheel rebuilds the
//    wheels to its right once it settles, iOS date-picker style: the suffix
//    survives if still legal (h+ao -> g+ao), otherwise snaps to the nearest
//    in letter order; the character wheel returns to the best-known first.
//  * A light tap on a non-selected row scrolls that row into the band; a
//    tap on the band itself confirms the selected character.
//  * A bare unit that is a complete syllable (a / e / m / o) shows a
//    quiet middle dot in the suffix wheel: a | · | 啊.

namespace pime {

// Page-object conventions follow view::BrowsePage: create() fails clean,
// the object lives for the app's lifetime and toggles with setHidden().
class PickerView {
public:
    ~PickerView();

    bool create(lv_obj_t* parent, const CandidateSource* source, GlyphPainter* painter);
    void destroy();
    void setHidden(bool hidden);

    // Positions the wheels on the syllable and list position of `id`'s
    // primary reading, so opening the picker resumes at the last-learned
    // character. Falls back to the default syllable ("hao") when the id or
    // its reading is unusable.
    void showCharacter(uint16_t id);

    // Physical-key routing (one detent up/down on the character wheel);
    // call with the LVGL lock held.
    void candidateStep(int8_t dir);

    // One-shot pick flag, same contract as the tap flags in the math and
    // english views. `reading` (optional) receives the toned reading the
    // candidate was picked under.
    bool takePick(uint16_t& id, char* reading = nullptr, size_t cap = 0);

    // Mode-switch state carry (see view::SearchPage): the carrier is a
    // toneless letter prefix plus the selected candidate's id (-1 for none).
    // The wheels always dial a full syllable, so the export is never empty
    // and always carries an id. Importing an incomplete prefix completes it
    // to the first legal syllable in letter order under that prefix; an
    // empty prefix (the other modes have an empty state, a wheel does not)
    // lands on the default syllable.
    void exportState(char* prefix, size_t cap, int32_t& id) const;
    void importState(const char* prefix, int32_t id);

    // The widget's full-screen root, for hosts that add their own gestures.
    lv_obj_t* root() const
    {
        return _root;
    }
    // True while a press is steering a wheel (a grab of a moving wheel
    // included); hosts should ignore their own page gestures then (a
    // vertical drag is wheel scrolling, not a swipe).
    bool scrollActive() const
    {
        return _drag_wheel >= 0 && (_drag_steering || _press_caught);
    }

    // Invoked from the LVGL event callbacks (and driven directly by the host
    // sim); not part of the page's own API.
    void handlePress(int32_t x, int32_t y);
    void handleMove(int32_t x, int32_t y);
    void handleRelease(int32_t x, int32_t y);
    void handleAbort();

    // Direct-drive helpers for the host sim (validity enforced).
    bool selectSyllable(const char* syllable);
    const char* syllable() const
    {
        return _syllable;
    }
    uint16_t candidateIndex() const;
    uint16_t candidateCount() const
    {
        return _wheels[2].count;
    }
    float wheelOffset(uint8_t wheel) const;
    int16_t wheelX(uint8_t wheel) const;
    bool wheelSettled() const;  // no anim in flight on any wheel

private:
    static constexpr uint8_t kWheelCount = 3;
    static constexpr uint8_t kSlots      = 9;  // visible window: detent +/-4
    static constexpr uint16_t kGlyphPx   = 68;
    static constexpr size_t kMaxSyllable = 8;

    struct Wheel {
        PickerView* owner       = nullptr;
        uint8_t index           = 0;
        lv_obj_t* rows[kSlots]  = {};
        int32_t content[kSlots] = {};  // item index shown by each slot, -1 empty
        // Rendered tier of each slot: true = selected size (44 px font /
        // 68 px glyph), false = ring size (32 px / 48 px). Rows swap tiers
        // as they cross the band edge; no transforms are involved.
        bool selected_tier[kSlots] = {};
        uint16_t count             = 0;
        float offset               = 0.0f;  // fractional detent, unclamped while dragged
        float reveal               = 1.0f;  // rebuild fade-in factor
        int16_t x                  = 0;     // column centre, LV_ALIGN_CENTER offset
        int16_t width              = 0;     // selected-size content budget
        int16_t y_lim              = 0;     // chord-derived visibility limit
        int32_t detent             = 0;     // last haptic detent, for crossing ticks
        int32_t travel_from        = 0;     // detent when the gesture began
        bool snapping              = false;
        bool bouncing              = false;
        float bounce_home          = 0.0f;
    };

    bool allocate();
    void release();
    void layout();  // measures the columns and places the chrome

    void rebuildWheel(uint8_t w, uint16_t count, float offset, bool animate);
    void refreshWheel(uint8_t w, bool in_motion = false);
    void assignRow(Wheel& wh, uint8_t slot, int32_t item, bool selected);
    void settleWheel(uint8_t w, bool from_motion);
    void setUnit(uint16_t unit);
    void setSuffix(uint16_t suffix);
    void composeSyllable();
    uint16_t candidateAt(uint16_t index) const;
    void updateCaption();
    void matchedReading(uint16_t id, char* out, size_t cap) const;
    void confirmPick();
    // Scrolls the character wheel onto `id` if it is in the current list.
    void dialToCandidate(uint16_t id);

    int8_t wheelForPoint(int32_t x) const;
    float rubberOffset(const Wheel& wh) const;
    void startOffsetAnim(uint8_t w, float target, uint32_t duration_ms);
    void stopWheelAnims(uint8_t w);
    void scrollToRow(uint8_t w, int32_t row, bool from_key);
    void tickCrossings(Wheel& wh);

    static void offsetAnimCb(void* var, int32_t v);
    static void offsetAnimDoneCb(lv_anim_t* anim);
    static void revealAnimCb(void* var, int32_t v);
    static void flashAnimCb(void* var, int32_t v);

    const CandidateSource* _source = nullptr;
    GlyphPainter* _painter         = nullptr;

    lv_obj_t* _root    = nullptr;
    lv_obj_t* _band    = nullptr;
    lv_obj_t* _caption = nullptr;
    Wheel _wheels[kWheelCount];

    uint8_t* _glyph_buf[kSlots] = {};
    lv_image_dsc_t _glyph_dsc[kSlots];

    uint16_t _unit                   = 0;
    uint16_t _suffix                 = 0;
    char _syllable[kMaxSyllable + 1] = {};

    // gesture state
    int8_t _drag_wheel  = -1;
    bool _drag_steering = false;  // finger moved beyond the tap slop
    bool _press_caught  = false;  // press landed on a moving wheel (a catch):
                                  // the release never counts as a tap
    int32_t _press_x = 0, _press_y = 0;
    float _press_offset    = 0.0f;
    int32_t _last_y        = 0;
    uint32_t _last_ms      = 0;
    uint32_t _press_ms     = 0;
    float _velocity        = 0.0f;  // px per ms, filtered
    uint32_t _last_tick_ms = 0;     // haptic tick rate limiting

    bool _pick_pending     = false;
    uint16_t _pick_id      = 0;
    char _pick_reading[16] = {};
};

}  // namespace pime
