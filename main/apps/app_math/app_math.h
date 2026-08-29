/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once
#include <apps/common/key_manager/key_manager.h>
#include <mooncake.h>
#include <cstdint>
#include <memory>
#include "game/economy.h"
#include "game/problem.h"
#include "game/session.h"
#include "view/view.h"

/**
 * @brief Two-button arithmetic drill for children: 0..99, add and subtract.
 *
 * Every problem is two cards. A picks the left one, B picks the right one, and
 * a finger works just as well -- there is nothing else to learn. The decoy on
 * the losing card is always a mistake a child actually makes (a dropped carry,
 * a swapped sign), so guessing does not pay and a wrong answer says something
 * about what they have not got yet.
 *
 * Around that core sits the reward loop: every correct answer pays star dust,
 * ten dust carry into a star, and stars unlock new ways to play. The map page
 * turns the six difficulty tiers into places; the result page turns the
 * settling of the round's earnings into a small ceremony. All the arithmetic
 * of that economy lives in game/economy.* where the host tests can reach it;
 * this class owns only the clocks -- feedback pauses, the celebration
 * timeline, the second half of a double buzz.
 */
class AppMath : public mooncake::AppAbility {
public:
    AppMath();

    void onCreate() override;
    void onOpen() override;
    void onRunning() override;
    void onClose() override;

private:
    enum class Page : uint8_t {
        Map,
        Quiz,
        Result,
    };
    enum class Phase : uint8_t {
        Asking,
        Feedback,
    };
    /// The result page's celebration: stars land one by one, then the dust
    /// ticks in (pulsing on each carry), then a pending unlock takes the
    /// screen. Any tap or key skips ahead; Done is the ordinary idle result.
    enum class Cele : uint8_t {
        Idle,
        Stars,
        Dust,
        Unlock,
        Done,
    };

    void openMap();
    void startRound(uint8_t tier);
    void showResult();
    void pushProblem();
    void submitAnswer(bool picked_left);
    void afterFeedback();
    void stepCelebration(uint32_t now_ms);
    void skipCelebration();
    void maybeShowUnlock();
    void handleResultAdvance();
    void handleMapTap(int8_t node);
    void saveProgress();

    uint8_t tierBest(math::Level level) const;
    void bumpTierBest(math::Level level, uint8_t stars);
    view::MapInfo mapInfo() const;

    std::unique_ptr<input::KeyManager> _key_manager;
    std::unique_ptr<view::QuizPage> _quiz;
    std::unique_ptr<view::ResultPage> _result;
    std::unique_ptr<view::MapPage> _map;
    std::unique_ptr<math::Generator> _generator;
    std::unique_ptr<math::Session> _session;

    math::Level _level        = math::Level::AddWithin10;
    Page _page                = Page::Map;
    Phase _phase              = Phase::Asking;
    uint32_t _freeze_start_ms = 0;
    uint32_t _freeze_ms       = 0;
    uint32_t _last_key_ms     = 0;
    uint8_t _best_streak      = 0;
    uint32_t _total_correct   = 0;
    bool _progress_dirty      = false;

    // Reward state. _tier_stars packs six 2-bit best ratings, map unlocking
    // follows the highest tier the adaptive difficulty has ever suggested.
    math::Wallet _wallet;
    uint8_t _max_tier      = 0;
    uint8_t _selected_tier = 0;
    uint16_t _tier_stars   = 0;

    // Celebration timeline, all driven from onRunning.
    view::Summary _summary;
    math::RoundEarnings _earnings;
    Cele _cele             = Cele::Idle;
    uint8_t _cele_star     = 0;
    uint8_t _cele_dust     = 0;
    uint32_t _cele_next_ms = 0;
    bool _unlock_pending   = false;

    // Second pulse of the milestone double buzz, 0 when none is due.
    uint32_t _buzz_at_ms = 0;
};
