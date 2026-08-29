/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once
#include <cstdint>
#include "session.h"

/**
 * @brief The reward economy: star dust, stars, and what stars unlock.
 *
 * One currency, two denominations: ten dust make a star. The rate is not
 * arbitrary -- the wallet is a two-digit number that carries, so a child
 * checking their balance is rehearsing the exact skill tier 3 teaches.
 *
 * Dust rewards effort (every correct answer pays, including the retry of a
 * missed problem), stars reward mastery (the round rating, first-pass answers
 * only). That split matters: the round rating stays honest for the adaptive
 * difficulty, while the child who struggled still leaves richer than they
 * came. Under the old scheme the children most in need of encouragement were
 * the only ones who got nothing.
 *
 * Like the rest of game/, this is free of LVGL and ESP-IDF and fully covered
 * by tools/math_host_test.
 */
namespace math {

constexpr uint8_t kDustPerStar = 10;
/// Extra dust for getting the round's tenth, gold question right (on top of
/// the ordinary dust that answer already earns).
constexpr uint8_t kGoldBonusDust = 1;

struct Wallet {
    uint16_t stars = 0;
    uint8_t dust   = 0;  // always 0..9; settle() carries the tens over
};

struct RoundEarnings {
    uint8_t dust  = 0;  // as earned, before any carrying
    uint8_t stars = 0;  // the round rating, banked directly
};

/// What a finished round pays out. Dust is bounded by 11: each of the ten
/// problems can pay at most once (first pass or retry, never both), plus the
/// gold bonus.
RoundEarnings earningsForRound(const RoundStats& stats, uint8_t rating);

/// Applies earnings to the wallet, carrying dust into stars. Returns how many
/// stars the dust carried into, so the UI can play the make-ten moment exactly
/// that many times.
uint8_t settle(Wallet& wallet, const RoundEarnings& earnings);

/**
 * @brief Unlockable play modes, gated on total stars.
 *
 * The reward for playing is more ways to play -- the extrinsic prize and the
 * intrinsic one point the same way, which is what keeps the stars from
 * crowding out the game itself. Modes join this enum as they are implemented;
 * the thresholds only ever gate content that actually exists.
 */
enum class Mode : uint8_t {
    Judge = 0,  // 对不对: verify a completed equation instead of computing
};
constexpr uint8_t kModeCount = 1;

uint16_t modeThreshold(Mode mode);
bool modeUnlocked(uint16_t stars, Mode mode);
/// True when this settle stepped over the mode's threshold -- the moment the
/// unlock celebration belongs to.
bool modeJustUnlocked(uint16_t stars_before, uint16_t stars_after, Mode mode);
const char* modeKidName(Mode mode);

}  // namespace math
