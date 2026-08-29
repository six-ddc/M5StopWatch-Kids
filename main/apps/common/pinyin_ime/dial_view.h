/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once
#include <hal/hal.h>
#include <cstdint>
#include "t9_engine.h"

// Reusable alphabet-dial pinyin input for the round screen: 26 letters on a
// ring, exact-prefix typing (no T9 ambiguity layer), candidates in the
// centre. It talks to a CandidateSource for lookups and to a GlyphPainter
// for drawing candidates, so it carries no knowledge of the stroke engine
// and any app can host it.
//
// Interaction model (designed for 5-8 year olds):
//  * Letters that cannot continue the typed prefix dim out and stop taking
//    hits; a touch snaps to the nearest bright letter, so effective targets
//    grow as a syllable progresses.
//  * Press-and-slide on the ring floats a magnifier over the armed letter,
//    ticking a short buzz per letter crossed (the rotary-dial feel); the
//    letter commits on release.
//  * The last-typed letter keeps its ink colour plus a small dot: tapping it
//    again takes it back (pinyin has no doubled letters, which
//    hanzi_logic_test pins as an invariant). Tapping the echo also deletes;
//    holding it clears.

namespace pime {

// Draws one candidate into an A8 coverage buffer (stride == w, zeroed by the
// caller) and captions it. The hanzi app backs this with the stroke engine;
// a different host could use a font or bitmaps.
class GlyphPainter {
public:
    virtual ~GlyphPainter()                                                  = default;
    virtual bool paint(uint16_t id, uint8_t* buffer, uint16_t w, uint16_t h) = 0;
    // Toned reading(s) of `id`, space-separated, primary first; nullptr for
    // no caption. The view shows the reading matching the typed prefix.
    virtual const char* caption(uint16_t id) const = 0;
};

// Page-object conventions follow view::BrowsePage: create() fails clean,
// the object lives for the app's lifetime and toggles with setHidden().
class DialView {
public:
    ~DialView();

    bool create(lv_obj_t* parent, const CandidateSource* source, GlyphPainter* painter);
    void destroy();
    void setHidden(bool hidden);
    // Back to the empty state (input, selection and paging cleared).
    void reset();

    // Physical-key routing; call with the LVGL lock held.
    void nextCandidatePage();
    void previousCandidatePage();

    // One-shot pick flag, same contract as the tap flags in the math and
    // english views. `reading` (optional) receives the toned reading the
    // candidate was picked under.
    bool takePick(uint16_t& id, char* reading = nullptr, size_t cap = 0);

    // The widget's full-screen root, for hosts that add their own gestures.
    lv_obj_t* root() const
    {
        return _root;
    }
    // True while a ring press is in flight; hosts should ignore their own
    // page gestures then (a slide along the ring is letter scrubbing, not a
    // swipe).
    bool ringActive() const
    {
        return _armed >= 0;
    }

    // Invoked from the LVGL event callbacks (and driven directly by the host
    // sim); not part of the page's own API.
    void handleRingPress(int32_t x, int32_t y);
    void handleRingMove(int32_t x, int32_t y);
    void handleRingRelease();
    void handleRingAbort();
    void handleEchoShort();
    void handleEchoLong();
    void handleCandidate(uint8_t cell);

    // Direct-drive helpers for the host sim (validity enforced).
    bool typeLetter(char c);
    void deleteLetter();
    const char* prefix() const
    {
        return _prefix;
    }

private:
    static constexpr uint8_t kLetters    = 26;
    static constexpr uint8_t kCandCells  = 9;  // honeycomb 5 + 4
    static constexpr uint8_t kRow0       = 5;
    static constexpr uint16_t kCandGlyph = 52;
    static constexpr uint8_t kMaxLen     = 6;  // longest syllable: zhuang

    bool allocate();
    void release();
    void refresh();  // validity + centre + chrome
    void refreshRing();
    void refreshCandidates();
    // Lays out the committed prefix plus the grey in-flight letter: while a
    // ring press is armed the pending letter previews behind the echo (the
    // finger hides the ring, the echo is always visible); arming the recall
    // letter greys the last committed one instead. Release turns it white.
    void layoutEcho();
    // Nearest eligible slot within the snap window, or -1.
    int8_t slotForPoint(int32_t x, int32_t y) const;
    void armSlot(int8_t slot);
    bool eligible(uint8_t slot) const;
    void matchedReading(uint16_t id, char* out, size_t cap) const;
    void flyBack(char letter);

    const CandidateSource* _source = nullptr;
    GlyphPainter* _painter         = nullptr;

    lv_obj_t* _root              = nullptr;
    lv_obj_t* _echo              = nullptr;
    lv_obj_t* _pending           = nullptr;
    lv_obj_t* _empty_hint        = nullptr;
    lv_obj_t* _page_label        = nullptr;
    lv_obj_t* _recall_dot        = nullptr;
    lv_obj_t* _letters[kLetters] = {};

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

    char _prefix[kMaxLen + 1] = {};
    uint8_t _len              = 0;
    bool _valid[kLetters]     = {};
    int8_t _armed             = -1;  // ring slot under the finger, -1 idle
    uint16_t _page            = 0;
    uint16_t _cand_total      = 0;

    bool _pick_pending     = false;
    uint16_t _pick_id      = 0;
    char _pick_reading[16] = {};
};

}  // namespace pime
