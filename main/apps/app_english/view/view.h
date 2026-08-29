/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once
#include <hal/hal.h>
// lvgl.h does not pull in the image cache API, but the pages need
// lv_image_cache_drop(): they reuse one descriptor per image slot, and on the
// device (1 MB cache) a stale decode would redraw the previous picture. The
// host sim has the cache disabled, so this is the difference that would
// otherwise only show up on hardware.
#include <src/misc/cache/lv_cache.h>
#include <cstdint>
#include "../data/eng_data.h"
#include "../game/session.h"

/**
 * @brief The English app's four screens: unit picker, flashcard, quiz, result.
 *
 * Same conventions as the other two apps: pages are created once, shown and
 * hidden rather than rebuilt, and every method here must be called with the
 * LVGL lock held.
 *
 * Geometry is set by the panel and by where the buttons are, not by taste.
 * The glass is 466 px across (radius 233) and the two buttons sit on the bezel
 * at roughly 10 and 2 o'clock, so the quiz puts its two pictures under them --
 * pressing the left ear and picking the left picture are the same gesture.
 *
 * That constraint is what fixes the picture size at 144 px. Two 152 px cards
 * centred at (+/-84, -40) put their outer corners 198 px from centre with 16 px
 * of black between them. At 160 px the same layout throws a corner to 248 px,
 * off the glass, and closes the gap to 4 px. tools/english_host_test/sim
 * asserts the first half of that automatically.
 *
 * Pictures are LV_COLOR_FORMAT_I4: 16 colours, palette immediately ahead of
 * the pixels, decoded by LVGL's core. That is deliberate -- the host simulator
 * builds with TJPGD and LODEPNG switched off, so an indexed bitmap is the only
 * thing that renders identically there and on the device.
 *
 * Why eng_view and not view: the other three apps all put their pages in a
 * shared `view` namespace and rely on the class names not colliding. That runs
 * out here -- a quiz app naturally wants QuizPage, ResultPage and Summary, and
 * app_math already has all three. main.cpp includes every app's header, so the
 * clash is a hard build error rather than something to be careful about. The
 * host simulator does not catch it, because each sim links only one app's view.
 */
namespace eng_view {

/// Shared palette. Pure black field: an unlit pixel on this AMOLED emits
/// nothing, which is the single biggest thing the UI does for young eyes.
constexpr uint32_t kInk         = 0xF2F0EA;
constexpr uint32_t kDim         = 0x8A8A88;
constexpr uint32_t kCardBg      = 0x141414;
constexpr uint32_t kCardBorder  = 0x3C3C3C;
constexpr uint32_t kRightBorder = 0x4CC66A;
constexpr uint32_t kRightBg     = 0x0E2A14;
constexpr uint32_t kWrongBorder = 0xE0574B;
constexpr uint32_t kWrongBg     = 0x2A0E0E;
constexpr uint32_t kTrackColor  = 0x1A1A1A;
/// The English app's own hue, so a glance at the screen says which app this is
/// without reading anything. Vermilion is the stroke-order app, blue is
/// arithmetic; this is the mint green that goes with neither.
constexpr uint32_t kAccent = 0x3DDC97;

/// Fills a lv_image_dsc_t from a blob image so it can be handed to LVGL.
/// The descriptor points straight into .rodata -- nothing is copied.
void fillImageDsc(lv_image_dsc_t& dsc, const eng::Image& image);

/**
 * @brief Picks a unit. Five tiles laid out around the middle of the glass.
 *
 * A steps the selection, B starts it, and tapping a tile does both. Mirrors
 * the arithmetic map page closely enough that a child who has used one knows
 * this one.
 */
class UnitPage {
public:
    ~UnitPage();

    bool create(lv_obj_t* parent);
    void destroy();
    void setHidden(bool hidden);

    /// `best_stars` is one 0..3 rating per unit, drawn as dots under the name.
    void show(const eng::Data& data, uint16_t selected, const uint8_t* best_stars);

    /// One-shot read of a tap. `unit` is a **global** unit index, or -1 for
    /// "start the selected one" (the centre).
    bool takeTap(int16_t& unit);
    void clearTap();
    void handleClicked(int16_t unit);

private:
    /// Tiles that fit on the ring at once. The word list is much longer than
    /// this (40 units and growing), so show() pages: it draws the page holding
    /// the selected unit, and stepping the selection past the edge turns the
    /// page by itself. Eight keeps each tile readable and finger-sized.
    static constexpr uint8_t kTilesPerPage = 8;

    lv_obj_t* _root                  = nullptr;
    lv_obj_t* _tiles[kTilesPerPage]  = {};
    lv_obj_t* _labels[kTilesPerPage] = {};
    /// The centre's best-rating dots. They belong to whichever unit is
    /// selected, so there is one set, not one per tile.
    lv_obj_t* _dots[3] = {};
    lv_obj_t* _title   = nullptr;
    /// "12/40" -- without it, paging just looks like the tiles changing.
    lv_obj_t* _counter = nullptr;
    /// Invisible "start the chosen one" target filling the middle of the ring.
    lv_obj_t* _centre = nullptr;
    // One hint per button, each under the button it names. The two buttons sit
    // on the bezel like ears at 10 and 2 o'clock, so a single shared line at
    // the bottom of the screen points at nothing.
    lv_obj_t* _hint_a = nullptr;
    lv_obj_t* _hint_b = nullptr;
    uint8_t _shown    = 0;

    bool _tap_pending = false;
    int16_t _tap_unit = -1;
};

/**
 * @brief One word, looked at rather than answered: picture, word, gloss.
 *
 * There is no scoring here and nothing to get wrong. The whole unit goes past
 * once like this before any question is asked, which is the only part of the
 * round a child who knows none of the words can still succeed at.
 */
class CardPage {
public:
    ~CardPage();

    bool create(lv_obj_t* parent);
    void destroy();
    void setHidden(bool hidden);

    void show(const eng::Data& data, uint16_t word, uint8_t index, uint8_t total);

    /// Tapping anywhere on the page replays the audio; the app decides what
    /// that means.
    bool takeTap();
    void clearTap();
    void handleClicked();

private:
    lv_obj_t* _root  = nullptr;
    lv_obj_t* _arc   = nullptr;
    lv_obj_t* _image = nullptr;
    lv_obj_t* _word  = nullptr;
    lv_obj_t* _gloss = nullptr;
    // One hint per button, each under the button it names. The two buttons sit
    // on the bezel like ears at 10 and 2 o'clock, so a single shared line at
    // the bottom of the screen points at nothing.
    lv_obj_t* _hint_a = nullptr;
    lv_obj_t* _hint_b = nullptr;

    lv_image_dsc_t _dsc{};
    bool _tap_pending = false;
};

/**
 * @brief Two pictures, two buttons.
 *
 * ListenPick plays the word and shows nothing written -- the prompt line is a
 * speaker glyph, so a pre-reader can still win. ReadPick puts the written word
 * under the pictures instead.
 */
class QuizPage {
public:
    ~QuizPage();

    bool create(lv_obj_t* parent);
    void destroy();
    void setHidden(bool hidden);

    void show(const eng::Data& data, const eng::Question& question, uint8_t index, uint8_t total, uint8_t streak);

    /// Paints the outcome. On a miss the correct picture is outlined too, and
    /// the word is spelled out under both -- the pause exists so there is
    /// something to read.
    void showFeedback(const eng::Data& data, const eng::Question& question, bool correct, bool picked_left);

    bool takeTap(bool& picked_left);
    /// One-shot read of "play it again". Only ever set on a listening
    /// question -- see handleRootClicked().
    bool takeReplayTap();
    void clearTap();
    void handleCardClicked(bool left);
    void handleRootClicked();

private:
    lv_obj_t* _root      = nullptr;
    lv_obj_t* _arc       = nullptr;
    lv_obj_t* _status    = nullptr;
    lv_obj_t* _prompt    = nullptr;
    lv_obj_t* _hint      = nullptr;
    lv_obj_t* _cards[2]  = {nullptr, nullptr};  // [0] left, [1] right
    lv_obj_t* _images[2] = {nullptr, nullptr};

    lv_image_dsc_t _dsc[2]{};
    bool _tap_pending = false;
    bool _tap_left    = false;
    /// Tapping off the cards asks to hear the word again. Kept apart from
    /// _tap_pending because it is not an answer and must not be mistaken for
    /// one.
    bool _replay_pending = false;
    /// Whether the question on screen is a listening one. A reading question
    /// deliberately has no replay: the written word is the question, and
    /// saying it out loud would turn it back into a listening question.
    bool _listen = false;

    void resetCardStyles();
};

struct Summary {
    uint8_t correct     = 0;
    uint8_t total       = 0;
    uint8_t stars       = 0;
    const char* verdict = nullptr;
    const char* unit    = nullptr;
    /// Up to three words to look at again, already resolved to text.
    const char* missed[3] = {nullptr, nullptr, nullptr};
    uint8_t missed_count  = 0;
};

class ResultPage {
public:
    ~ResultPage();

    bool create(lv_obj_t* parent);
    void destroy();
    void setHidden(bool hidden);

    /// Draws the page with the stars unlit; the app reveals them one at a time.
    void beginSummary(const Summary& summary);
    void revealStar(uint8_t index);
    /// Skip straight to the finished state.
    void finishSummary(const Summary& summary);

    bool takeTap();
    void clearTap();
    void handleClicked();

private:
    lv_obj_t* _root    = nullptr;
    lv_obj_t* _dots[3] = {nullptr, nullptr, nullptr};
    lv_obj_t* _score   = nullptr;
    lv_obj_t* _verdict = nullptr;
    lv_obj_t* _missed  = nullptr;
    // One hint per button, each under the button it names. The two buttons sit
    // on the bezel like ears at 10 and 2 o'clock, so a single shared line at
    // the bottom of the screen points at nothing.
    lv_obj_t* _hint_a = nullptr;
    lv_obj_t* _hint_b = nullptr;

    bool _tap_pending = false;
};

}  // namespace eng_view
