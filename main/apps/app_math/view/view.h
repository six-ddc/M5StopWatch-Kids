/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once
#include <hal/hal.h>
#include <cstdint>
#include "../game/economy.h"
#include "../game/problem.h"
#include "../game/session.h"

/**
 * @brief The arithmetic game's three screens: quiz, result, level map.
 *
 * All follow the same convention as the stroke-order app's pages: created once
 * up front, shown and hidden rather than destroyed, and every method that
 * touches LVGL must be called with the lock held.
 *
 * Layout is pinned to the 466 px round panel (radius 233), and to where the
 * buttons physically are: they sit like ears at roughly 10 and 2 o'clock, so
 * the answer cards go directly beneath them. Card corners land 215 px from
 * centre; tools/math_host_test/sim asserts that nothing lit escapes the
 * circle, which is how the equation overflow and a missing-glyph bug were
 * both caught before reaching hardware.
 *
 * Animation discipline: while a question is on screen everything holds still
 * (this display is stared at by a small child). One-shot pulses are reserved
 * for milestones -- crossing a streak colour, a star landing, the dust
 * carrying into a star -- and never loop.
 */
namespace view {

class QuizPage {
public:
    ~QuizPage();

    bool create(lv_obj_t* parent);
    void destroy();
    void setHidden(bool hidden);

    /// Draws a problem and clears any feedback styling from the previous one.
    /// Judge problems render the completed equation in the smaller digit font
    /// (the 96 px face cannot fit "50 + 40 = 90" on the glass) with 对 / 错 on
    /// the cards; the gold (tenth) question gets a golden status line.
    void showProblem(const math::Problem& problem, uint8_t index, uint8_t total,
                     uint8_t streak, bool in_retry, bool is_gold);

    /// Paints the outcome: the chosen card turns green or red, and on a miss
    /// the correct card is outlined too so the child sees what it should have
    /// been. A judge equation is rewritten to its true form -- that line is
    /// the lesson. When the streak crosses a colour milestone the ring changes
    /// colour right now (not on the next problem) and pulses once.
    void showFeedback(bool correct, bool picked_left, uint8_t streak_after, bool milestone);

    /// One-shot read of a card tap. Returns false if nothing is queued.
    bool takeTap(bool& picked_left);
    /// Drops any queued tap, so presses made during a feedback pause do not
    /// leak into the next problem.
    void clearTap();

    /// Invoked from the LVGL click handler; not part of the page's own API.
    void handleCardClicked(bool left);

private:
    lv_obj_t* _root       = nullptr;
    lv_obj_t* _arc        = nullptr;
    lv_obj_t* _status     = nullptr;
    lv_obj_t* _equation   = nullptr;
    lv_obj_t* _cards[2]   = {nullptr, nullptr};  // [0] left, [1] right
    lv_obj_t* _values[2]  = {nullptr, nullptr};

    math::Problem _problem;
    bool _tap_pending = false;
    bool _tap_left    = false;

    void resetCardStyles();
};

/// Everything the result page needs to draw one finished round. The wallet is
/// given as it stood *before* the round so the page can start there and let
/// the app walk it forward step by step, sounds in hand.
struct Summary {
    uint8_t correct     = 0;
    uint8_t total       = 0;
    uint8_t stars       = 0;
    const char* verdict = nullptr;
    math::Level level   = math::Level::AddWithin10;
    math::Wallet wallet_before;
};

class ResultPage {
public:
    ~ResultPage();

    bool create(lv_obj_t* parent);
    void destroy();
    void setHidden(bool hidden);

    /// Draws the page with the stars unlit and the wallet at its pre-round
    /// value. The app then drives the celebration: one revealStar() per star,
    /// one showWallet() per dust tick, pulseWalletStar() on each carry.
    void beginSummary(const Summary& summary);
    /// Lights star dot `index` (0..2) with a one-shot pulse.
    void revealStar(uint8_t index);
    /// Updates the wallet line to an intermediate (or final) value.
    void showWallet(uint16_t stars, uint8_t dust);
    /// One-shot pulse on the wallet's star icon: the make-ten moment.
    void pulseWalletStar();
    /// Skip straight to the finished state: all stars lit, wallet final.
    void finishSummary(const Summary& summary, const math::Wallet& wallet_after);

    /// Full-screen unlock celebration for a newly earned play mode. Sits on
    /// top of the summary; the app dismisses it on the next tap or key.
    void showUnlock(math::Mode mode);
    void hideUnlock();

    /// Updates just the tier line (the child-facing map name).
    void showLevel(math::Level level);

    bool takeTap();
    void clearTap();
    void handleClicked();

private:
    lv_obj_t* _root       = nullptr;
    lv_obj_t* _dots[3]    = {nullptr, nullptr, nullptr};
    lv_obj_t* _score      = nullptr;
    lv_obj_t* _verdict    = nullptr;
    lv_obj_t* _level      = nullptr;
    // One hint per button, each sitting under the button it describes.
    lv_obj_t* _hint_a     = nullptr;
    lv_obj_t* _hint_b     = nullptr;
    // Wallet row: star icon + count, dust icon + count, self-centering.
    lv_obj_t* _wallet_row  = nullptr;
    lv_obj_t* _wallet_star = nullptr;
    lv_obj_t* _stars_label = nullptr;
    lv_obj_t* _dust_label  = nullptr;
    // Unlock overlay, hidden until a threshold is crossed.
    lv_obj_t* _unlock       = nullptr;
    lv_obj_t* _unlock_name  = nullptr;
    lv_obj_t* _unlock_cards[2] = {nullptr, nullptr};

    bool _tap_pending = false;
};

/// What the map draws. Tiers 0..max_unlocked are open; the rest sit dark until
/// the adaptive difficulty has promoted the child past their neighbour.
struct MapInfo {
    uint8_t selected     = 0;
    uint8_t max_unlocked = 0;
    uint8_t best_stars[math::kLevelCount] = {};
    math::Wallet wallet;
};

/**
 * @brief Level picker: six nodes around the rim of the round screen.
 *
 * The circle is the path -- reading order runs clockwise from the top. The
 * centre shows the selected tier's child-facing name, its best star rating,
 * and the wallet. A on the bezel steps the selection, B starts; tapping a
 * node selects it, tapping it again (or the centre) starts.
 */
class MapPage {
public:
    ~MapPage();

    bool create(lv_obj_t* parent);
    void destroy();
    void setHidden(bool hidden);

    void show(const MapInfo& info);

    /// One-shot read of a tap. node is a tier index, or -1 for the centre.
    bool takeTap(int8_t& node);
    void clearTap();
    void handleClicked(int8_t node);

private:
    lv_obj_t* _root  = nullptr;
    lv_obj_t* _nodes[math::kLevelCount]      = {};
    lv_obj_t* _node_labels[math::kLevelCount] = {};
    lv_obj_t* _node_dots[math::kLevelCount][3] = {};
    lv_obj_t* _centre      = nullptr;  // invisible tap target for "start"
    lv_obj_t* _name        = nullptr;
    lv_obj_t* _tier_name   = nullptr;
    lv_obj_t* _best_dots[3] = {};
    lv_obj_t* _wallet_row  = nullptr;
    lv_obj_t* _stars_label = nullptr;
    lv_obj_t* _dust_label  = nullptr;
    lv_obj_t* _hint_a      = nullptr;
    lv_obj_t* _hint_b      = nullptr;

    bool _tap_pending = false;
    int8_t _tap_node  = -1;
};

}  // namespace view
