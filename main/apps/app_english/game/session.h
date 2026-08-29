/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once
#include <cstdint>
#include "../data/eng_data.h"

/**
 * @brief One trip through a unit: look at every word, then answer ten questions.
 *
 * The shape follows what a four-year-old can actually sit through. First the
 * whole unit goes past as picture cards with the word spoken -- no input to get
 * wrong, no score, just exposure. Only then do the questions start, and every
 * one of them is two cards and two buttons, exactly like the arithmetic app,
 * because that is the only interaction this hardware really has.
 *
 * Questions come in two flavours and they arrive in order of difficulty:
 * ListenPick plays the word and shows two pictures (sound to meaning, no
 * reading required), ReadPick shows the written word and two pictures (shape to
 * meaning). A child who cannot read yet still gets six questions they can win.
 *
 * There is deliberately no retry queue and no penalty. A miss is recorded so
 * the result page can name the words to look at again, and that is all it does.
 *
 * No LVGL and no ESP-IDF in here: tools/english_host_test drives this class
 * directly and asserts the invariants over every seed.
 */
namespace eng {

enum class Stage : uint8_t {
    Idle,
    Learn,  ///< flashcards, one per word in the unit
    Quiz,   ///< kQuestionsPerRound two-choice questions
    Done,
};

enum class Activity : uint8_t {
    ListenPick,  ///< hear the word, pick the picture
    ReadPick,    ///< read the word, pick the picture
};

constexpr uint8_t kQuestionsPerRound = 10;
/// The first six questions never require reading. Anything at or past this
/// index shows the written word instead of speaking it.
constexpr uint8_t kListenQuestions = 6;
/// A unit needs at least two words or there is no such thing as a wrong answer
/// to offer.
constexpr uint16_t kMinUnitWords = 2;

struct Question {
    uint16_t target   = 0;  ///< global word index of the right answer
    uint16_t decoy    = 0;  ///< global word index of the other card
    bool target_left  = false;
    Activity activity = Activity::ListenPick;
};

struct Stats {
    uint8_t asked   = 0;
    uint8_t correct = 0;
    uint8_t streak  = 0;  ///< best run within the round
};

class Session {
public:
    /// Begins the learn phase for `unit`. `seed` fixes the question order, so
    /// a host test can replay any round exactly.
    ///
    /// Returns false (and leaves the session Idle) when the unit is too small
    /// to build a two-choice question from.
    bool start(const Data& data, uint16_t unit, uint32_t seed);

    Stage stage() const
    {
        return _stage;
    }
    uint16_t unit() const
    {
        return _unit;
    }

    /// Learn phase: the word currently on the card.
    uint16_t learnWord() const;
    /// Quiz phase: the question currently on screen.
    const Question& question() const
    {
        return _question;
    }

    /// 0-based position within the current phase, and its length.
    uint8_t index() const
    {
        return _index;
    }
    uint8_t total() const;

    /// Learn phase only. Moves to the next card, or into the quiz when the
    /// unit runs out. Returns the new stage.
    Stage advanceLearn();

    /// Quiz phase only. Scores the answer but does not move on -- the app
    /// holds the feedback on screen first, then calls advanceQuiz().
    bool submit(bool picked_left);
    /// Quiz phase only. Draws the next question, or finishes the round.
    Stage advanceQuiz();

    const Stats& stats() const
    {
        return _stats;
    }
    /// 0..3, from the number right out of kQuestionsPerRound.
    uint8_t stars() const;
    /// Child-facing Chinese verdict for the result page.
    const char* verdict() const;

    /// Words missed this round, for the "look at these again" line. Returns
    /// how many were written into `out` (at most `cap`).
    uint8_t missedWords(uint16_t* out, uint8_t cap) const;

private:
    const Data* _data = nullptr;
    Stage _stage      = Stage::Idle;
    uint16_t _unit    = 0;
    uint16_t _first   = 0;
    uint16_t _count   = 0;
    uint8_t _index    = 0;
    uint32_t _rng     = 1;

    Question _question;
    Stats _stats;
    uint8_t _streak = 0;

    // A bitmask over the unit's words -- units are 12 words here and the
    // format caps a unit well under 32, so one word of state replaces a list.
    uint32_t _missed = 0;
    // The previous target, so the same word never appears twice running.
    uint16_t _last_target = 0xFFFF;

    uint32_t nextRandom();
    void drawQuestion();
};

}  // namespace eng
