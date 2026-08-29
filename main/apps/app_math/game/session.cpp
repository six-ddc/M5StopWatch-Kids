/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include "session.h"

namespace math {

Session::Session(Generator& generator) : _gen(&generator) {}

void Session::startRound(Level level, bool with_judge)
{
    _level       = level;
    _stats       = RoundStats{};
    _fresh_index = 0;
    _retry_count = 0;
    _retry_index = 0;
    _streak      = 0;
    _judge_mask  = 0;
    _in_retry    = false;
    _finished    = false;
    _answered    = false;

    _gen->clearHistory();

    if (with_judge) {
        // Two distinct positions among the first nine. The tenth is the gold
        // question and stays a Pick: two climaxes on one problem would blur
        // both.
        while (__builtin_popcount(_judge_mask) < kJudgePerRound) {
            _judge_mask = static_cast<uint16_t>(
                _judge_mask | (1u << (_gen->random() % (kProblemsPerRound - 1))));
        }
    }

    loadFresh();
}

void Session::loadFresh()
{
    const bool gold = (_fresh_index + 1 == kProblemsPerRound);
    _current        = gold ? _gen->nextGold(_level) : _gen->next(_level);
    ++_fresh_index;
    if (_judge_mask & (1u << (_fresh_index - 1))) {
        // Same numbers, same decoy, different question: the child verifies the
        // equation instead of computing it. answer_on_left becomes "it is
        // true", which submit() scores without knowing the difference.
        _current.kind = Kind::Judge;
    }
}

void Session::loadRetry()
{
    _current = _retry[_retry_index];
    // Same sum, fresh decoy, fresh left/right. Getting it right the second
    // time has to mean the child actually worked it out, not that they
    // remembered which card to press.
    _gen->reroll(_current);
}

uint8_t Session::index() const
{
    return _in_retry ? static_cast<uint8_t>(_retry_index + 1) : _fresh_index;
}

uint8_t Session::total() const
{
    return _in_retry ? _retry_count : kProblemsPerRound;
}

bool Session::submit(bool picked_left)
{
    if (_finished || _answered) {
        return false;
    }
    _answered            = true;
    const bool is_correct = (picked_left == _current.answer_on_left);

    if (_in_retry) {
        ++_stats.retry_asked;
        if (is_correct) {
            ++_stats.retry_correct;
        }
    } else {
        ++_stats.fresh_asked;
        if (is_correct) {
            ++_stats.fresh_correct;
            if (_fresh_index == kProblemsPerRound) {
                _stats.gold_correct = true;
            }
        }
    }

    if (is_correct) {
        ++_streak;
        if (_streak > _stats.best_streak) {
            _stats.best_streak = _streak;
        }
    } else {
        _streak = 0;
        // Only first-pass misses queue up. A miss during the replay does not
        // re-queue, otherwise a problem the child cannot do yet would trap
        // them in the round forever.
        if (!_in_retry && _retry_count < kProblemsPerRound) {
            _retry[_retry_count++] = _current;
        }
    }
    return is_correct;
}

void Session::advance()
{
    if (_finished || !_answered) {
        return;
    }
    _answered = false;

    if (!_in_retry) {
        if (_fresh_index < kProblemsPerRound) {
            loadFresh();
            return;
        }
        if (_retry_count > 0) {
            _in_retry    = true;
            _retry_index = 0;
            loadRetry();
            return;
        }
        _finished = true;
        return;
    }

    ++_retry_index;
    if (_retry_index < _retry_count) {
        loadRetry();
        return;
    }
    _finished = true;
}

uint8_t Session::stars() const
{
    if (_stats.fresh_correct >= 9) {
        return 3;
    }
    if (_stats.fresh_correct >= 7) {
        return 2;
    }
    if (_stats.fresh_correct >= 5) {
        return 1;
    }
    return 0;
}

const char* Session::verdict() const
{
    switch (stars()) {
        case 3:
            return "太棒了";
        case 2:
            return "真不错";
        case 1:
            return "继续加油";
        default:
            return "别灰心";
    }
}

Level Session::suggestLevel(Level current) const
{
    const uint8_t tier = static_cast<uint8_t>(current);
    if (_stats.fresh_correct >= 8 && _stats.best_streak >= 5 && tier + 1 < kLevelCount) {
        return static_cast<Level>(tier + 1);
    }
    if (_stats.fresh_correct < 5 && tier > 0) {
        return static_cast<Level>(tier - 1);
    }
    return current;
}

}  // namespace math
