/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include "session.h"

namespace eng {

namespace {

/// A unit's missed-word set is a bitmask, so a unit wider than this would
/// silently drop the tail. The packer writes 12-word units; this is the
/// ceiling that assumption is allowed to move to.
constexpr uint16_t kMaxUnitWords = 32;

}  // namespace

uint32_t Session::nextRandom()
{
    // xorshift32, same as the arithmetic generator: cheap, and a fixed seed
    // reproduces a whole round exactly, which is what the host tests need.
    _rng ^= _rng << 13;
    _rng ^= _rng >> 17;
    _rng ^= _rng << 5;
    return _rng;
}

bool Session::start(const Data& data, uint16_t unit, uint32_t seed)
{
    _stage = Stage::Idle;
    _data  = nullptr;

    if (!data.valid() || unit >= data.unitCount()) {
        return false;
    }
    const Unit u = data.unit(unit);
    if (u.count < kMinUnitWords) {
        return false;
    }

    _data  = &data;
    _unit  = unit;
    _first = u.first;
    _count = u.count > kMaxUnitWords ? kMaxUnitWords : u.count;

    // A zero seed would leave xorshift stuck at zero forever.
    _rng         = seed != 0 ? seed : 0x1234567Bu;
    _index       = 0;
    _missed      = 0;
    _streak      = 0;
    _last_target = 0xFFFF;
    _stats       = Stats{};
    _stage       = Stage::Learn;
    return true;
}

uint16_t Session::learnWord() const
{
    if (_stage != Stage::Learn || _index >= _count) {
        return 0;
    }
    return static_cast<uint16_t>(_first + _index);
}

uint8_t Session::total() const
{
    switch (_stage) {
        case Stage::Learn:
            return static_cast<uint8_t>(_count);
        case Stage::Quiz:
            return kQuestionsPerRound;
        default:
            return 0;
    }
}

Stage Session::advanceLearn()
{
    if (_stage != Stage::Learn) {
        return _stage;
    }
    ++_index;
    if (_index < _count) {
        return _stage;
    }
    _stage = Stage::Quiz;
    _index = 0;
    drawQuestion();
    return _stage;
}

void Session::drawQuestion()
{
    if (_data == nullptr || _count < kMinUnitWords) {
        return;
    }

    // Pick a target that is not the one just asked, so the same picture never
    // sits on screen twice running. With two words in a unit there is only one
    // other choice, which is exactly what this loop lands on.
    uint16_t local = static_cast<uint16_t>(nextRandom() % _count);
    if (_count > 1) {
        uint8_t guard = 0;
        while (static_cast<uint16_t>(_first + local) == _last_target && guard < 8) {
            local = static_cast<uint16_t>(nextRandom() % _count);
            ++guard;
        }
    }

    // The decoy is drawn from the same unit: within a category the two
    // pictures are genuinely confusable, so a right answer means something.
    uint16_t decoy = static_cast<uint16_t>(nextRandom() % (_count - 1));
    if (decoy >= local) {
        ++decoy;  // skip the target without rejection-sampling
    }

    _question.target      = static_cast<uint16_t>(_first + local);
    _question.decoy       = static_cast<uint16_t>(_first + decoy);
    _question.target_left = (nextRandom() & 1) != 0;
    _question.activity    = _index < kListenQuestions ? Activity::ListenPick : Activity::ReadPick;
    _last_target          = _question.target;
}

bool Session::submit(bool picked_left)
{
    if (_stage != Stage::Quiz) {
        return false;
    }
    const bool correct = (picked_left == _question.target_left);

    ++_stats.asked;
    if (correct) {
        ++_stats.correct;
        ++_streak;
        if (_streak > _stats.streak) {
            _stats.streak = _streak;
        }
    } else {
        _streak              = 0;
        const uint16_t local = static_cast<uint16_t>(_question.target - _first);
        if (local < kMaxUnitWords) {
            _missed |= (1u << local);
        }
    }
    return correct;
}

Stage Session::advanceQuiz()
{
    if (_stage != Stage::Quiz) {
        return _stage;
    }
    ++_index;
    if (_index >= kQuestionsPerRound) {
        _stage = Stage::Done;
        return _stage;
    }
    drawQuestion();
    return _stage;
}

uint8_t Session::stars() const
{
    // Three stars has to be reachable without being automatic: nine out of ten
    // is one slip, which a child who knows the unit will manage often enough
    // to keep trying for it.
    if (_stats.correct >= 9) {
        return 3;
    }
    if (_stats.correct >= 7) {
        return 2;
    }
    if (_stats.correct >= 5) {
        return 1;
    }
    return 0;
}

const char* Session::verdict() const
{
    // Reuses the arithmetic app's four phrases so the subset font already
    // covers them -- see UI_STRINGS in tools/hanzi_pipeline/build_hanzi_data.py.
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

uint8_t Session::missedWords(uint16_t* out, uint8_t cap) const
{
    if (out == nullptr || cap == 0) {
        return 0;
    }
    uint8_t n = 0;
    for (uint16_t i = 0; i < _count && n < cap; ++i) {
        if (_missed & (1u << i)) {
            out[n++] = static_cast<uint16_t>(_first + i);
        }
    }
    return n;
}

}  // namespace eng
