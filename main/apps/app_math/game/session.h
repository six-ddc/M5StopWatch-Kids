/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once
#include <cstdint>
#include "problem.h"

/**
 * @brief One round of the quiz: ten fresh problems, then a replay of the misses.
 *
 * Like problem.h this is free of LVGL and ESP-IDF so it can be driven straight
 * from a host test. It also carries no notion of time -- the app layer owns the
 * clock and the feedback pauses.
 */
namespace math {

constexpr uint8_t kProblemsPerRound = 10;
/// Judge questions mixed into a round once the mode is unlocked. They land on
/// random fresh positions, never on the tenth -- that one is the gold question.
constexpr uint8_t kJudgePerRound = 2;

struct RoundStats {
    uint8_t fresh_asked   = 0;
    uint8_t fresh_correct = 0;
    uint8_t retry_asked   = 0;
    uint8_t retry_correct = 0;
    uint8_t best_streak   = 0;
    /// Whether the tenth, gold question was answered right first time.
    bool gold_correct     = false;
};

class Session {
public:
    explicit Session(Generator& generator);

    void startRound(Level level, bool with_judge = false);

    /// False once the round is over and the result screen should take over.
    bool active() const
    {
        return !_finished;
    }
    const Problem& current() const
    {
        return _current;
    }
    /// True while replaying problems the child got wrong earlier this round.
    bool inRetry() const
    {
        return _in_retry;
    }
    /// True while the tenth fresh problem -- the round's gold question -- is up.
    bool isGold() const
    {
        return !_in_retry && !_finished && _fresh_index == kProblemsPerRound;
    }
    /// 1-based position within the current phase, for the "4/10" readout.
    uint8_t index() const;
    /// Length of the current phase: ten for the main run, the miss count for
    /// the replay. Deliberately not a single growing total -- "4/11" appearing
    /// mid-round because you slipped up reads as a punishment.
    uint8_t total() const;

    /// Records an answer for the current problem and returns whether it was
    /// right. Does not advance: the caller shows feedback first, then calls
    /// advance(). A second call before advance() is ignored.
    bool submit(bool picked_left);

    /// Moves to the next problem, into the replay phase, or ends the round.
    void advance();

    uint8_t streak() const
    {
        return _streak;
    }
    const RoundStats& stats() const
    {
        return _stats;
    }
    /// 0..3, from how many of the ten fresh problems were right.
    uint8_t stars() const;
    /// Short Chinese verdict matching the star count.
    const char* verdict() const;
    /// Adaptive difficulty: bumps up on a strong round, eases off on a weak
    /// one, otherwise holds. Clamped to the tier range.
    Level suggestLevel(Level current) const;

private:
    void loadFresh();
    void loadRetry();

    Generator* _gen = nullptr;
    Level _level    = Level::AddWithin10;
    Problem _current;
    Problem _retry[kProblemsPerRound];
    RoundStats _stats;

    uint8_t _fresh_index  = 0;  // how many fresh problems have been served
    uint8_t _retry_count  = 0;
    uint8_t _retry_index  = 0;
    uint8_t _streak       = 0;
    uint16_t _judge_mask  = 0;  // bit i-1 set = fresh position i asks a Judge
    bool _in_retry        = false;
    bool _finished        = true;
    bool _answered        = false;
};

}  // namespace math
