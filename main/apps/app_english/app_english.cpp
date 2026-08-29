/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include "app_english.h"
#include <apps/common/audio/audio.h>
#include <assets/assets.h>
#include <assets/english/english_data.h>
#include <esp_random.h>
#include <hal/hal.h>
#include <hal/utils/settings/settings.h>
#include <mooncake_log.h>
#include <algorithm>
#include "data/adpcm.h"

using namespace mooncake;

namespace {

// Polling the buttons costs an I2C transaction; 40 Hz is far quicker than a
// child can press.
constexpr uint32_t kKeyPollMs = 25;

// How long the screen holds after an answer. A miss gets longer because that
// is when the right picture is outlined and the word is spelled out -- there
// is something to look at.
constexpr uint32_t kFreezeCorrectMs = 700;
constexpr uint32_t kFreezeWrongMs   = 1800;

constexpr uint32_t kStarStepMs = 320;

constexpr const char* kNvsNamespace = "english";
constexpr const char* kNvsUnit      = "unit";
constexpr const char* kNvsBest      = "best";

}  // namespace

AppEnglish::AppEnglish()
{
    setAppInfo().name = "英语";
#if !KIDS_STANDALONE
    // Only a build with a launcher has anything to draw the icon on;
    // a single-app build leaves the image out of the firmware entirely,
    // so referencing it here would not even link.
    setAppInfo().icon = (void*)&icon_english;
#endif
}

void AppEnglish::onCreate()
{
    mclog::tagInfo(getAppInfo().name, "on create");
}

void AppEnglish::onOpen()
{
    mclog::tagInfo(getAppInfo().name, "on open");

    if (!_data.load(english_data_blob, english_data_blob_size)) {
        // A truncated or mis-generated blob is not something to limp along
        // with: every page would draw blanks.
        mclog::tagError(getAppInfo().name, "english_data blob rejected");
        close();
        return;
    }

    {
        Settings settings(kNvsNamespace, false);
        const int stored = settings.GetInt(kNvsUnit, 0);
        if (stored >= 0 && stored < _data.unitCount()) {
            _selected_unit = static_cast<uint16_t>(stored);
        }
        // Ratings are one byte per unit. GetBlob takes the capacity by
        // pointer and writes back what it actually found, so a missing key or
        // a blob from an older build with fewer units just leaves the tail at
        // zero rather than failing the open.
        size_t best_len = sizeof(_best);
        settings.GetBlob(kNvsBest, _best, &best_len);
    }

    if (_data.unitCount() > kMaxUnitsTracked) {
        // UnitPage indexes _best by the selected unit, so a blob with more
        // units than we track would read past the array. Refusing to open is
        // better than a silent out-of-bounds read on a device with no
        // debugger; the fix is to raise kMaxUnitsTracked and reflash.
        mclog::tagError(getAppInfo().name, "blob has {} units, only {} tracked", _data.unitCount(), kMaxUnitsTracked);
        close();
        return;
    }

    _session     = std::make_unique<eng::Session>();
    _key_manager = std::make_unique<input::KeyManager>();

    {
        LvglLockGuard lock;

        _units  = std::make_unique<eng_view::UnitPage>();
        _card   = std::make_unique<eng_view::CardPage>();
        _quiz   = std::make_unique<eng_view::QuizPage>();
        _result = std::make_unique<eng_view::ResultPage>();
        if (!_units->create(lv_screen_active()) || !_card->create(lv_screen_active()) ||
            !_quiz->create(lv_screen_active()) || !_result->create(lv_screen_active())) {
            mclog::tagError(getAppInfo().name, "page failed to initialise");
            _result.reset();
            _quiz.reset();
            _card.reset();
            _units.reset();
            close();
            return;
        }
    }

    mclog::tagInfo(getAppInfo().name, "{} words in {} units, {}x{} art, {} Hz audio", _data.wordCount(),
                   _data.unitCount(), _data.imageW(), _data.imageH(), _data.audioRate());

    openUnits();
    _last_key_ms = GetHAL().millis();
}

uint32_t AppEnglish::speak(uint16_t word)
{
    eng::Audio clip;
    if (!_data.audio(word, clip)) {
        return 0;
    }
    // Decoding expands 16 kHz ADPCM to whatever the codec runs at (44.1 kHz),
    // which is a ~70 KB buffer. It must not happen under the LVGL lock, and
    // the codec copies what it is given, so reusing _pcm is safe.
    const uint32_t out_rate = static_cast<uint32_t>(GetHAL().getAudioSampleRate());
    adpcm::decodeToPlayback(clip, _data.audioRate(), out_rate, _pcm);
    if (_pcm.empty()) {
        return 0;
    }
    GetHAL().audioPlay(_pcm);
    return static_cast<uint32_t>(static_cast<uint64_t>(_pcm.size()) * 1000 / out_rate);
}

uint32_t AppEnglish::playSfxOrSpeak(uint16_t word)
{
    // A moo is a better prompt than the word "cow" for a four-year-old, so it
    // wins when the unit carries one. No sound-effect source is wired up yet,
    // so in practice this falls through to the recording -- the branch is here
    // because the format already reserves the slot.
    eng::Audio clip;
    if (_data.sfx(word, clip)) {
        const uint32_t out_rate = static_cast<uint32_t>(GetHAL().getAudioSampleRate());
        adpcm::decodeToPlayback(clip, _data.audioRate(), out_rate, _pcm);
        if (!_pcm.empty()) {
            GetHAL().audioPlay(_pcm);
            return static_cast<uint32_t>(static_cast<uint64_t>(_pcm.size()) * 1000 / out_rate);
        }
    }
    return speak(word);
}

void AppEnglish::startCardSpeech(uint16_t word)
{
    _repeat_word = word;
    // The first reading happens now; the other two are queued against the
    // clock, so the child hears the word, then silence long enough to try it
    // themselves, then the word again.
    _repeat_left    = kCardRepeats > 0 ? kCardRepeats - 1 : 0;
    _repeat_next_ms = GetHAL().millis() + speak(word) + kRepeatGapMs;
}

void AppEnglish::cancelRepeats()
{
    _repeat_left = 0;
}

void AppEnglish::openUnits()
{
    _page = Page::Units;
    _cele = Cele::Idle;
    cancelRepeats();

    LvglLockGuard lock;
    _units->clearTap();
    _units->show(_data, _selected_unit, _best);
    _units->setHidden(false);
    _card->setHidden(true);
    _quiz->setHidden(true);
    _result->setHidden(true);
}

void AppEnglish::startUnit(uint16_t unit)
{
    if (!_session || !_session->start(_data, unit, esp_random())) {
        mclog::tagError(getAppInfo().name, "unit {} will not start", unit);
        return;
    }
    _selected_unit = unit;
    _phase         = Phase::Asking;
    _page          = Page::Card;
    _cele          = Cele::Idle;

    {
        LvglLockGuard lock;
        _card->clearTap();
        _quiz->clearTap();
        _result->clearTap();
        _units->clearTap();
        _card->show(_data, _session->learnWord(), _session->index(), _session->total());
        _card->setHidden(false);
        _units->setHidden(true);
        _quiz->setHidden(true);
        _result->setHidden(true);
    }

    // Audio outside the lock: decoding allocates and playback blocks.
    startCardSpeech(_session->learnWord());
}

void AppEnglish::showCard()
{
    {
        LvglLockGuard lock;
        _card->show(_data, _session->learnWord(), _session->index(), _session->total());
    }
    startCardSpeech(_session->learnWord());
}

void AppEnglish::pushQuestion()
{
    const eng::Question& q = _session->question();
    {
        LvglLockGuard lock;
        _quiz->show(_data, q, _session->index(), _session->total(), _session->stats().streak);
    }
    if (q.activity == eng::Activity::ListenPick) {
        // The sound *is* the question here, so it plays itself.
        speak(q.target);
    }
}

void AppEnglish::advanceCard()
{
    const eng::Stage next = _session->advanceLearn();
    if (next == eng::Stage::Learn) {
        showCard();
        return;
    }

    // The unit has been seen; questions start.
    _page  = Page::Quiz;
    _phase = Phase::Asking;
    {
        LvglLockGuard lock;
        _quiz->clearTap();
        _quiz->setHidden(false);
        _card->setHidden(true);
    }
    pushQuestion();
}

void AppEnglish::submitAnswer(bool picked_left)
{
    if (_page != Page::Quiz || _phase != Phase::Asking) {
        return;
    }
    const eng::Question q = _session->question();
    const bool correct    = _session->submit(picked_left);

    {
        LvglLockGuard lock;
        _quiz->showFeedback(_data, q, correct, picked_left);
    }

    // Tone synthesis builds a PCM buffer, so it stays outside the lock.
    if (correct) {
        audio::play_melody({72, 76, 79}, 0.09f, 0.35f);
        GetHAL().vibrate(20, 60);
    } else {
        audio::play_melody({60, 55}, 0.12f, 0.35f);
        GetHAL().vibrate(40);
    }

    _phase           = Phase::Feedback;
    _freeze_start_ms = GetHAL().millis();
    _freeze_ms       = correct ? kFreezeCorrectMs : kFreezeWrongMs;
}

void AppEnglish::afterFeedback()
{
    // Anything pressed during the pause is dropped rather than queued: the
    // pause exists so the answer can be seen, and a queued tap would blow
    // through the next question.
    {
        LvglLockGuard lock;
        _quiz->clearTap();
    }

    if (_session->advanceQuiz() == eng::Stage::Quiz) {
        pushQuestion();
        return;
    }

    // Round over.
    const eng::Stats& stats = _session->stats();
    const uint8_t rating    = _session->stars();

    if (_selected_unit < kMaxUnitsTracked && rating > _best[_selected_unit]) {
        _best[_selected_unit] = rating;
    }

    _summary         = eng_view::Summary{};
    _summary.correct = stats.correct;
    _summary.total   = eng::kQuestionsPerRound;
    _summary.stars   = rating;
    _summary.verdict = _session->verdict();
    _summary.unit    = _data.unit(_selected_unit).title;

    uint16_t missed[3]    = {};
    _summary.missed_count = _session->missedWords(missed, 3);
    for (uint8_t i = 0; i < _summary.missed_count; ++i) {
        _summary.missed[i] = _data.word(missed[i]).text;
    }

    mclog::tagInfo(getAppInfo().name, "unit {} done: {}/{} correct, best streak {}, {} stars", _selected_unit,
                   stats.correct, eng::kQuestionsPerRound, stats.streak, rating);

    showResult();

    _cele_star      = 0;
    _cele           = _summary.stars > 0 ? Cele::Stars : Cele::Done;
    _cele_next_ms   = GetHAL().millis() + kStarStepMs;
    _progress_dirty = true;
}

void AppEnglish::showResult()
{
    _page = Page::Result;

    LvglLockGuard lock;
    _result->clearTap();
    _result->beginSummary(_summary);
    _result->setHidden(false);
    _quiz->setHidden(true);
    _card->setHidden(true);
    _units->setHidden(true);
}

void AppEnglish::stepCelebration(uint32_t now_ms)
{
    if (_cele != Cele::Stars) {
        return;
    }
    if (_cele_star < _summary.stars) {
        audio::play_melody({static_cast<int>(79 + 2 * _cele_star)}, 0.09f, 0.35f);
        GetHAL().vibrate(15, 60);
        {
            LvglLockGuard lock;
            _result->revealStar(_cele_star);
        }
        ++_cele_star;
        _cele_next_ms = now_ms + kStarStepMs;
        return;
    }
    _cele = Cele::Done;
}

void AppEnglish::skipCelebration()
{
    {
        LvglLockGuard lock;
        _result->finishSummary(_summary);
    }
    _cele = Cele::Done;
}

void AppEnglish::handleResultAdvance()
{
    if (_cele == Cele::Stars) {
        // The first press fast-forwards the ceremony; it never starts the next
        // round by surprise.
        skipCelebration();
        return;
    }
    startUnit(_selected_unit);
}

void AppEnglish::handleUnitTap(int16_t unit)
{
    if (unit < 0 || unit == static_cast<int16_t>(_selected_unit)) {
        startUnit(_selected_unit);
        return;
    }
    if (unit < static_cast<int16_t>(_data.unitCount())) {
        _selected_unit = static_cast<uint16_t>(unit);
        LvglLockGuard lock;
        _units->show(_data, _selected_unit, _best);
    }
}

void AppEnglish::saveProgress()
{
    // Writing NVS turns the flash cache off and stalls the other core, so this
    // must never run while holding the LVGL lock.
    Settings settings(kNvsNamespace, true);
    settings.SetInt(kNvsUnit, static_cast<int>(_selected_unit));
    settings.SetBlob(kNvsBest, _best, sizeof(_best));
    _progress_dirty = false;
}

void AppEnglish::onRunning()
{
    if (!_key_manager || !_units || !_card || !_quiz || !_result || !_session) {
        return;
    }
    const uint32_t now_ms = GetHAL().millis();

    if (_page == Page::Quiz && _phase == Phase::Feedback) {
        if (now_ms - _freeze_start_ms < _freeze_ms) {
            return;  // still inside the pause: keys and taps both ignored
        }
        _phase = Phase::Asking;
        afterFeedback();
        return;
    }

    if (_page == Page::Result && _cele == Cele::Stars && now_ms >= _cele_next_ms) {
        stepCelebration(now_ms);
    }

    // The queued readings. Guarding on the page is what makes "keep reading
    // only while we are still here" true: turning the card or leaving the app
    // moves _page and the rest of the sequence simply never comes due.
    if (_page == Page::Card && _repeat_left > 0 && now_ms >= _repeat_next_ms) {
        --_repeat_left;
        _repeat_next_ms = now_ms + speak(_repeat_word) + kRepeatGapMs;
        return;
    }

    // Taps are queued by the LVGL click handlers and drained here, outside the
    // callback, where locking, audio and NVS are all legal.
    if (_page == Page::Card && _card->takeTap()) {
        // A tap reads the word once. Dropping the queue matters: audioPlay()
        // interrupts whatever is playing, so a queued reading coming due a
        // moment later would talk over the replay the child just asked for.
        cancelRepeats();
        speak(_session->learnWord());
        return;
    }
    // Checked before the answer tap: they are separate flags set by separate
    // objects, so this only decides which one gets served first in the rare
    // frame that has both.
    if (_page == Page::Quiz && _phase == Phase::Asking && _quiz->takeReplayTap()) {
        speak(_session->question().target);
        return;
    }
    bool picked_left = false;
    if (_page == Page::Quiz && _quiz->takeTap(picked_left)) {
        submitAnswer(picked_left);
        return;
    }
    if (_page == Page::Result && _result->takeTap()) {
        handleResultAdvance();
        return;
    }
    int16_t unit = -1;
    if (_page == Page::Units && _units->takeTap(unit)) {
        handleUnitTap(unit);
        return;
    }

    if (now_ms - _last_key_ms < kKeyPollMs) {
        return;
    }
    _last_key_ms = now_ms;

    switch (_key_manager->update()) {
        case input::KeyEvent::GoHome:
            if (_page == Page::Units) {
#if !KIDS_STANDALONE
                // The launcher reopens itself once this app reaches Sleeping.
                // A single-app build has no launcher, so the press does
                // nothing rather than leaving a blank screen.
                close();
#endif
            } else {
                // One step back out to the unit picker first, so a mis-hold
                // does not throw away a round in progress without warning.
                openUnits();
            }
            return;

        case input::KeyEvent::GoPrevious:  // A
            switch (_page) {
                case Page::Units:
                    _selected_unit = static_cast<uint16_t>((_selected_unit + 1) % _data.unitCount());
                    {
                        LvglLockGuard lock;
                        _units->show(_data, _selected_unit, _best);
                    }
                    break;
                case Page::Card:
                    cancelRepeats();
                    playSfxOrSpeak(_session->learnWord());
                    break;
                case Page::Quiz:
                    submitAnswer(true);
                    break;
                case Page::Result:
                    handleResultAdvance();
                    break;
            }
            break;

        case input::KeyEvent::GoNext:  // B
            switch (_page) {
                case Page::Units:
                    startUnit(_selected_unit);
                    break;
                case Page::Card:
                    advanceCard();
                    break;
                case Page::Quiz:
                    submitAnswer(false);
                    break;
                case Page::Result:
                    if (_cele == Cele::Done) {
                        openUnits();
                    } else {
                        skipCelebration();
                    }
                    break;
            }
            break;

        default:
            break;
    }

    if (_progress_dirty) {
        saveProgress();
    }
}

void AppEnglish::onClose()
{
    mclog::tagInfo(getAppInfo().name, "on close");

    saveProgress();
    _key_manager.reset();

    LvglLockGuard lock;
    _result.reset();
    _quiz.reset();
    _card.reset();
    _units.reset();
    _session.reset();
}
