/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include "economy.h"

namespace math {

RoundEarnings earningsForRound(const RoundStats& stats, uint8_t rating)
{
    RoundEarnings out;
    out.dust = static_cast<uint8_t>(stats.fresh_correct + stats.retry_correct +
                                    (stats.gold_correct ? kGoldBonusDust : 0));
    out.stars = rating;
    return out;
}

uint8_t settle(Wallet& wallet, const RoundEarnings& earnings)
{
    const uint8_t total   = static_cast<uint8_t>(wallet.dust + earnings.dust);
    const uint8_t carried = static_cast<uint8_t>(total / kDustPerStar);
    wallet.dust           = static_cast<uint8_t>(total % kDustPerStar);
    // Saturate rather than wrap. 65535 stars is years of play, but a wrapped
    // wallet would zero a child's whole collection, which is the one failure
    // this system must never produce.
    const uint32_t stars = static_cast<uint32_t>(wallet.stars) + earnings.stars + carried;
    wallet.stars         = stars > 0xFFFF ? 0xFFFF : static_cast<uint16_t>(stars);
    return carried;
}

uint16_t modeThreshold(Mode mode)
{
    switch (mode) {
        case Mode::Judge:
            return 10;
    }
    return 0xFFFF;
}

bool modeUnlocked(uint16_t stars, Mode mode)
{
    return stars >= modeThreshold(mode);
}

bool modeJustUnlocked(uint16_t stars_before, uint16_t stars_after, Mode mode)
{
    return !modeUnlocked(stars_before, mode) && modeUnlocked(stars_after, mode);
}

const char* modeKidName(Mode mode)
{
    switch (mode) {
        case Mode::Judge:
            return "对不对";
    }
    return "";
}

}  // namespace math
