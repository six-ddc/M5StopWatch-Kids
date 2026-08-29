/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once
#include <apps/build_config.h>
#include <apps/common/key_manager/key_manager.h>
#include <mooncake.h>
#include <cstdint>
#include <memory>
#include <vector>
#include "data/eng_data.h"
#include "game/session.h"
#include "view/view.h"

/**
 * @brief Picture-and-sound English for children who mostly cannot read yet.
 *
 * A unit is twelve concrete words. They go past once as flashcards -- picture,
 * spoken word, Chinese gloss, nothing to get wrong -- and then ten two-choice
 * questions follow. The first six play the word and show two pictures, so a
 * pre-reader can win them; the last four show the written word instead.
 *
 * Everything the child hears is a real recording, not a synthesised voice, and
 * every question is two cards under the two bezel buttons. There is no reading
 * aloud and no fill-in-the-blank: at this age the whole of the interaction is
 * look, listen, and point.
 *
 * The learning logic lives in game/session.* and the blob reader under data/,
 * both free of LVGL and ESP-IDF so tools/english_host_test can drive them.
 * This class owns only what needs a clock or a device: audio playback, the
 * feedback pause, the star-by-star celebration, and NVS.
 */
class AppEnglish : public mooncake::AppAbility {
public:
    AppEnglish();

    void onCreate() override;
    void onOpen() override;
    void onRunning() override;
    void onClose() override;

private:
    enum class Page : uint8_t {
        Units,
        Card,
        Quiz,
        Result,
    };
    enum class Phase : uint8_t {
        Asking,
        Feedback,
    };
    /// Result page: stars land one at a time, then the page is idle.
    enum class Cele : uint8_t {
        Idle,
        Stars,
        Done,
    };

    void openUnits();
    void startUnit(uint16_t unit);
    void showCard();
    void pushQuestion();
    void showResult();
    void submitAnswer(bool picked_left);
    void afterFeedback();
    void advanceCard();
    void stepCelebration(uint32_t now_ms);
    void skipCelebration();
    void handleResultAdvance();
    void handleUnitTap(int16_t unit);
    void saveProgress();

    /// Decodes a word's recording and hands it to the codec, returning how
    /// long it will take to play in milliseconds (0 if nothing played). Must
    /// not be called with the LVGL lock held -- decoding builds a PCM buffer
    /// of about 90 KB and playback blocks on I2S.
    ///
    /// The length is returned rather than discarded because audioPlay() is
    /// asynchronous and offers no way to ask whether it has finished, so the
    /// only way to schedule the next reading is to work out from the sample
    /// count when this one ends.
    uint32_t speak(uint16_t word);
    uint32_t playSfxOrSpeak(uint16_t word);

    /// Reads the word now and queues the remaining kCardRepeats - 1 readings.
    void startCardSpeech(uint16_t word);
    /// Drops any queued reading. Called whenever the child asks for a replay
    /// themselves, so the automatic sequence does not cut across it.
    void cancelRepeats();

    std::unique_ptr<input::KeyManager> _key_manager;
    std::unique_ptr<eng_view::UnitPage> _units;
    std::unique_ptr<eng_view::CardPage> _card;
    std::unique_ptr<eng_view::QuizPage> _quiz;
    std::unique_ptr<eng_view::ResultPage> _result;
    std::unique_ptr<eng::Session> _session;

    eng::Data _data;
    /// Reused across every playback so the allocation happens once. Sized for
    /// the longest clip at 44.1 kHz on first use.
    std::vector<int16_t> _pcm;

    Page _page                = Page::Units;
    Phase _phase              = Phase::Asking;
    uint16_t _selected_unit   = 0;
    uint32_t _freeze_start_ms = 0;
    uint32_t _freeze_ms       = 0;
    uint32_t _last_key_ms     = 0;
    bool _progress_dirty      = false;

    /// A card reads its word three times on its own, spaced far enough apart
    /// that a child can say it back in between. Hearing a word once is not how
    /// anyone learns it, and at this age nobody is going to work out that the
    /// picture is a button.
    ///
    /// Only the card page does this. In the quiz the recording *is* the
    /// question, and reading it three times would stretch every listening
    /// question by five seconds.
    static constexpr uint8_t kCardRepeats  = 3;
    static constexpr uint32_t kRepeatGapMs = 1200;

    uint8_t _repeat_left     = 0;
    uint32_t _repeat_next_ms = 0;
    uint16_t _repeat_word    = 0;

    eng_view::Summary _summary;
    /// Best rating per unit, one byte each, persisted as a packed blob.
    ///
    /// Sized well past the current unit count on purpose: this array is handed
    /// to UnitPage::show(), which indexes it by the selected unit, so it has to
    /// cover every unit the blob can contain. It was [8] while the data had
    /// five units and became an out-of-bounds read the moment the word list
    /// grew. loadProgress() also clamps, so a blob with more units than this
    /// degrades to "no stars recorded" rather than reading past the end.
    static constexpr uint16_t kMaxUnitsTracked = 64;
    uint8_t _best[kMaxUnitsTracked]            = {};
    Cele _cele                                 = Cele::Idle;
    uint8_t _cele_star                         = 0;
    uint32_t _cele_next_ms                     = 0;
};
