/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once
#include <cstdint>

/**
 * @brief Arithmetic problem generation for the two-button quiz.
 *
 * Deliberately free of LVGL and ESP-IDF headers so the whole generator can be
 * exercised on the host: see tools/math_host_test/. Randomness comes from an
 * internal xorshift seeded by the caller, which makes every sequence exactly
 * reproducible from its seed.
 */
namespace math {

enum class Op : uint8_t {
    Add,
    Sub,
};

/**
 * @brief Difficulty tiers, in the order the first-grade curriculum teaches them.
 *
 * Tier 2 (CarryWithin20) is the one children actually struggle with -- adding
 * across ten and borrowing back down. The tiers below it are warm-up, the ones
 * above it are the same skill on bigger numbers.
 */
enum class Level : uint8_t {
    AddWithin10 = 0,
    SubWithin10,
    CarryWithin20,
    TensOnly,
    TwoDigitPlusOne,
    TwoDigitFull,
};

constexpr uint8_t kLevelCount = 6;

/// Short Chinese label for the level, e.g. "20 以内进位".
const char* levelName(Level level);

/// Child-facing map name for the level, e.g. "进位山洞".
const char* levelKidName(Level level);

/**
 * @brief How a problem is asked. Both kinds share the same two-button input.
 *
 * Pick shows "a + b" with two candidate answers on the cards. Judge shows a
 * completed equation "a + b = c" and the cards read 对 / 错 -- the child
 * verifies instead of computing. Judge reuses the whole Pick machinery: the
 * shown value is the answer or the decoy, so a false equation is always a
 * plausible mistake and still cannot be rejected on sight.
 */
enum class Kind : uint8_t {
    Pick,
    Judge,
};

struct Problem {
    uint8_t lhs        = 0;
    uint8_t rhs        = 0;
    Op op              = Op::Add;
    uint8_t answer     = 0;
    uint8_t distractor = 0;
    Kind kind          = Kind::Pick;
    /// For Pick: which card holds the right answer. Randomised per problem so
    /// a child cannot settle into always pressing the same button.
    /// For Judge: whether the shown equation is true. The left card is 对, so
    /// "correct card is left" and "equation is true" stay one flag -- submit()
    /// does not need to know the kind.
    bool answer_on_left = false;

    uint8_t leftValue() const
    {
        return answer_on_left ? answer : distractor;
    }
    uint8_t rightValue() const
    {
        return answer_on_left ? distractor : answer;
    }
    /// The value printed in a Judge equation: the answer when the equation is
    /// true, the decoy when it lies.
    uint8_t shownValue() const
    {
        return answer_on_left ? answer : distractor;
    }
    /// Identity of the sum/difference itself, ignoring which side the answer
    /// landed on -- used for the repeat filter.
    uint32_t key() const
    {
        return (static_cast<uint32_t>(op) << 16) | (static_cast<uint32_t>(lhs) << 8) | rhs;
    }
};

class Generator {
public:
    explicit Generator(uint32_t seed);

    void reseed(uint32_t seed);

    /// Builds a fresh problem for the tier, avoiding the last kRecent sums.
    Problem next(Level level);

    /// Like next(), but prefers the tier's hardest shape: a sum that carries
    /// or a difference that borrows. On tiers where no problem ever crosses
    /// ten (round tens) every attempt fails and this decays to next() -- the
    /// gold question's bonus is for the occasion there, not for difficulty.
    Problem nextGold(Level level);

    /// Re-rolls the distractor and the answer side of an existing problem.
    /// Used when a missed problem comes back at the end of the round: same
    /// sum, different decoy, so the child cannot pass on muscle memory alone.
    void reroll(Problem& problem);

    /// Forgets the repeat history. Called at the start of a round.
    void clearHistory();

    uint32_t random();

private:
    /// Uniform in [0, n). n == 0 yields 0.
    uint8_t roll(uint8_t n);
    /// Uniform in [lo, hi], inclusive. Returns lo if hi < lo.
    uint8_t range(uint8_t lo, uint8_t hi);

    void build(Level level, Problem& out);
    uint8_t pickDistractor(const Problem& problem);
    bool wasRecent(uint32_t key) const;
    void remember(uint32_t key);

    static constexpr uint8_t kRecent = 8;

    uint32_t _state = 1;
    uint32_t _recent[kRecent];
    uint8_t _recent_count = 0;
    uint8_t _recent_head  = 0;
};

}  // namespace math
