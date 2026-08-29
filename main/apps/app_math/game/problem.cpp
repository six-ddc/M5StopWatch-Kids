/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include "problem.h"

namespace math {

const char* levelName(Level level)
{
    switch (level) {
        case Level::AddWithin10:
            return "10 以内加法";
        case Level::SubWithin10:
            return "10 以内减法";
        case Level::CarryWithin20:
            return "20 以内进退位";
        case Level::TensOnly:
            return "整十数加减";
        case Level::TwoDigitPlusOne:
            return "两位数加减一位数";
        case Level::TwoDigitFull:
            return "两位数加减两位数";
    }
    return "";
}

const char* levelKidName(Level level)
{
    switch (level) {
        case Level::AddWithin10:
            return "数字花园";
        case Level::SubWithin10:
            return "数字池塘";
        case Level::CarryWithin20:
            return "进位山洞";
        case Level::TensOnly:
            return "整十高山";
        case Level::TwoDigitPlusOne:
            return "大数森林";
        case Level::TwoDigitFull:
            return "大数海洋";
    }
    return "";
}

namespace {

/// The tier-defining hard shape: addition that carries, subtraction that
/// borrows. Same gating as the decoy pool uses. On the easiest tiers this
/// picks out the boundary cases -- a sum of exactly ten, a subtraction dipping
/// below ten -- which is the make-ten skill in its own right.
bool crossesTen(const Problem& p)
{
    const uint8_t lo = p.lhs % 10;
    const uint8_t ro = p.rhs % 10;
    return p.op == Op::Add ? (lo + ro) >= 10 : lo < ro;
}

}  // namespace

Generator::Generator(uint32_t seed)
{
    reseed(seed);
}

void Generator::reseed(uint32_t seed)
{
    // xorshift is stuck forever at zero, so a zero seed has to be replaced.
    _state = seed != 0 ? seed : 0x1234567Bu;
    clearHistory();
}

void Generator::clearHistory()
{
    _recent_count = 0;
    _recent_head  = 0;
}

uint32_t Generator::random()
{
    // xorshift32: cheap, no state beyond the seed, and the whole sequence is
    // determined by it -- which is what makes the host tests reproducible.
    _state ^= _state << 13;
    _state ^= _state >> 17;
    _state ^= _state << 5;
    return _state;
}

uint8_t Generator::roll(uint8_t n)
{
    if (n == 0) {
        return 0;
    }
    return static_cast<uint8_t>(random() % n);
}

uint8_t Generator::range(uint8_t lo, uint8_t hi)
{
    if (hi <= lo) {
        return lo;
    }
    return static_cast<uint8_t>(lo + random() % (hi - lo + 1));
}

bool Generator::wasRecent(uint32_t key) const
{
    for (uint8_t i = 0; i < _recent_count; ++i) {
        if (_recent[i] == key) {
            return true;
        }
    }
    return false;
}

void Generator::remember(uint32_t key)
{
    _recent[_recent_head] = key;
    _recent_head          = static_cast<uint8_t>((_recent_head + 1) % kRecent);
    if (_recent_count < kRecent) {
        ++_recent_count;
    }
}

void Generator::build(Level level, Problem& out)
{
    switch (level) {
        case Level::AddWithin10: {
            // Pick the total first so every sum in 2..10 is equally likely.
            // Picking the addends first would over-represent the small sums.
            const uint8_t sum = range(2, 10);
            const uint8_t a   = range(1, static_cast<uint8_t>(sum - 1));
            out.lhs           = a;
            out.rhs           = static_cast<uint8_t>(sum - a);
            out.op            = Op::Add;
            out.answer        = sum;
            break;
        }

        case Level::SubWithin10: {
            const uint8_t a = range(2, 10);
            const uint8_t b = range(1, a);
            out.lhs         = a;
            out.rhs         = b;
            out.op          = Op::Sub;
            out.answer      = static_cast<uint8_t>(a - b);
            break;
        }

        case Level::CarryWithin20: {
            if (roll(2) == 0) {
                // Carrying add: both addends stay single-digit, total goes
                // past ten. Clamping the low addend to sum-9 is what keeps the
                // other one from spilling into two digits.
                const uint8_t sum = range(11, 18);
                const uint8_t a   = range(static_cast<uint8_t>(sum - 9), 9);
                out.lhs           = a;
                out.rhs           = static_cast<uint8_t>(sum - a);
                out.op            = Op::Add;
                out.answer        = sum;
            } else {
                // Borrowing subtract: the ones digit of lhs must be smaller
                // than rhs, otherwise there is nothing to borrow and the
                // problem is not actually teaching the tier's skill.
                const uint8_t b = range(2, 9);
                const uint8_t a = range(11, static_cast<uint8_t>(10 + b - 1));
                out.lhs         = a;
                out.rhs         = b;
                out.op          = Op::Sub;
                out.answer      = static_cast<uint8_t>(a - b);
            }
            break;
        }

        case Level::TensOnly: {
            if (roll(2) == 0) {
                const uint8_t x = range(1, 8);
                const uint8_t y = range(1, static_cast<uint8_t>(9 - x));
                out.lhs         = static_cast<uint8_t>(x * 10);
                out.rhs         = static_cast<uint8_t>(y * 10);
                out.op          = Op::Add;
                out.answer      = static_cast<uint8_t>((x + y) * 10);
            } else {
                const uint8_t x = range(2, 9);
                const uint8_t y = range(1, x);
                out.lhs         = static_cast<uint8_t>(x * 10);
                out.rhs         = static_cast<uint8_t>(y * 10);
                out.op          = Op::Sub;
                out.answer      = static_cast<uint8_t>((x - y) * 10);
            }
            break;
        }

        case Level::TwoDigitPlusOne: {
            const uint8_t b = range(1, 9);
            if (roll(2) == 0) {
                const uint8_t a = range(10, static_cast<uint8_t>(99 - b));
                out.lhs         = a;
                out.rhs         = b;
                out.op          = Op::Add;
                out.answer      = static_cast<uint8_t>(a + b);
            } else {
                const uint8_t a = range(10, 99);
                out.lhs         = a;
                out.rhs         = b;
                out.op          = Op::Sub;
                out.answer      = static_cast<uint8_t>(a - b);
            }
            break;
        }

        case Level::TwoDigitFull: {
            if (roll(2) == 0) {
                // lhs caps at 89 so there is always room for a two-digit rhs
                // without the total running past 99.
                const uint8_t a = range(10, 89);
                const uint8_t b = range(10, static_cast<uint8_t>(99 - a));
                out.lhs         = a;
                out.rhs         = b;
                out.op          = Op::Add;
                out.answer      = static_cast<uint8_t>(a + b);
            } else {
                const uint8_t a = range(20, 99);
                const uint8_t b = range(10, a);
                out.lhs         = a;
                out.rhs         = b;
                out.op          = Op::Sub;
                out.answer      = static_cast<uint8_t>(a - b);
            }
            break;
        }
    }
}

uint8_t Generator::pickDistractor(const Problem& p)
{
    // Every candidate is a mistake children actually make, weighted by how
    // often it shows up. A plausible decoy is what stops the quiz from being
    // winnable by elimination -- both cards have to look possible, so the only
    // way through is to do the arithmetic.
    struct Candidate {
        int16_t value;
        uint8_t weight;
    };
    Candidate pool[8];
    uint8_t n = 0;

    auto add = [&](int16_t value, uint8_t weight) {
        if (value < 0 || value > 99 || value == static_cast<int16_t>(p.answer) || n >= 8) {
            return;
        }
        for (uint8_t i = 0; i < n; ++i) {
            if (pool[i].value == value) {
                pool[i].weight = static_cast<uint8_t>(pool[i].weight + weight);
                return;
            }
        }
        pool[n].value  = value;
        pool[n].weight = weight;
        ++n;
    };

    const int16_t answer = static_cast<int16_t>(p.answer);
    const int16_t lhs    = static_cast<int16_t>(p.lhs);
    const int16_t rhs    = static_cast<int16_t>(p.rhs);
    const int16_t lhs_ones = static_cast<int16_t>(p.lhs % 10);
    const int16_t rhs_ones = static_cast<int16_t>(p.rhs % 10);

    // Adding where the sign says subtract, and the other way round. Applies to
    // every tier: misreading the operator is not tied to the size of the
    // numbers.
    add(p.op == Op::Add ? lhs - rhs : lhs + rhs, 4);

    if (lhs_ones == 0 && rhs_ones == 0) {
        // Round tens are their own number system: 80 - 40 is always a ten, so
        // a decoy like 41 is wrong on sight and never makes anyone do the
        // arithmetic. Slip a whole ten instead of a unit.
        add(answer - 10, 5);
        add(answer + 10, 5);
        add(answer - 20, 1);
        add(answer + 20, 1);
    } else {
        // Losing the carry, or forgetting that the tens column was borrowed
        // from. Only counts where the problem actually carries or borrows --
        // on 9 - 1 there is nothing to borrow, and 19 would just be a number
        // that is obviously too big.
        const bool carries = p.op == Op::Add && (lhs_ones + rhs_ones) >= 10;
        const bool borrows = p.op == Op::Sub && lhs_ones < rhs_ones;
        if (carries) {
            add(answer - 10, 5);
        }
        if (borrows) {
            add(answer + 10, 5);
            // The classic borrowing mistake: the ones column gets subtracted
            // the wrong way round because it "won't go". 15 - 8 becomes
            // |5 - 8| = 3 in the ones with the tens left alone, giving 13.
            const int16_t flipped_ones = static_cast<int16_t>(
                lhs_ones > rhs_ones ? lhs_ones - rhs_ones : rhs_ones - lhs_ones);
            add(static_cast<int16_t>((p.lhs / 10) * 10 + flipped_ones), 5);
        }
        // Off by one in the ones column.
        add(answer + 1, 2);
        add(answer - 1, 2);
        // Transposed digits, but only when the swap yields a real two-digit
        // number: 30 would swap to 03, which just reads as 3.
        if (p.answer >= 10 && (p.answer / 10) != (p.answer % 10) && (p.answer % 10) != 0) {
            add(static_cast<int16_t>((p.answer % 10) * 10 + (p.answer / 10)), 1);
        }
    }

    if (n == 0) {
        // Nothing plausible survived the 0..99 clamp. Reachable at the extremes
        // -- e.g. answer 0 from a subtraction, where -10, -1 and the swap are
        // all out of range and lhs+rhs collides with something else.
        for (uint8_t attempt = 0; attempt < 16; ++attempt) {
            const int16_t delta = static_cast<int16_t>(range(2, 5)) * (roll(2) == 0 ? 1 : -1);
            const int16_t value = answer + delta;
            if (value >= 0 && value <= 99 && value != answer) {
                return static_cast<uint8_t>(value);
            }
        }
        return p.answer == 0 ? 1 : static_cast<uint8_t>(p.answer - 1);
    }

    uint16_t total = 0;
    for (uint8_t i = 0; i < n; ++i) {
        total = static_cast<uint16_t>(total + pool[i].weight);
    }
    uint16_t pick = static_cast<uint16_t>(random() % total);
    for (uint8_t i = 0; i < n; ++i) {
        if (pick < pool[i].weight) {
            return static_cast<uint8_t>(pool[i].value);
        }
        pick = static_cast<uint16_t>(pick - pool[i].weight);
    }
    return static_cast<uint8_t>(pool[0].value);
}

Problem Generator::next(Level level)
{
    Problem p;
    // The easiest tier only has 45 distinct sums, so the repeat filter has to
    // give up rather than spin: a duplicate is far better than a hang.
    for (uint8_t attempt = 0; attempt < 24; ++attempt) {
        build(level, p);
        if (!wasRecent(p.key())) {
            break;
        }
    }
    remember(p.key());
    p.distractor     = pickDistractor(p);
    p.answer_on_left = roll(2) == 0;
    return p;
}

Problem Generator::nextGold(Level level)
{
    // Same attempt budget as next(). A tier that cannot cross ten burns the
    // attempts and falls through -- build() is allocation-free, so the cost of
    // that is trivial, and only the problem actually served enters the repeat
    // history.
    Problem p;
    for (uint8_t attempt = 0; attempt < 24; ++attempt) {
        build(level, p);
        if (crossesTen(p) && !wasRecent(p.key())) {
            remember(p.key());
            p.distractor     = pickDistractor(p);
            p.answer_on_left = roll(2) == 0;
            return p;
        }
    }
    return next(level);
}

void Generator::reroll(Problem& problem)
{
    problem.distractor     = pickDistractor(problem);
    problem.answer_on_left = roll(2) == 0;
}

}  // namespace math
