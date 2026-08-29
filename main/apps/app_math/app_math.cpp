/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include "app_math.h"
#include <apps/common/audio/audio.h>
#include <assets/assets.h>
#include <esp_random.h>
#include <hal/hal.h>
#include <hal/utils/settings/settings.h>
#include <mooncake_log.h>
#include <algorithm>

using namespace mooncake;

namespace {

// Polling the buttons costs an I2C transaction; 40 Hz is far faster than a
// child can press and leaves the bus alone the rest of the time.
constexpr uint32_t kKeyPollMs = 25;

// How long the screen holds still after an answer. A miss gets more than twice
// as long because that is when the correct card is on show and there is
// actually something to read.
constexpr uint32_t kFreezeCorrectMs = 600;
constexpr uint32_t kFreezeWrongMs   = 1500;

// Celebration pacing: stars land slowly enough to be counted, dust ticks fast
// enough to feel like pouring, and a carry earns a beat of its own.
constexpr uint32_t kStarStepMs  = 320;
constexpr uint32_t kDustStepMs  = 90;
constexpr uint32_t kCarryStepMs = 350;

// The correct-answer chime climbs the major scale with the streak, topping
// out an octave up at a streak of eight -- the same score that earns a
// promotion, so "as high as it goes" and "ready to move up" sound like one
// thing. The wrong-answer sound never changes and never gets interesting;
// nothing here may make failure fun to collect.
constexpr uint8_t kScaleSteps[8] = {0, 2, 4, 5, 7, 9, 11, 12};

constexpr const char* kNvsNamespace = "math";
constexpr const char* kNvsLevel     = "level";
constexpr const char* kNvsBest      = "best";
constexpr const char* kNvsTotal     = "total";
constexpr const char* kNvsDust      = "dust";
constexpr const char* kNvsStars     = "stars";
constexpr const char* kNvsMaxTier   = "maxtier";
constexpr const char* kNvsTierStars = "tierstars";

}  // namespace

AppMath::AppMath()
{
    setAppInfo().name = "算术";
    setAppInfo().icon = (void*)&icon_math;
}

void AppMath::onCreate()
{
    mclog::tagInfo(getAppInfo().name, "on create");
}

void AppMath::onOpen()
{
    mclog::tagInfo(getAppInfo().name, "on open");

    {
        Settings settings(kNvsNamespace, false);
        const int stored = settings.GetInt(kNvsLevel, 0);
        if (stored >= 0 && stored < math::kLevelCount) {
            _level = static_cast<math::Level>(stored);
        }
        _best_streak   = static_cast<uint8_t>(settings.GetInt(kNvsBest, 0));
        _total_correct = static_cast<uint32_t>(settings.GetInt(kNvsTotal, 0));
        _wallet.dust   = static_cast<uint8_t>(settings.GetInt(kNvsDust, 0) % math::kDustPerStar);
        _wallet.stars  = static_cast<uint16_t>(settings.GetInt(kNvsStars, 0));
        _tier_stars    = static_cast<uint16_t>(settings.GetInt(kNvsTierStars, 0));
        // Owners from before the map existed have only kNvsLevel; treating it
        // as the frontier keeps every tier they could already play reachable.
        _max_tier = static_cast<uint8_t>(
            std::clamp<int>(settings.GetInt(kNvsMaxTier, static_cast<int>(_level)), 0,
                            math::kLevelCount - 1));
    }
    _selected_tier = static_cast<uint8_t>(_level);

    // Hardware entropy, so the run of problems differs between sessions. The
    // generator itself stays a pure function of this seed, which is what lets
    // the host tests reproduce any sequence exactly.
    _generator   = std::make_unique<math::Generator>(esp_random());
    _session     = std::make_unique<math::Session>(*_generator);
    _key_manager = std::make_unique<input::KeyManager>();

    {
        LvglLockGuard lock;

        _quiz   = std::make_unique<view::QuizPage>();
        _result = std::make_unique<view::ResultPage>();
        _map    = std::make_unique<view::MapPage>();
        if (!_quiz->create(lv_screen_active()) || !_result->create(lv_screen_active()) ||
            !_map->create(lv_screen_active())) {
            mclog::tagError(getAppInfo().name, "page failed to initialise");
            _map.reset();
            _result.reset();
            _quiz.reset();
            close();
            return;
        }
    }

    mclog::tagInfo(getAppInfo().name, "level {}, max tier {}, wallet {}*{}d, {} correct so far",
                   static_cast<int>(_level), _max_tier, _wallet.stars, _wallet.dust,
                   _total_correct);

    openMap();
    _last_key_ms = GetHAL().millis();
}

view::MapInfo AppMath::mapInfo() const
{
    view::MapInfo info;
    info.selected     = _selected_tier;
    info.max_unlocked = _max_tier;
    info.wallet       = _wallet;
    for (uint8_t i = 0; i < math::kLevelCount; ++i) {
        info.best_stars[i] = tierBest(static_cast<math::Level>(i));
    }
    return info;
}

uint8_t AppMath::tierBest(math::Level level) const
{
    return static_cast<uint8_t>((_tier_stars >> (2 * static_cast<uint8_t>(level))) & 0x3);
}

void AppMath::bumpTierBest(math::Level level, uint8_t stars)
{
    if (stars > tierBest(level)) {
        const uint8_t shift = 2 * static_cast<uint8_t>(level);
        _tier_stars = static_cast<uint16_t>((_tier_stars & ~(0x3u << shift)) |
                                            (static_cast<uint16_t>(stars & 0x3) << shift));
    }
}

void AppMath::openMap()
{
    _page = Page::Map;
    _cele = Cele::Idle;

    LvglLockGuard lock;
    _map->clearTap();
    _map->show(mapInfo());
    _map->setHidden(false);
    _quiz->setHidden(true);
    _result->setHidden(true);
}

void AppMath::startRound(uint8_t tier)
{
    if (!_session) {
        return;
    }
    _level         = static_cast<math::Level>(tier);
    _selected_tier = tier;
    _session->startRound(_level, math::modeUnlocked(_wallet.stars, math::Mode::Judge));
    _phase = Phase::Asking;
    _page  = Page::Quiz;
    _cele  = Cele::Idle;

    LvglLockGuard lock;
    _quiz->clearTap();
    _result->clearTap();
    _map->clearTap();
    pushProblem();
    _quiz->setHidden(false);
    _result->setHidden(true);
    _map->setHidden(true);
}

void AppMath::pushProblem()
{
    if (!_session || !_quiz) {
        return;
    }
    _quiz->showProblem(_session->current(), _session->index(), _session->total(),
                       _session->streak(), _session->inRetry(), _session->isGold());
}

void AppMath::showResult()
{
    // _summary is fully prepared by afterFeedback() before this runs.
    _page = Page::Result;

    LvglLockGuard lock;
    _result->beginSummary(_summary);
    _result->setHidden(false);
    _quiz->setHidden(true);
    _map->setHidden(true);
}

void AppMath::submitAnswer(bool picked_left)
{
    if (!_session || !_session->active() || _page != Page::Quiz) {
        return;
    }
    // inRetry() must be read before submit() -- afterwards the session may
    // already have moved its bookkeeping on.
    const bool in_retry   = _session->inRetry();
    const bool correct    = _session->submit(picked_left);
    const uint8_t streak  = _session->streak();
    const bool milestone  = correct && (streak == 3 || streak == 5);

    {
        LvglLockGuard lock;
        _quiz->showFeedback(correct, picked_left, streak, milestone);
    }

    // Tone synthesis builds a PCM buffer, so it stays outside the lock.
    if (!correct) {
        audio::play_melody({60, 55}, 0.12f, 0.35f);
        GetHAL().vibrate(40);
    } else {
        if (in_retry) {
            // A rescued miss resolves upward: different from an ordinary hit,
            // because getting it right the second time is its own achievement.
            audio::play_melody({67, 72, 76}, 0.08f, 0.35f);
        } else {
            const uint8_t step = kScaleSteps[std::min<uint8_t>(streak, 8) - 1];
            std::vector<int> notes = {72 + step, 76 + step};
            if (milestone) {
                notes.push_back(79 + step);  // the milestone completes the triad
            }
            audio::play_melody(notes, 0.09f, 0.35f);
        }
        if (milestone) {
            // Double buzz: second pulse lands from onRunning during the pause.
            // The streak keeps counting through the replay phase, so a rescue
            // can be a milestone too.
            GetHAL().vibrate(25);
            _buzz_at_ms = GetHAL().millis() + 80;
        } else {
            GetHAL().vibrate(20, 60);
        }
    }

    _phase           = Phase::Feedback;
    _freeze_start_ms = GetHAL().millis();
    _freeze_ms       = correct ? kFreezeCorrectMs : kFreezeWrongMs;
}

void AppMath::afterFeedback()
{
    if (!_session) {
        return;
    }
    // Anything pressed during the pause is discarded rather than queued: the
    // pause exists so the answer can be read, and a queued tap would blow
    // straight through the next problem.
    _quiz->clearTap();

    _session->advance();
    if (_session->active()) {
        LvglLockGuard lock;
        pushProblem();
        return;
    }

    const auto& stats    = _session->stats();
    const uint8_t rating = _session->stars();

    _earnings                 = math::earningsForRound(stats, rating);
    const math::Wallet before = _wallet;
    math::settle(_wallet, _earnings);
    _unlock_pending = math::modeJustUnlocked(before.stars, _wallet.stars, math::Mode::Judge);

    _total_correct += stats.fresh_correct + stats.retry_correct;
    if (stats.best_streak > _best_streak) {
        _best_streak = stats.best_streak;
    }
    bumpTierBest(_level, rating);
    _level = _session->suggestLevel(_level);
    if (static_cast<uint8_t>(_level) > _max_tier) {
        _max_tier = static_cast<uint8_t>(_level);
    }
    _selected_tier = static_cast<uint8_t>(_level);

    _summary.correct       = stats.fresh_correct;
    _summary.total         = math::kProblemsPerRound;
    _summary.stars         = rating;
    _summary.verdict       = _session->verdict();
    _summary.level         = _level;
    _summary.wallet_before = before;

    mclog::tagInfo(getAppInfo().name,
                   "round done: {}/{} correct, streak {}, +{} dust, wallet {}*{}d, next tier {}",
                   stats.fresh_correct, math::kProblemsPerRound, stats.best_streak,
                   _earnings.dust, _wallet.stars, _wallet.dust, static_cast<int>(_level));

    showResult();

    _cele_star      = 0;
    _cele_dust      = 0;
    _cele           = _summary.stars > 0 ? Cele::Stars : Cele::Dust;
    _cele_next_ms   = GetHAL().millis() + kStarStepMs;
    _progress_dirty = true;
}

void AppMath::maybeShowUnlock()
{
    if (_unlock_pending) {
        _unlock_pending = false;
        _cele           = Cele::Unlock;
        audio::play_melody({72, 76, 79, 84, 88}, 0.08f, 0.4f);
        GetHAL().vibrate(30);
        LvglLockGuard lock;
        // A tap queued before the overlay existed must not dismiss it in the
        // same tick -- that would skip the one moment this economy builds to.
        _result->clearTap();
        _result->showUnlock(math::Mode::Judge);
    } else {
        _cele = Cele::Done;
    }
}

void AppMath::stepCelebration(uint32_t now_ms)
{
    // The readout is recomputed per step, so it needs the same saturation the
    // wallet itself has -- at the 65535 cap the sum would otherwise wrap and
    // show a child's fortune as zero.
    const auto capStars = [](uint32_t stars) {
        return static_cast<uint16_t>(std::min<uint32_t>(stars, 0xFFFF));
    };

    if (_cele == Cele::Stars) {
        if (_cele_star < _summary.stars) {
            // Each landing star also counts into the wallet readout, so the
            // rating and the collection are visibly the same stars.
            audio::play_melody({static_cast<int>(79 + 2 * _cele_star)}, 0.09f, 0.35f);
            GetHAL().vibrate(15, 60);
            LvglLockGuard lock;
            _result->revealStar(_cele_star);
            ++_cele_star;
            _result->showWallet(capStars(_summary.wallet_before.stars + _cele_star),
                                _summary.wallet_before.dust);
            _cele_next_ms = now_ms + kStarStepMs;
            return;
        }
        _cele         = Cele::Dust;
        _cele_next_ms = now_ms + kDustStepMs;
        return;
    }

    if (_cele == Cele::Dust) {
        if (_cele_dust < _earnings.dust) {
            ++_cele_dust;
            const uint16_t poured = static_cast<uint16_t>(_summary.wallet_before.dust + _cele_dust);
            const bool carried    = poured % math::kDustPerStar == 0;
            const uint16_t stars  = capStars(static_cast<uint32_t>(_summary.wallet_before.stars) +
                                            _summary.stars + poured / math::kDustPerStar);
            if (carried) {
                // The make-ten moment: ten dust become a star, with its own
                // chord. This is the tier-3 lesson playing itself out in the
                // wallet.
                audio::play_melody({72, 76, 79, 84}, 0.06f, 0.4f);
                GetHAL().vibrate(20);
            } else {
                audio::play_melody({static_cast<int>(70 + _cele_dust)}, 0.04f, 0.3f);
            }
            LvglLockGuard lock;
            _result->showWallet(stars, poured % math::kDustPerStar);
            if (carried) {
                _result->pulseWalletStar();
            }
            _cele_next_ms = now_ms + (carried ? kCarryStepMs : kDustStepMs);
            return;
        }
        maybeShowUnlock();
    }
}

void AppMath::skipCelebration()
{
    {
        LvglLockGuard lock;
        _result->finishSummary(_summary, _wallet);
    }
    maybeShowUnlock();
}

void AppMath::handleResultAdvance()
{
    switch (_cele) {
        case Cele::Stars:
        case Cele::Dust:
            // The first tap fast-forwards the ceremony; it never launches the
            // next round by surprise.
            skipCelebration();
            break;
        case Cele::Unlock: {
            _cele = Cele::Done;
            LvglLockGuard lock;
            _result->hideUnlock();
            break;
        }
        default:
            startRound(static_cast<uint8_t>(_level));
            break;
    }
}

void AppMath::handleMapTap(int8_t node)
{
    if (node < 0 || node == _selected_tier) {
        // The centre, or the already-selected node: go.
        startRound(_selected_tier);
        return;
    }
    if (node <= _max_tier && node < math::kLevelCount) {
        _selected_tier = static_cast<uint8_t>(node);
        LvglLockGuard lock;
        _map->show(mapInfo());
    }
    // A locked node does nothing -- it is scenery from here.
}

void AppMath::saveProgress()
{
    // Writing NVS turns the flash cache off and stalls the other core, so this
    // must never run while holding the LVGL lock or inside a draw callback.
    Settings settings(kNvsNamespace, true);
    settings.SetInt(kNvsLevel, static_cast<int>(_level));
    settings.SetInt(kNvsBest, static_cast<int>(_best_streak));
    settings.SetInt(kNvsTotal, static_cast<int>(_total_correct));
    settings.SetInt(kNvsDust, static_cast<int>(_wallet.dust));
    settings.SetInt(kNvsStars, static_cast<int>(_wallet.stars));
    settings.SetInt(kNvsMaxTier, static_cast<int>(_max_tier));
    settings.SetInt(kNvsTierStars, static_cast<int>(_tier_stars));
    _progress_dirty = false;
}

void AppMath::onRunning()
{
    if (!_key_manager || !_quiz || !_result || !_map) {
        return;
    }
    const uint32_t now_ms = GetHAL().millis();

    if (_page == Page::Quiz && _phase == Phase::Feedback) {
        // The second half of a milestone's double buzz falls due during the
        // pause and must not wait it out.
        if (_buzz_at_ms != 0 && now_ms >= _buzz_at_ms) {
            GetHAL().vibrate(25);
            _buzz_at_ms = 0;
        }
        if (now_ms - _freeze_start_ms < _freeze_ms) {
            // Still inside the pause: keys and taps are both ignored.
            return;
        }
        _phase = Phase::Asking;
        afterFeedback();
        return;
    }

    if (_page == Page::Result && (_cele == Cele::Stars || _cele == Cele::Dust) &&
        now_ms >= _cele_next_ms) {
        stepCelebration(now_ms);
    }

    // Taps are queued by the LVGL click handlers and drained here, outside the
    // callback, where locking and NVS are allowed.
    bool picked_left = false;
    if (_page == Page::Quiz && _quiz->takeTap(picked_left)) {
        submitAnswer(picked_left);
        return;
    }
    if (_page == Page::Result && _result->takeTap()) {
        handleResultAdvance();
        return;
    }
    int8_t node = -1;
    if (_page == Page::Map && _map->takeTap(node)) {
        handleMapTap(node);
        return;
    }

    if (now_ms - _last_key_ms < kKeyPollMs) {
        return;
    }
    _last_key_ms = now_ms;

    switch (_key_manager->update()) {
        case input::KeyEvent::GoHome:
            // The launcher reopens itself once this app reaches Sleeping.
            close();
            return;

        case input::KeyEvent::GoPrevious:  // A
            if (_page == Page::Quiz) {
                submitAnswer(true);
            } else if (_page == Page::Result) {
                handleResultAdvance();
            } else {
                // Step the selection through the unlocked tiers.
                _selected_tier = static_cast<uint8_t>((_selected_tier + 1) % (_max_tier + 1));
                LvglLockGuard lock;
                _map->show(mapInfo());
            }
            break;

        case input::KeyEvent::GoNext:  // B
            if (_page == Page::Quiz) {
                submitAnswer(false);
            } else if (_page == Page::Result) {
                if (_cele == Cele::Done) {
                    openMap();
                } else {
                    handleResultAdvance();
                }
            } else {
                startRound(_selected_tier);
            }
            break;

        default:
            break;
    }

    if (_progress_dirty) {
        saveProgress();
    }
}

void AppMath::onClose()
{
    mclog::tagInfo(getAppInfo().name, "on close");

    saveProgress();
    _key_manager.reset();

    LvglLockGuard lock;
    _map.reset();
    _quiz.reset();
    _result.reset();
    _session.reset();
    _generator.reset();
}
