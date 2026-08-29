/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 *
 * Host tests for the arithmetic game's pure logic layer.
 *
 * The generator's invariants matter more than they look: a first-grader has
 * not met negative numbers, and a decoy that is out of range or equal to the
 * answer silently turns a two-choice question into a one-choice one. Both
 * failure modes are invisible on the watch, so they get pinned down here.
 *
 *   cmake -S tools/math_host_test -B build_math && cmake --build build_math
 *   ./build_math/math_logic_test
 */
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>
#include "economy.h"
#include "problem.h"
#include "session.h"

namespace {

int g_failures = 0;
int g_checks   = 0;

void check(bool condition, const std::string& what)
{
    ++g_checks;
    if (!condition) {
        ++g_failures;
        if (g_failures <= 20) {
            std::printf("  FAIL: %s\n", what.c_str());
        } else if (g_failures == 21) {
            std::printf("  ... further failures suppressed\n");
        }
    }
}

std::string describe(const math::Problem& p)
{
    char buf[96];
    std::snprintf(buf, sizeof(buf), "%u %c %u = %u (decoy %u, answer %s)", p.lhs,
                  p.op == math::Op::Add ? '+' : '-', p.rhs, p.answer, p.distractor,
                  p.answer_on_left ? "left" : "right");
    return buf;
}

struct LevelTally {
    uint32_t adds = 0;
    uint32_t subs = 0;
};

// Checks the invariants that hold for every tier, then the ones specific to
// this tier's teaching goal.
void checkProblem(math::Level level, const math::Problem& p, LevelTally& tally)
{
    const std::string d = describe(p);

    check(p.answer <= 99, "answer out of 0..99: " + d);
    check(p.rhs >= 1, "degenerate operand (+0 / -0): " + d);
    check(p.distractor <= 99, "decoy out of 0..99: " + d);
    check(p.distractor != p.answer, "decoy equals the answer: " + d);

    if (p.op == math::Op::Add) {
        ++tally.adds;
        check(static_cast<int>(p.lhs) + p.rhs == p.answer, "sum does not add up: " + d);
    } else {
        ++tally.subs;
        check(p.lhs >= p.rhs, "subtraction would go negative: " + d);
        check(static_cast<int>(p.lhs) - p.rhs == p.answer, "difference does not subtract: " + d);
    }

    // The two cards must between them be exactly {answer, decoy}.
    const bool left_is_answer = p.leftValue() == p.answer && p.rightValue() == p.distractor;
    const bool right_is_answer = p.rightValue() == p.answer && p.leftValue() == p.distractor;
    check(left_is_answer || right_is_answer, "card values do not match answer/decoy: " + d);
    check(p.answer_on_left == left_is_answer, "answer_on_left disagrees with the cards: " + d);

    switch (level) {
        case math::Level::AddWithin10:
            check(p.op == math::Op::Add, "tier 0 must be addition: " + d);
            check(p.lhs >= 1 && p.lhs <= 9, "tier 0 lhs out of 1..9: " + d);
            check(p.rhs >= 1 && p.rhs <= 9, "tier 0 rhs out of 1..9: " + d);
            check(p.answer >= 2 && p.answer <= 10, "tier 0 sum out of 2..10: " + d);
            break;

        case math::Level::SubWithin10:
            check(p.op == math::Op::Sub, "tier 1 must be subtraction: " + d);
            check(p.lhs >= 2 && p.lhs <= 10, "tier 1 lhs out of 2..10: " + d);
            check(p.answer <= 9, "tier 1 answer out of 0..9: " + d);
            break;

        case math::Level::CarryWithin20:
            if (p.op == math::Op::Add) {
                check(p.lhs >= 2 && p.lhs <= 9, "tier 2 add lhs out of 2..9: " + d);
                check(p.rhs >= 2 && p.rhs <= 9, "tier 2 add rhs out of 2..9: " + d);
                // The whole point of the tier: the sum must cross ten.
                check(p.answer > 10 && p.answer <= 18, "tier 2 add does not carry: " + d);
            } else {
                check(p.lhs >= 11 && p.lhs <= 18, "tier 2 sub lhs out of 11..18: " + d);
                check(p.rhs >= 2 && p.rhs <= 9, "tier 2 sub rhs out of 2..9: " + d);
                // ...and the subtraction must actually borrow.
                check(p.lhs % 10 < p.rhs, "tier 2 sub does not borrow: " + d);
                check(p.answer >= 2 && p.answer <= 9, "tier 2 sub answer out of 2..9: " + d);
            }
            break;

        case math::Level::TensOnly:
            check(p.lhs % 10 == 0 && p.rhs % 10 == 0, "tier 3 operands are not round tens: " + d);
            check(p.lhs >= 10 && p.lhs <= 90, "tier 3 lhs out of 10..90: " + d);
            check(p.rhs >= 10 && p.rhs <= 90, "tier 3 rhs out of 10..90: " + d);
            check(p.answer % 10 == 0, "tier 3 answer is not a round ten: " + d);
            if (p.op == math::Op::Add) {
                check(p.answer <= 90, "tier 3 sum past 90: " + d);
            }
            break;

        case math::Level::TwoDigitPlusOne:
            check(p.lhs >= 10 && p.lhs <= 99, "tier 4 lhs out of 10..99: " + d);
            check(p.rhs >= 1 && p.rhs <= 9, "tier 4 rhs out of 1..9: " + d);
            break;

        case math::Level::TwoDigitFull:
            check(p.lhs >= 10 && p.lhs <= 99, "tier 5 lhs out of 10..99: " + d);
            check(p.rhs >= 10 && p.rhs <= 99, "tier 5 rhs out of 10..99: " + d);
            break;
    }
}

void testGenerator()
{
    constexpr uint32_t kIterations = 1000000;
    std::printf("[generator] %u problems per tier\n", kIterations);

    for (uint8_t tier = 0; tier < math::kLevelCount; ++tier) {
        const auto level = static_cast<math::Level>(tier);
        math::Generator gen(0xC0FFEEu + tier);
        LevelTally tally;

        // Sliding window mirroring the generator's own repeat filter.
        constexpr uint8_t kWindow = 8;
        std::vector<uint32_t> window;
        uint32_t repeats_in_window = 0;

        for (uint32_t i = 0; i < kIterations; ++i) {
            const math::Problem p = gen.next(level);
            checkProblem(level, p, tally);

            for (uint32_t key : window) {
                if (key == p.key()) {
                    ++repeats_in_window;
                    break;
                }
            }
            window.push_back(p.key());
            if (window.size() > kWindow) {
                window.erase(window.begin());
            }
        }

        const uint32_t total = tally.adds + tally.subs;
        const double add_pct = 100.0 * tally.adds / total;
        std::printf("  tier %u %-24s add %5.1f%%  sub %5.1f%%  window repeats %u\n", tier,
                    math::levelName(level), add_pct, 100.0 - add_pct, repeats_in_window);

        // Tiers that mix operations should stay near an even split; the two
        // single-operation tiers are checked by checkProblem above.
        if (level == math::Level::CarryWithin20 || level == math::Level::TensOnly ||
            level == math::Level::TwoDigitPlusOne || level == math::Level::TwoDigitFull) {
            check(add_pct > 45.0 && add_pct < 55.0,
                  std::string("lopsided add/sub mix in ") + math::levelName(level));
        }

        // The easiest tier has only 45 distinct sums, so the filter is allowed
        // to give up occasionally rather than spin. Everything else has enough
        // room that a repeat inside the window means the filter is broken.
        const uint32_t allowed = (level == math::Level::AddWithin10 ||
                                  level == math::Level::SubWithin10 ||
                                  level == math::Level::CarryWithin20)
                                     ? kIterations / 100
                                     : 0;
        check(repeats_in_window <= allowed,
              std::string("too many near-repeats in ") + math::levelName(level) + ": " +
                  std::to_string(repeats_in_window));
    }
}

void testDeterminism()
{
    std::printf("[determinism] same seed, same sequence\n");
    math::Generator a(12345);
    math::Generator b(12345);
    for (int i = 0; i < 10000; ++i) {
        const auto pa = a.next(math::Level::TwoDigitFull);
        const auto pb = b.next(math::Level::TwoDigitFull);
        check(pa.key() == pb.key() && pa.distractor == pb.distractor &&
                  pa.answer_on_left == pb.answer_on_left,
              "sequences diverged at " + std::to_string(i));
    }

    // A zero seed must not lock xorshift at zero forever.
    math::Generator zero(0);
    bool varied = false;
    const uint32_t first = zero.random();
    for (int i = 0; i < 100; ++i) {
        if (zero.random() != first) {
            varied = true;
            break;
        }
    }
    check(varied, "zero seed left the PRNG stuck");
}

void testReroll()
{
    std::printf("[reroll] the sum survives, the decoy does not have to\n");
    math::Generator gen(777);
    for (int i = 0; i < 20000; ++i) {
        math::Problem p    = gen.next(math::Level::CarryWithin20);
        const uint32_t key = p.key();
        const uint8_t answer = p.answer;
        gen.reroll(p);
        check(p.key() == key, "reroll changed the problem itself");
        check(p.answer == answer, "reroll changed the answer");
        check(p.distractor != p.answer, "reroll produced a decoy equal to the answer");
        check(p.distractor <= 99, "reroll produced an out-of-range decoy");
        LevelTally ignored;
        checkProblem(math::Level::CarryWithin20, p, ignored);
    }
}

// Plays a whole round with a caller-supplied answering strategy.
struct RoundTrace {
    uint32_t fresh_served = 0;
    uint32_t retry_served = 0;
    uint32_t max_index    = 0;
};

template <typename Strategy>
RoundTrace playRound(math::Session& session, math::Level level, Strategy strategy)
{
    RoundTrace trace;
    session.startRound(level);
    uint32_t guard = 0;
    while (session.active()) {
        if (++guard > 1000) {
            check(false, "round never terminated");
            break;
        }
        if (session.inRetry()) {
            ++trace.retry_served;
        } else {
            ++trace.fresh_served;
        }
        check(session.index() >= 1 && session.index() <= session.total(),
              "index outside 1..total");
        check(session.isGold() ==
                  (!session.inRetry() && session.index() == math::kProblemsPerRound),
              "isGold disagrees with the position");
        if (session.index() > trace.max_index) {
            trace.max_index = session.index();
        }
        const bool pick_left = strategy(session);
        session.submit(pick_left);
        session.advance();
    }
    return trace;
}

// Which kind of mistake a decoy represents. Classified by reproducing the
// candidate rules from pickDistractor() -- an "Other" means every one of them
// was filtered out by the 0..99 clamp and the generator fell back to a nearby
// number, which is the one case where the decoy really is arbitrary.
enum class DecoyKind {
    CarryMiss,
    BorrowOnes,
    TensSlip,
    OpConfuse,
    OffByOne,
    DigitSwap,
    Other,
};

// Mirrors pickDistractor's own gating, so a decoy is only attributed to a rule
// that was actually in play for that problem. Values can collide between rules
// (a flipped sign that happens to equal answer-10); ties go to the heavier
// rule, which is the more likely source.
DecoyKind classify(const math::Problem& p)
{
    const int answer = p.answer;
    const int decoy  = p.distractor;
    const int lo     = p.lhs % 10;
    const int ro     = p.rhs % 10;
    const int flipped = p.op == math::Op::Add ? static_cast<int>(p.lhs) - p.rhs
                                              : static_cast<int>(p.lhs) + p.rhs;

    if (lo == 0 && ro == 0) {
        if (decoy == answer - 10 || decoy == answer + 10 || decoy == answer - 20 ||
            decoy == answer + 20) {
            return DecoyKind::TensSlip;
        }
        if (decoy == flipped) {
            return DecoyKind::OpConfuse;
        }
        return DecoyKind::Other;
    }

    const bool carries = p.op == math::Op::Add && (lo + ro) >= 10;
    const bool borrows = p.op == math::Op::Sub && lo < ro;
    if ((carries && decoy == answer - 10) || (borrows && decoy == answer + 10)) {
        return DecoyKind::CarryMiss;
    }
    if (borrows && decoy == (p.lhs / 10) * 10 + (lo > ro ? lo - ro : ro - lo)) {
        return DecoyKind::BorrowOnes;
    }
    if (decoy == flipped) {
        return DecoyKind::OpConfuse;
    }
    if (decoy == answer + 1 || decoy == answer - 1) {
        return DecoyKind::OffByOne;
    }
    if (answer >= 10 && (answer / 10) != (answer % 10) && (answer % 10) != 0 &&
        decoy == (answer % 10) * 10 + answer / 10) {
        return DecoyKind::DigitSwap;
    }
    return DecoyKind::Other;
}

// Are the decoys actually confusable, or would a child spot them by size alone?
void testDecoyQuality()
{
    constexpr uint32_t kIterations = 200000;
    std::printf("[decoys] %u per tier -- share of each mistake type\n", kIterations);
    std::printf("  %-24s %6s %6s %6s %6s %6s %6s %7s  %8s\n", "tier", "carry", "ones",
                "tens", "sign", "off1", "swap", "unattr", "|diff|<=2");

    uint32_t total_other = 0;
    uint32_t total_all   = 0;

    for (uint8_t tier = 0; tier < math::kLevelCount; ++tier) {
        const auto level = static_cast<math::Level>(tier);
        math::Generator gen(0xD3C0Fu + tier);
        uint32_t kinds[7] = {0, 0, 0, 0, 0, 0, 0};
        uint32_t close    = 0;  // decoy within 2 of the answer

        for (uint32_t i = 0; i < kIterations; ++i) {
            const math::Problem p = gen.next(level);
            ++kinds[static_cast<int>(classify(p))];
            const int diff = static_cast<int>(p.distractor) - p.answer;
            if (diff <= 2 && diff >= -2) {
                ++close;
            }
        }
        total_other += kinds[6];
        total_all += kIterations;

        auto pct = [&](uint32_t n) { return 100.0 * n / kIterations; };
        std::printf("  %-24s %5.1f%% %5.1f%% %5.1f%% %5.1f%% %5.1f%% %5.1f%% %6.2f%%  %8.1f%%\n",
                    math::levelName(level), pct(kinds[0]), pct(kinds[1]), pct(kinds[2]),
                    pct(kinds[3]), pct(kinds[4]), pct(kinds[5]), pct(kinds[6]), pct(close));
    }

    const double other_pct = 100.0 * total_other / total_all;
    std::printf("  overall unattributable (arbitrary fallback): %.3f%%\n", other_pct);
    // Deliberately not asserting anything about digit counts. A decoy with a
    // different digit count sounds spottable, but the most valuable decoy in
    // the whole set -- 9 + 9 = 8, the dropped carry -- is exactly that. What
    // matters is that a decoy corresponds to a mistake a child makes, not that
    // it looks similar to the answer.
    check(other_pct < 2.0, "fallback decoys exceed 2% -- the mistake pool is not covering");
}

// A handful of concrete problems, so the decoys can be eyeballed.
void showSamples()
{
    std::printf("[samples]\n");
    const char* names[] = {"carry", "ones", "tens", "sign", "off1", "swap", "??"};
    for (uint8_t tier = 0; tier < math::kLevelCount; ++tier) {
        const auto level = static_cast<math::Level>(tier);
        math::Generator gen(0xA11CEu + tier);
        std::printf("  %-24s", math::levelName(level));
        for (int i = 0; i < 4; ++i) {
            const math::Problem p = gen.next(level);
            std::printf("   %2u %c %-2u = %2u vs %2u (%s)", p.lhs,
                        p.op == math::Op::Add ? '+' : '-', p.rhs, p.answer, p.distractor,
                        names[static_cast<int>(classify(p))]);
        }
        std::printf("\n");
    }
}

void testSession()
{
    std::printf("[session] round structure, streaks, stars, adaptation\n");
    math::Generator gen(24680);
    math::Session session(gen);

    // All correct: ten problems, no replay, three stars, streak of ten.
    {
        auto trace = playRound(session, math::Level::AddWithin10,
                               [](const math::Session& s) { return s.current().answer_on_left; });
        check(trace.fresh_served == math::kProblemsPerRound, "perfect round was not 10 problems");
        check(trace.retry_served == 0, "perfect round still queued a replay");
        check(session.stats().fresh_correct == 10, "perfect round did not score 10");
        check(session.stats().best_streak == 10, "perfect round streak was not 10");
        check(session.stars() == 3, "perfect round did not earn 3 stars");
        check(session.stats().gold_correct, "perfect round missed the gold flag");
        check(session.suggestLevel(math::Level::AddWithin10) == math::Level::SubWithin10,
              "perfect round did not promote");
        check(session.suggestLevel(math::Level::TwoDigitFull) == math::Level::TwoDigitFull,
              "promotion ran past the top tier");
    }

    // All wrong: ten fresh, then ten replays, no stars, demote.
    {
        auto trace = playRound(session, math::Level::TwoDigitFull,
                               [](const math::Session& s) { return !s.current().answer_on_left; });
        check(trace.fresh_served == math::kProblemsPerRound, "failed round was not 10 problems");
        check(trace.retry_served == math::kProblemsPerRound, "every miss should have replayed");
        check(session.stats().fresh_correct == 0, "failed round scored above zero");
        check(session.stats().retry_asked == 10, "replay phase did not ask 10");
        check(session.stats().best_streak == 0, "failed round recorded a streak");
        check(session.stars() == 0, "failed round earned stars");
        check(!session.stats().gold_correct, "failed round still flagged gold");
        check(session.suggestLevel(math::Level::TwoDigitFull) == math::Level::TwoDigitPlusOne,
              "failed round did not demote");
        check(session.suggestLevel(math::Level::AddWithin10) == math::Level::AddWithin10,
              "demotion ran past the bottom tier");
    }

    // Missing during the replay must not re-queue, or the round never ends.
    // The guard in playRound would have caught an infinite loop above; this
    // asserts the exact count instead.
    {
        int served = 0;
        session.startRound(math::Level::SubWithin10);
        while (session.active()) {
            ++served;
            check(served <= 25, "replay re-queued and the round ran away");
            session.submit(!session.current().answer_on_left);
            session.advance();
        }
        check(served == 20, "all-wrong round should be exactly 10 + 10 problems");
    }

    // Star thresholds sit at 9 / 7 / 5.
    {
        struct Case {
            int correct;
            uint8_t stars;
        };
        const Case cases[] = {{10, 3}, {9, 3}, {8, 2}, {7, 2}, {6, 1}, {5, 1}, {4, 0}, {0, 0}};
        for (const auto& c : cases) {
            int answered = 0;
            session.startRound(math::Level::AddWithin10);
            while (session.active() && !session.inRetry()) {
                const bool correct = answered < c.correct;
                session.submit(correct == session.current().answer_on_left);
                session.advance();
                ++answered;
            }
            check(session.stats().fresh_correct == c.correct,
                  "scored " + std::to_string(session.stats().fresh_correct) + " expected " +
                      std::to_string(c.correct));
            check(session.stars() == c.stars,
                  std::to_string(c.correct) + " correct should be " +
                      std::to_string(c.stars) + " stars, got " + std::to_string(session.stars()));
        }
    }

    // Promotion needs both a high score and a real streak -- 8 right in a row
    // is a child who has it; 8 right with misses sprinkled through is a child
    // who is still guessing at the hard ones.
    {
        // Misses at 2 and 6 break the run into 2 / 3 / 3.
        session.startRound(math::Level::CarryWithin20);
        int i = 0;
        while (session.active() && !session.inRetry()) {
            const bool correct = (i != 2 && i != 6);
            session.submit(correct == session.current().answer_on_left);
            session.advance();
            ++i;
        }
        check(session.stats().fresh_correct == 8, "expected 8 correct in the scattered test");
        check(session.stats().best_streak == 3, "expected a best streak of 3");
        check(session.suggestLevel(math::Level::CarryWithin20) == math::Level::CarryWithin20,
              "promoted without a 5-streak");
    }
    {
        // Same score, but the 8 are consecutive.
        session.startRound(math::Level::CarryWithin20);
        int i = 0;
        while (session.active() && !session.inRetry()) {
            const bool correct = (i < 8);
            session.submit(correct == session.current().answer_on_left);
            session.advance();
            ++i;
        }
        check(session.stats().fresh_correct == 8, "expected 8 correct in the streak test");
        check(session.stats().best_streak == 8, "expected a best streak of 8");
        check(session.suggestLevel(math::Level::CarryWithin20) == math::Level::TensOnly,
              "8 in a row should promote");
    }

    // submit() before advance() is idempotent; advance() without submit() is a no-op.
    {
        session.startRound(math::Level::AddWithin10);
        const uint32_t key = session.current().key();
        const bool right   = session.current().answer_on_left;
        check(session.submit(right), "first submit should report correct");
        check(!session.submit(!right), "second submit should be ignored");
        check(session.stats().fresh_asked == 1, "double submit was counted twice");
        session.advance();
        check(session.current().key() != key || session.index() == 2, "advance did not move on");
        session.advance();  // no submit since the last advance
        check(session.index() == 2, "advance without submit still moved on");
    }
}

// The gold question prefers the tier's hardest shape but must never step
// outside the tier's own invariants -- it draws from the same build() rules.
void testGoldQuestion()
{
    constexpr uint32_t kIterations = 100000;
    std::printf("[gold] %u per tier -- crossing-ten preference\n", kIterations);

    auto crosses = [](const math::Problem& p) {
        const uint8_t lo = p.lhs % 10;
        const uint8_t ro = p.rhs % 10;
        return p.op == math::Op::Add ? (lo + ro) >= 10 : lo < ro;
    };

    for (uint8_t tier = 0; tier < math::kLevelCount; ++tier) {
        const auto level = static_cast<math::Level>(tier);
        math::Generator gen(0x601D0000u + tier);
        uint32_t crossing = 0;
        LevelTally tally;
        // A gold question never occurs back to back: it is one problem in ten,
        // after nine ordinary ones have filled the repeat history. Sampling it
        // in that shape matters on the small tiers, where the make-ten
        // problems are few enough that a bare nextGold() loop would exhaust
        // them into the history and measure an artifact instead.
        for (uint32_t i = 0; i < kIterations; ++i) {
            gen.clearHistory();
            for (int j = 0; j < 9; ++j) {
                (void)gen.next(level);
            }
            const math::Problem p = gen.nextGold(level);
            checkProblem(level, p, tally);
            if (crosses(p)) {
                ++crossing;
            }
        }
        const double pct = 100.0 * crossing / kIterations;
        std::printf("  tier %u %-24s crossing %5.1f%%\n", tier, math::levelName(level), pct);

        switch (level) {
            case math::Level::CarryWithin20:
                check(crossing == kIterations, "tier 2 gold must always cross ten");
                break;
            case math::Level::TensOnly:
                // Round tens cannot cross; gold must decay to a normal problem.
                check(crossing == 0, "tier 3 gold cannot cross ten, yet it did");
                break;
            case math::Level::TwoDigitPlusOne:
            case math::Level::TwoDigitFull:
                check(pct > 99.0, "gold rarely crosses on a tier where it easily could");
                break;
            case math::Level::AddWithin10:
            case math::Level::SubWithin10:
                // Crossing means sum == 10 / minuend == 10: rare per build, so
                // 24 attempts land it most of the time but not always.
                check(pct > 85.0, "gold seldom finds the make-ten shape");
                break;
        }
    }

    // Determinism must survive the extra sampling loop.
    math::Generator a(555), b(555);
    for (int i = 0; i < 10000; ++i) {
        const auto pa = a.nextGold(math::Level::TwoDigitFull);
        const auto pb = b.nextGold(math::Level::TwoDigitFull);
        check(pa.key() == pb.key() && pa.distractor == pb.distractor &&
                  pa.answer_on_left == pb.answer_on_left,
              "gold sequences diverged at " + std::to_string(i));
    }
}

void testJudgeRounds()
{
    std::printf("[judge] mixing, retry, truth balance\n");
    math::Generator gen(13579);
    math::Session session(gen);

    uint32_t truths = 0, judges_total = 0;
    uint16_t masks_seen = 0;

    for (int round = 0; round < 2000; ++round) {
        session.startRound(math::Level::CarryWithin20, true);
        uint8_t judges = 0;
        bool judge_on_gold = false;
        while (session.active() && !session.inRetry()) {
            const auto& p = session.current();
            if (p.kind == math::Kind::Judge) {
                ++judges;
                masks_seen = static_cast<uint16_t>(masks_seen | (1u << (session.index() - 1)));
                if (session.isGold()) {
                    judge_on_gold = true;
                }
                if (p.answer_on_left) {
                    ++truths;
                }
                check(p.shownValue() == (p.answer_on_left ? p.answer : p.distractor),
                      "shown value does not match the truth flag");
            }
            // Answer everything wrong so every problem replays: the retry must
            // keep the question kind.
            session.submit(!session.current().answer_on_left);
            session.advance();
        }
        check(judges == math::kJudgePerRound, "round did not mix exactly 2 judge questions");
        check(!judge_on_gold, "a judge question landed on the gold position");
        judges_total += judges;

        uint8_t retry_judges = 0;
        while (session.active()) {
            if (session.current().kind == math::Kind::Judge) {
                ++retry_judges;
            }
            session.submit(session.current().answer_on_left);
            session.advance();
        }
        check(retry_judges == math::kJudgePerRound, "retry lost the judge kind");
    }

    // The truth flag is a fair coin, and the judge slots wander across all
    // nine eligible positions.
    const double truth_pct = 100.0 * truths / judges_total;
    check(truth_pct > 45.0 && truth_pct < 55.0,
          "judge truth flag is lopsided: " + std::to_string(truth_pct));
    check(masks_seen == 0x1FF,
          "judge questions never reached some of positions 1..9");

    // A round without the flag stays pure Pick.
    session.startRound(math::Level::CarryWithin20, false);
    while (session.active()) {
        check(session.current().kind == math::Kind::Pick, "judge leaked into a plain round");
        session.submit(session.current().answer_on_left);
        session.advance();
    }
}

void testEconomy()
{
    std::printf("[economy] earnings bounds, make-ten carry, unlock edges\n");

    // Earnings track the session's own counters across randomly played rounds.
    math::Generator gen(97531);
    math::Session session(gen);
    for (int round = 0; round < 2000; ++round) {
        session.startRound(math::Level::TwoDigitPlusOne, (round % 2) == 0);
        uint32_t rng = 0x9E3779B9u ^ round;
        while (session.active()) {
            rng ^= rng << 13;
            rng ^= rng >> 17;
            rng ^= rng << 5;
            session.submit((rng & 1) == 0);
            session.advance();
        }
        const auto& s = session.stats();
        const auto e  = math::earningsForRound(s, session.stars());
        check(e.dust == s.fresh_correct + s.retry_correct + (s.gold_correct ? 1 : 0),
              "dust does not add up from the stats");
        check(e.dust <= 11, "a round paid more than 11 dust");
        check(e.stars == session.stars(), "earned stars diverge from the rating");
        check(s.gold_correct ? s.fresh_correct >= 1 : true, "gold_correct without a correct");
    }

    // settle() is exact base-ten arithmetic: value conservation plus carry count.
    {
        math::Wallet w;
        uint32_t rng = 0xABCDEF01u;
        for (int i = 0; i < 100000; ++i) {
            rng ^= rng << 13;
            rng ^= rng >> 17;
            rng ^= rng << 5;
            if (w.stars > 60000) {
                // Stay clear of the saturation clamp; it gets its own check below.
                w = math::Wallet{};
            }
            math::RoundEarnings e;
            e.dust  = static_cast<uint8_t>(rng % 12);
            e.stars = static_cast<uint8_t>((rng >> 8) % 4);
            const uint32_t before  = w.stars * 10u + w.dust;
            const uint8_t carried  = math::settle(w, e);
            const uint32_t after   = w.stars * 10u + w.dust;
            check(w.dust <= 9, "settle left dust at ten or more");
            check(after == before + e.dust + e.stars * 10u, "settle lost or invented value");
            check(carried == (before % 10 + e.dust) / 10, "carry count is wrong");
        }

        // The wallet saturates instead of wrapping: a wrap would zero a
        // child's whole collection.
        math::Wallet full;
        full.stars = 0xFFFF;
        full.dust  = 9;
        math::settle(full, math::RoundEarnings{11, 3});
        check(full.stars == 0xFFFF, "wallet wrapped at the top");
        check(full.dust <= 9, "saturated wallet still carried wrong");
    }

    // Unlock edges: strictly at the threshold, monotone, and the crossing
    // fires exactly once.
    {
        const uint16_t at = math::modeThreshold(math::Mode::Judge);
        check(at == 10, "judge threshold moved without the tests noticing");
        check(!math::modeUnlocked(at - 1, math::Mode::Judge), "unlocked below the threshold");
        check(math::modeUnlocked(at, math::Mode::Judge), "not unlocked at the threshold");
        check(math::modeJustUnlocked(at - 1, at, math::Mode::Judge), "crossing not detected");
        check(!math::modeJustUnlocked(at, at + 5, math::Mode::Judge), "crossing fired twice");
        check(!math::modeJustUnlocked(at - 2, at - 1, math::Mode::Judge),
              "crossing fired below the threshold");
    }
}

}  // namespace

int main()
{
    testGenerator();
    testDecoyQuality();
    showSamples();
    testDeterminism();
    testReroll();
    testSession();
    testGoldQuestion();
    testJudgeRounds();
    testEconomy();

    std::printf("\nchecks=%d failures=%d\nRESULT: %s\n", g_checks, g_failures,
                g_failures == 0 ? "PASS" : "FAIL");
    return g_failures == 0 ? 0 : 1;
}
